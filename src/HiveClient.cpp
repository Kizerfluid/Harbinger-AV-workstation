#include "HiveClient.h"
#include "HiveProtocol.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>
#include <QDir>
#include <QHostInfo>
#include <QJsonArray>
#include <utility>
#include <QSslCertificate>

HiveClient::HiveClient(QString nodeId,QString hostname,QString ip,quint16 port,QObject *parent)
    : QObject(parent),node_(std::move(nodeId)),name_(std::move(hostname)),address_(std::move(ip)),port_(port) {
    socket_.setPeerVerifyMode(QSslSocket::QueryPeer); // Replaced with mandatory SHA-256 certificate pinning below.
    connect(&socket_,&QSslSocket::encrypted,this,[this] {
        fingerprint_=QString::fromLatin1(socket_.peerCertificate().digest(QCryptographicHash::Sha256).toHex());
        if (fingerprint_.size()!=64) { socket_.abort(); return; }
        const QString saved=QSettings().value("Hive/trusted/"+node_).toString();
        if (!saved.isEmpty() && saved!=fingerprint_) { lastError_="Worker TLS identity changed. Pair again only after verifying its certificate."; socket_.abort(); return; }
        if (saved==fingerprint_) acceptFingerprint(true);
        else Q_EMIT fingerprintNeeded(node_,fingerprint_);
    });
    connect(&socket_,&QSslSocket::readyRead,this,&HiveClient::receive);
    connect(&socket_,&QSslSocket::bytesWritten,this,[this](qint64) { pumpUpload(); });
    connect(&socket_,&QSslSocket::disconnected,this,[this] {
        const QString why=lastError_.isEmpty()?"Worker disconnected":lastError_;
        if (busy_) failTask(why);
        ready_=false; approved_=false; Q_EMIT disconnected(why);
    });
    connect(&socket_,&QSslSocket::errorOccurred,this,[this](QAbstractSocket::SocketError) {
        if (lastError_.isEmpty()) lastError_=socket_.errorString();
    });
}
HiveClient::~HiveClient() {
    socket_.disconnect(this);
    socket_.abort();
}
void HiveClient::connectToWorker() { lastError_.clear(); socket_.connectToHostEncrypted(address_,port_); }
QString HiveClient::nodeId() const { return node_; }
QString HiveClient::label() const { return name_; }
QString HiveClient::ip() const { return address_; }
bool HiveClient::isReady() const { return ready_; }
bool HiveClient::isBusy() const { return busy_; }
bool HiveClient::isConnectedOrConnecting() const {return socket_.state()!=QAbstractSocket::UnconnectedState;}
QSet<QString> HiveClient::encoders() const { return encoders_; }
QString HiveClient::gpuSummary() const { return gpuSummary_; }
void HiveClient::disconnectWorker() { socket_.abort(); }
void HiveClient::acceptFingerprint(bool accepted) {
    if (!accepted || socket_.state()!=QAbstractSocket::ConnectedState) { socket_.abort(); return; }
    // The user has compared a certificate fingerprint on the physical worker.
    // Store the WHOLE hash, not the short verification prefix.
    QSettings().setValue("Hive/trusted/"+node_,fingerprint_);
    verified_=true;
    send({{"type","hello"},{"version",HiveProtocol::Version},{"name",QHostInfo::localHostName().left(80)}});
}
void HiveClient::send(const QJsonObject &o) {
    if (verified_ && socket_.state()==QAbstractSocket::ConnectedState) socket_.write(HiveProtocol::frame(o));
}
void HiveClient::receive() {
    while (socket_.bytesAvailable()>0) {
        incoming_+=socket_.read(128*1024);
        QList<QJsonObject> frames;QString error;
        if (!HiveProtocol::consume(incoming_,frames,error)) { lastError_=error; socket_.abort(); return; }
        for (const auto &o:frames) handle(o);
    }
}
void HiveClient::handle(const QJsonObject &o) {
    const QString type=o.value("type").toString();
    if (type=="error") { const QString msg=o.value("message").toString("Worker error"); failTask(msg); lastError_=msg; socket_.disconnectFromHost(); return; }
    if (type=="ready" && verified_ && !approved_) {
        approved_=true;ready_=true;
        for (const auto &e:o.value("encoders").toArray()) encoders_.insert(e.toString());
        QStringList gpus;
        for (const auto &e:o.value("gpus").toArray()) {
            const auto g=e.toObject();
            QString detail=g.value("name").toString();
            QStringList verified;
            for (const auto &codec:g.value("verifiedEncoders").toArray()) verified<<codec.toString();
            detail+=verified.isEmpty()?" (unverified)":" ["+verified.join(", ")+"]";
            gpus<<detail;
        }
        gpuSummary_=gpus.isEmpty()?"Software / unverified GPU":gpus.join(", ");
        Q_EMIT ready(); return;
    }
    if (!approved_) return;
    if (type=="uploadAccepted" && uploading_) { pumpUpload(); return; }
    if (type=="uploaded" && uploading_) {
        uploading_=false; sourceFile_.close(); cachedSource_=task_.source; startEncoding(); return;
    }
    if (type=="started") { Q_EMIT logMessage(name_+" encoding with "+o.value("encoder").toString()); return; }
    if (type=="progress" && busy_) { Q_EMIT taskProgress(qBound(0,o.value("percent").toInt(),99)); return; }
    if (type=="resultStart" && busy_ && !downloading_) {
        expectedBytes_=o.value("size").toString().toLongLong();resultBytes_=0;resultHash_.reset();
        if (expectedBytes_<=0 || expectedBytes_>HiveProtocol::MaxFileSize) { failTask("Invalid output size");socket_.abort();return; }
        const QString tmp=task_.output+".harbinger-hive-part";
        if (QFileInfo::exists(task_.output) || QFileInfo::exists(tmp)) { failTask("Output already exists"); socket_.abort();return; }
        resultFile_.setFileName(tmp);
        if (!resultFile_.open(QIODevice::WriteOnly|QIODevice::Truncate)) { failTask("Cannot create output on host");socket_.abort();return; }
        downloading_=true;lastTransferPercent_=-1;
        Q_EMIT transferStatus("Downloading encoded result");return;
    }
    if (type=="resultChunk" && downloading_) {
        const QByteArray chunk=QByteArray::fromBase64(o.value("data").toString().toLatin1());
        if (chunk.isEmpty() || chunk.size()>HiveProtocol::ChunkSize || resultBytes_+chunk.size()>expectedBytes_ ||
            resultFile_.write(chunk)!=chunk.size()) { failTask("Invalid result chunk or host storage error"); socket_.abort(); return; }
        resultBytes_+=chunk.size();resultHash_.addData(chunk);
        const int pct=int(100.0*resultBytes_/expectedBytes_);
        if (pct/10!=lastTransferPercent_/10) {lastTransferPercent_=pct; Q_EMIT transferStatus(QString("Receiving output: %1%").arg(pct));}
        return;
    }
    if (type=="resultEnd" && downloading_) {
        resultFile_.close(); downloading_=false;
        const bool valid=resultBytes_==expectedBytes_ && resultBytes_==o.value("bytes").toString().toLongLong() &&
            QString::fromLatin1(resultHash_.result().toHex())==o.value("sha256").toString();
        const QString temp=task_.output+".harbinger-hive-part";
        if (!valid || QFileInfo::exists(task_.output) || !QFile::rename(temp,task_.output)) {
            QFile::remove(temp);
            failTask("Output integrity validation or final promotion failed");return;
        }
        busy_=false;ready_=true;
        Q_EMIT taskDone(true,{},task_.output);
        return;
    }
}
bool HiveClient::startTask(const HiveRemoteTask &task) {
    if (!approved_ || !ready_ || busy_ || socket_.state()!=QAbstractSocket::ConnectedState || !QFileInfo(task.source).isReadable() ||
        task.source==task.output || QFileInfo::exists(task.output)) return false;
    task_=task;busy_=true;ready_=false;resultBytes_=0;expectedBytes_=0;lastError_.clear();
    if (cachedSource_==task.source) startEncoding();
    else startUpload();
    return true;
}
void HiveClient::startUpload() {
    sourceFile_.close();sourceFile_.setFileName(task_.source);
    if (!sourceFile_.open(QIODevice::ReadOnly) || sourceFile_.size()<=0 || sourceFile_.size()>HiveProtocol::MaxFileSize) {
        failTask("Cannot open media for upload");return;
    }
    sourceBytes_=0;sourceHash_.reset();uploading_=true;lastTransferPercent_=-1;
    Q_EMIT transferStatus("Uploading source to worker");
    send({{"type","uploadStart"},{"filename",QFileInfo(task_.source).fileName()},
          {"size",QString::number(sourceFile_.size())}});
}
void HiveClient::pumpUpload() {
    if (!uploading_ || !sourceFile_.isOpen() || socket_.state()!=QAbstractSocket::ConnectedState) return;
    // uploadAccepted precedes the first chunk, so this function is triggered
    // by that response and by the socket's write-drained signal.
    static constexpr int burst=8;
    int n=0;
    while (socket_.bytesToWrite()<512*1024 && n++<burst) {
        const QByteArray chunk=sourceFile_.read(HiveProtocol::ChunkSize);
        if (chunk.isEmpty()) {
            if (!sourceFile_.atEnd()) { failTask("Read error during upload");socket_.abort();return; }
            sourceFile_.close();
            send({{"type","uploadEnd"},{"sha256",QString::fromLatin1(sourceHash_.result().toHex())}});
            return;
        }
        sourceHash_.addData(chunk);sourceBytes_+=chunk.size();
        const int pct=int(100.0*sourceBytes_/sourceFile_.size());
        if (pct/10!=lastTransferPercent_/10) {lastTransferPercent_=pct;Q_EMIT transferStatus(QString("Uploading source: %1%").arg(pct));}
        send({{"type","uploadChunk"},{"data",QString::fromLatin1(chunk.toBase64())}});
    }
}
void HiveClient::startEncoding() {
    const auto &m=task_.media;
    int audioChannels = 0;
    for (const auto &track : m.streams) {
        if (track.type == "audio" && track.index == m.audioStreamIndex) {
            audioChannels = track.channels;
            break;
        }
    }
    QJsonArray tracks;
    for (const auto &track : m.streams) {
        if (track.type != "video" && track.type != "audio") continue;
        if (tracks.size() >= 256) break;
        tracks.append(QJsonObject{{"index",track.index},{"type",track.type},
            {"channels",track.channels},{"bitDepth",track.bitDepth}});
    }
    QJsonObject media{{"duration",m.duration},{"videoIndex",m.videoStreamIndex},
        {"audioIndex",m.audioStreamIndex},{"audioChannels",audioChannels},
        {"hasVideo",m.hasVideo},{"hasAudio",m.hasAudio},
        {"videoCodec",m.videoCodec},{"streams",tracks}};
    send({{"type","encode"},{"options",HiveProtocol::encodeOptions(task_.options)},
          {"media",media},{"start",task_.start},{"length",task_.length},
          {"segment",task_.segment},{"encoder",task_.exactEncoder}});
}
void HiveClient::failTask(const QString &reason) {
    if (!busy_) return;
    sourceFile_.close(); resultFile_.close();
    QFile::remove(task_.output+".harbinger-hive-part");
    busy_=false;uploading_=false;downloading_=false;ready_=false;
    Q_EMIT taskDone(false,reason,{});
}
