#include "HiveWorker.h"
#include "HiveProtocol.h"
#include "Codec.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QNetworkDatagram>
#include <QProcess>
#include <QSettings>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QSslError>
#include <QTcpSocket>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTcpServer>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QCryptographicHash>
#include <functional>

namespace {
class SecureServer : public QTcpServer {
public:
    std::function<void(qintptr)> acceptSocket;
protected:
    void incomingConnection(qintptr socketDescriptor) override {
        if (acceptSocket) acceptSocket(socketDescriptor);
    }
};
bool isLanAddress(const QHostAddress &address) {
    if (address.protocol()!=QAbstractSocket::IPv4Protocol) return false;
    const quint32 a=address.toIPv4Address();
    return (a&0xff000000u)==0x0a000000u ||        // 10.0.0.0/8
           (a&0xfff00000u)==0xac100000u ||        // 172.16.0.0/12
           (a&0xffff0000u)==0xc0a80000u ||        // 192.168.0.0/16
           (a&0xff000000u)==0x7f000000u ||        // 127.0.0.0/8
           (a&0xffff0000u)==0xa9fe0000u ||        // link-local
           (a&0xffc00000u)==0x64400000u;          // 100.64.0.0/10
}
QString digestHex(const QSslCertificate &cert) {
    return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
}
}

class HiveWorkerSession final : public QObject {
public:
    HiveWorkerSession(HiveWorker *owner, QSslSocket *socket)
        : QObject(owner), owner_(owner), socket_(socket), inputHash_(QCryptographicHash::Sha256),
          resultHash_(QCryptographicHash::Sha256) {
        folder_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath("HarbingerHive/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QDir().mkpath(folder_);
        connect(socket_, &QSslSocket::readyRead, this, [this] { receive(); });
        connect(socket_, &QSslSocket::bytesWritten, this, [this](qint64) { pumpResult(); });
        connect(socket_, &QSslSocket::disconnected, this, [this] { deleteLater(); });
        connect(&encoder_, &QProcess::readyReadStandardOutput, this, [this] { readProgress(); });
        connect(&encoder_, &QProcess::readyReadStandardError, this, [this] {
            const QString text = QString::fromUtf8(encoder_.readAllStandardError());
            errorLog_ = (errorLog_ + text).right(6000);
        });
        connect(&encoder_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [this](int code,QProcess::ExitStatus status) { finished(code,status); });
        connect(&encoder_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) fail("FFmpeg could not start: " + encoder_.errorString());
        });
    }
    ~HiveWorkerSession() override {
        encoder_.blockSignals(true);
        encoder_.kill(); encoder_.waitForFinished(1000);
        input_.close(); result_.close();
        QDir(folder_).removeRecursively();
        if (claimed_) owner_->release();
        socket_->deleteLater();
    }
    void send(const QJsonObject &o) {
        if (socket_->state() == QAbstractSocket::ConnectedState) socket_->write(HiveProtocol::frame(o));
    }
private:
    void fail(const QString &reason) {
        if (failed_) return;
        failed_=true;
        send({{"type","error"},{"message",reason.left(500)}});
        if (encoder_.state() != QProcess::NotRunning) {encoder_.blockSignals(true);encoder_.kill();}
        if (claimed_) { claimed_ = false; owner_->release(); }
        // Do not permit partial uploads or failed jobs to be reused.
        input_.close(); result_.close();
        QFile::remove(inputPath_); QFile::remove(outputPath_);
        QTimer::singleShot(250, socket_, [s=socket_] { s->disconnectFromHost(); });
    }
    void receive() {
        while (socket_->bytesAvailable()>0) {
            incoming_ += socket_->read(128*1024);
            QList<QJsonObject> frames; QString problem;
            if (!HiveProtocol::consume(incoming_,frames,problem)) { fail(problem); return; }
            for (const auto &o : frames) {
                handle(o);
                if (socket_->state() != QAbstractSocket::ConnectedState) return;
            }
        }
    }
    void handle(const QJsonObject &o) {
        if (failed_) return;
        const QString type = o.value("type").toString();
        if (!approved_) {
            if (type != "hello" || o.value("version").toInt() != HiveProtocol::Version) {
                fail("Incompatible Hive protocol or unauthorised request"); return;
            }
            const QString label = o.value("name").toString("Unknown computer").left(80);
            const QString ip = socket_->peerAddress().toString();
            if (owner_->isHeadless()) {
                if (!owner_->isAllowedHeadlessPeer(socket_->peerAddress())) {
                    fail("Host IP is not allowlisted for this headless worker"); return;
                }
            } else {
                const auto answer = QMessageBox::question(nullptr,"Harbinger Hive — Worker approval",
                    QString("Allow %1 (%2) to transfer media and use this computer for FFmpeg encoding?\n\n"
                            "Only approve computers you trust. Access lasts for this connection only.").arg(label,ip),
                    QMessageBox::Yes | QMessageBox::No,QMessageBox::No);
                if (answer != QMessageBox::Yes) { fail("Worker denied connection"); return; }
            }
            approved_ = true;
            QJsonArray encoders;
            for (const auto &e:owner_->hardwareReport().encoders) encoders.append(e);
            QJsonArray gpus;
            for (const auto &g:owner_->hardwareReport().devices) {
                QJsonArray supported;
                for (auto it=g.codecs.cbegin();it!=g.codecs.cend();++it)
                    if (it.value().encode==Hardware::State::Verified) supported.append(it.key());
                gpus.append(QJsonObject{{"name",g.name.left(100)},{"backend",g.backend},
                                         {"verifiedEncoders",supported}});
            }
            send({{"type","ready"},{"node",owner_->nodeId()},
                  {"name",QHostInfo::localHostName()},
                  {"encoders",encoders},{"gpus",gpus},
                  {"protocol",HiveProtocol::Version}});
            Q_EMIT owner_->logMessage("Approved Hive connection from " + ip);
            return;
        }
        if (type == "uploadStart" && !busy_) {
            const qint64 size = o.value("size").toVariant().toLongLong();
            if (size <= 0 || size > HiveProtocol::MaxFileSize || (!claimed_ && !owner_->claim())) {
                fail("Worker busy or invalid media size"); return;
            }
            claimed_ = true;
            if (QStorageInfo(folder_).bytesAvailable() < size + 256 * 1024 * 1024) {
                fail("Insufficient temporary storage on worker"); return;
            }
            QFile::remove(inputPath_);
            inputPath_ = QDir(folder_).filePath("source" + HiveProtocol::cleanSuffix(o.value("filename").toString()));
            expected_ = size; received_ = 0;
            inputHash_.reset(); input_.setFileName(inputPath_);
            if (!input_.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail("Cannot create staging file"); return; }
            uploadOpen_ = true;
            send({{"type","uploadAccepted"}});
            return;
        }
        if (type == "uploadChunk" && uploadOpen_) {
            const QByteArray data = QByteArray::fromBase64(o.value("data").toString().toLatin1());
            if (data.isEmpty() || data.size() > HiveProtocol::ChunkSize ||
                received_ + data.size() > expected_ || input_.write(data) != data.size()) {
                fail("Invalid upload chunk or temporary storage write failure"); return;
            }
            inputHash_.addData(data); received_ += data.size();
            return;
        }
        if (type == "uploadEnd" && uploadOpen_) {
            input_.close(); uploadOpen_ = false;
            if (received_ != expected_ ||
                QString::fromLatin1(inputHash_.result().toHex()) != o.value("sha256").toString()) {
                fail("Source file integrity check failed"); return;
            }
            send({{"type","uploaded"}});
            return;
        }
        if (type == "encode" && claimed_ && !uploadOpen_ && encoder_.state() == QProcess::NotRunning && !sending_) {
            lastRequest_=o;retried_=false;startJob(o); return;
        }
        fail("Unexpected or unauthorised Hive message: " + type.left(50));
    }
    void startJob(const QJsonObject &request) {
        EncodeOptions options;
        if (!HiveProtocol::decodeOptions(request.value("options").toObject(),options)) {
            fail("Invalid encoding options"); return;
        }
        const auto metadata = request.value("media").toObject();
        MediaInfo info;
        info.path = inputPath_;
        info.duration = qMax(0.0,metadata.value("duration").toDouble());
        info.videoStreamIndex = metadata.value("videoIndex").toInt(-1);
        info.audioStreamIndex = metadata.value("audioIndex").toInt(-1);
        info.hasVideo = metadata.value("hasVideo").toBool();
        info.hasAudio = metadata.value("hasAudio").toBool();
        info.videoCodec = metadata.value("videoCodec").toString();
        // Reconstruct essential source stream properties sent by the host.
        // Smart 10-bit preservation depends on video bitDepth; 5.1/7.1
        // handling depends on each audio stream's source channels.
        const auto tracks=metadata.value("streams").toArray();
        if (tracks.size()>256) { fail("Too many stream descriptors"); return; }
        for (const auto &value : tracks) {
            const auto trackData=value.toObject();
            MediaStream track;
            track.index=trackData.value("index").toInt(-1);
            track.type=trackData.value("type").toString();
            if (track.index<0 || track.index>2048 ||
                (track.type!="video" && track.type!="audio")) {
                fail("Invalid stream descriptor");return;
            }
            track.channels=qBound(0,trackData.value("channels").toInt(),64);
            track.bitDepth=qBound(0,trackData.value("bitDepth").toInt(),32);
            info.streams.append(track);
        }
        if (info.streams.isEmpty() && info.hasAudio && info.audioStreamIndex>=0) {
            MediaStream track;track.index=info.audioStreamIndex;track.type="audio";
            track.channels=qBound(0,metadata.value("audioChannels").toInt(),64);
            info.streams.append(track);
        }
        const bool needsVideo = !Codec::isAudioOnly(options.container) && options.videoAction != "remove";
        const bool needsAudio = options.audioAction != "remove";
        if (!QFileInfo::exists(inputPath_) || (needsVideo && !info.hasVideo) ||
            (needsAudio && !info.hasAudio && !needsVideo) || (!needsVideo && !needsAudio)) {
            fail("Missing staged input or video track"); return;
        }
        const double start = request.value("start").toDouble();
        const double duration = request.value("length").toDouble();
        segment_ = request.value("segment").toBool(false);
        if (segment_ && options.smart) { fail("Smart Encoding cannot be segmented"); return; }
        remuxing_=false;
        finaliseSmartMkv_=options.smart && !segment_ && options.container=="mkv";
        const QString exactEncoder = request.value("encoder").toString();
        if (segment_) {
            // Same software implementation across every chunk. No mixed GPU bitstreams.
            if (start < 0 || duration <= 0 || duration > 7200 || exactEncoder.isEmpty() ||
                !owner_->hardwareReport().encoders.contains(exactEncoder) ||
                Hardware::codecForEncoder(exactEncoder) != options.videoCodec ||
                !exactEncoder.startsWith("lib")) { fail("Unsupported segmented encoding request"); return; }
            options.decodeMode = "cpu"; options.encodeMode = "cpu";
            options.container = "mkv"; options.audioEncoder = "none";
            options.videoAction = "transcode"; options.audioAction = "remove";
            options.videoEncoder = exactEncoder;
        }
        if (!owner_->allowGpu()) {
            options.encodeMode="cpu";options.decodeMode="cpu";
        }
        Hardware::Choice choice;
        if (segment_) { choice.encoder = exactEncoder; choice.softwareEncoder = exactEncoder; }
        else choice = Hardware::select(options,info,owner_->hardwareReport());
        lastChoice_=choice;
        if (!choice.ok || (choice.encoder.isEmpty() && needsVideo && options.videoAction == "transcode")) { fail("No compatible encoder on worker: " + choice.error); return; }
        options.videoEncoder = choice.encoder;
        QFile::remove(outputPath_);
        outputPath_ = QDir(folder_).filePath("result.part." + options.container);
        remuxPath_ = QDir(folder_).filePath("result.remux.mkv");
        QFile::remove(remuxPath_);
        QStringList args = Codec::buildArgs(options,info,outputPath_,Hardware::inputArgs(choice),
            Hardware::outputArgs(choice),choice.hardwareEncode && choice.encoderBackend == "vaapi");
        if (choice.hardwareEncode && choice.encoderBackend == "vaapi") {
            // Initialise VAAPI device before input (mirrors local engine).
            const int at = args.indexOf("-i");
            if (at >= 0) {
                args.insert(at,"-init_hw_device");args.insert(at+1,"vaapi=harb:"+choice.locator);
                args.insert(at+2,"-filter_hw_device");args.insert(at+3,"harb");
            }
        }
        if (owner_->threadLimit()>0) {
            args.insert(args.size()-1,"-threads");
            args.insert(args.size()-1,QString::number(owner_->threadLimit()));
        }
        if (segment_) {
            const int i = args.indexOf("-i");
            if (i >= 0) { args.insert(i+2,"-ss"); args.insert(i+3,QString::number(start,'f',3));
                         args.insert(i+4,"-t"); args.insert(i+5,QString::number(duration,'f',3)); }
        }
        currentDuration_ = segment_ ? duration : info.duration;
        progressData_.clear(); errorLog_.clear(); busy_ = true;
        encoder_.start(owner_->ffmpegPath(),args);
        send({{"type","started"},{"encoder",options.videoEncoder}});
    }
    void readProgress() {
        progressData_.append(encoder_.readAllStandardOutput());
        if (progressData_.size() > 65536) progressData_.clear();
        int end;
        while ((end=progressData_.indexOf('\n')) >= 0) {
            const auto line=progressData_.left(end).trimmed(); progressData_.remove(0,end+1);
            if (line.startsWith("out_time=") && currentDuration_ > 0) {
                const auto parts = line.mid(9).split(':');
                if (parts.size()==3) {
                    const double t=parts[0].toDouble()*3600+parts[1].toDouble()*60+parts[2].toDouble();
                    const double fraction=qBound(0.0,t/currentDuration_,1.0);
                    send({{"type","progress"},{"percent",qBound(0,
                        int(finaliseSmartMkv_ ? (remuxing_ ? 95.0 + 4.0*fraction : 95.0*fraction)
                                              : 100.0*fraction),99)}});
                }
            }
        }
    }
    void finished(int code,QProcess::ExitStatus status) {
        readProgress();
        const QString finishedPath = remuxing_ ? remuxPath_ : outputPath_;
        if (code != 0 || status != QProcess::NormalExit ||
            !QFileInfo::exists(finishedPath) || QFileInfo(finishedPath).size()<=0) {
            if (!remuxing_ && !segment_ && !retried_ && (lastChoice_.hardwareEncode || lastChoice_.hardwareDecode) &&
                lastRequest_.value("options").toObject().value("fallback").toBool(true)) {
                retried_=true;
                auto retry=lastRequest_;
                auto options=retry.value("options").toObject();
                options["encode"]="cpu";options["decode"]="cpu";
                retry["options"]=options;
                send({{"type","started"},{"encoder","Hardware failed — retrying with software"}});
                startJob(retry);
                return;
            }
            fail((remuxing_ ? "Smart MKV remux failed: " : "FFmpeg failed: ") + errorLog_.right(1500)); return;
        }
        if (finaliseSmartMkv_ && !remuxing_) {
            remuxing_=true;
            progressData_.clear();errorLog_.clear();
            encoder_.start(owner_->ffmpegPath(),{
                "-hide_banner","-nostdin","-n","-loglevel","warning",
                "-stats_period","0.25","-progress","pipe:1",
                "-i",outputPath_,"-map","0","-c","copy",
                "-map_metadata","0","-map_chapters","0",remuxPath_});
            send({{"type","started"},{"encoder","Smart MKV finalisation (lossless remux)"}});
            return;
        }
        if (remuxing_) {
            QFile::remove(outputPath_);
            outputPath_=remuxPath_;
        }
        result_.setFileName(outputPath_);
        if (!result_.open(QIODevice::ReadOnly)) { fail("Could not open encoded result"); return; }
        sending_ = true; resultHash_.reset(); sent_ = 0;
        send({{"type","resultStart"},{"size",QString::number(result_.size())}});
        QTimer::singleShot(0,this,[this] { pumpResult(); });
    }
    void pumpResult() {
        if (!sending_ || socket_->state() != QAbstractSocket::ConnectedState) return;
        int bursts=0;
        while (socket_->bytesToWrite() < 512*1024 && bursts++ < 8) {
            const QByteArray chunk = result_.read(HiveProtocol::ChunkSize);
            if (chunk.isEmpty()) {
                if (!result_.atEnd()) { fail("Could not read encoded output"); return; }
                result_.close(); sending_=false; busy_=false;
                send({{"type","resultEnd"},{"bytes",QString::number(sent_)},
                      {"sha256",QString::fromLatin1(resultHash_.result().toHex())}});
                QFile::remove(outputPath_);
                // Session stays authorised; cached staged input can serve more segments.
                return;
            }
            resultHash_.addData(chunk); sent_ += chunk.size();
            send({{"type","resultChunk"},{"data",QString::fromLatin1(chunk.toBase64())}});
        }
    }
    HiveWorker *owner_;
    QSslSocket *socket_;
    QByteArray incoming_;
    QString folder_,inputPath_,outputPath_,remuxPath_;
    QFile input_,result_;
    QProcess encoder_;
    QCryptographicHash inputHash_,resultHash_;
    QJsonObject lastRequest_;
    Hardware::Choice lastChoice_;
    bool retried_=false;
    bool remuxing_=false,finaliseSmartMkv_=false;
    QByteArray progressData_;
    QString errorLog_;
    qint64 expected_=0,received_=0,sent_=0;
    double currentDuration_=0;
    bool approved_=false,claimed_=false,uploadOpen_=false,busy_=false,sending_=false,segment_=false,failed_=false;
};

HiveWorker::HiveWorker(QObject *parent) : QObject(parent) {
    QSettings settings;
    threadLimit_=qBound(0,settings.value("Hive/threads",0).toInt(),64);
    allowGpu_=settings.value("Hive/allowGpu",true).toBool();
    id_ = settings.value("Hive/nodeId").toString();
    if (id_.isEmpty()) { id_=QUuid::createUuid().toString(QUuid::WithoutBraces); settings.setValue("Hive/nodeId",id_); }
    auto *server = new SecureServer;
    server->setParent(this);
    server->acceptSocket = [this](qintptr descriptor) { accepted(descriptor); };
    server_ = server;
    broadcast_=new QUdpSocket(this);
    advertiseTimer_=new QTimer(this);
    advertiseTimer_->setInterval(3000);
    connect(advertiseTimer_,&QTimer::timeout,this,&HiveWorker::advertise);
}
HiveWorker::~HiveWorker() { disable(); }
bool HiveWorker::enabled() const { return server_->isListening(); }
QString HiveWorker::fingerprint() const { return fingerprint_; }
QString HiveWorker::nodeId() const { return id_; }
QString HiveWorker::ffmpegPath() const { return ffmpeg_; }
const Hardware::Report &HiveWorker::hardwareReport() const { return report_; }
void HiveWorker::setFFmpeg(const QString &p) { if (!p.isEmpty()) ffmpeg_=p; }
bool HiveWorker::setHeadlessAllowedHost(const QString &ipv4) {
    QHostAddress address;
    if (!address.setAddress(ipv4) || address.protocol()!=QAbstractSocket::IPv4Protocol ||
        !isLanAddress(address)) return false;
    allowedHeadlessHost_=address;
    headless_=true;
    return true;
}
bool HiveWorker::isHeadless() const { return headless_; }
bool HiveWorker::isAllowedHeadlessPeer(const QHostAddress &peer) const {
    return headless_ && peer.protocol()==QAbstractSocket::IPv4Protocol && peer==allowedHeadlessHost_;
}
void HiveWorker::setHardwareReport(const Hardware::Report &r) { report_=r; }
void HiveWorker::setThreadLimit(int n) {
    threadLimit_=qBound(0,n,64);QSettings().setValue("Hive/threads",threadLimit_);
}
int HiveWorker::threadLimit() const {return threadLimit_;}
void HiveWorker::setAllowGpu(bool enabled) {allowGpu_=enabled;QSettings().setValue("Hive/allowGpu",enabled);}
bool HiveWorker::allowGpu() const {return allowGpu_;}
bool HiveWorker::claim() { if (claimed_) return false; claimed_=true; return true; }
void HiveWorker::release() { claimed_=false; }
bool HiveWorker::prepareCertificate(QString &problem) {
    const QString base=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/hive";
    if (!QDir().mkpath(base)) { problem="Cannot create Hive settings folder"; return false; }
    certPath_=base+"/worker.crt"; keyPath_=base+"/worker.key";
    if (!QFileInfo::exists(certPath_) || !QFileInfo::exists(keyPath_)) {
        QProcess p;
        p.start("openssl",{"req","-x509","-newkey","rsa:3072","-nodes","-sha256","-days","3650",
                          "-keyout",keyPath_,"-out",certPath_,"-subj","/CN=Harbinger-Hive-Worker"});
        if (!p.waitForFinished(30000) || p.exitCode()!=0) {
            problem="TLS identity creation requires OpenSSL CLI (openssl) available on PATH. " + QString::fromUtf8(p.readAllStandardError()).right(450);
            return false;
        }
        QFile::setPermissions(keyPath_,QFile::ReadOwner|QFile::WriteOwner);
    }
    const auto certs=QSslCertificate::fromPath(certPath_);
    if (certs.isEmpty()) { problem="Invalid Hive TLS certificate"; return false; }
    QFile key(keyPath_);
    if (!key.open(QIODevice::ReadOnly) || QSslKey(key.readAll(),QSsl::Rsa,QSsl::Pem,QSsl::PrivateKey).isNull()) {
        problem="Invalid Hive TLS private key"; return false;
    }
    fingerprint_=digestHex(certs.first());
    return true;
}
bool HiveWorker::enable(QString *error) {
    if (enabled()) return true;
    QString problem;
    if (!QSslSocket::supportsSsl()) problem="Qt TLS backend unavailable; cannot safely start Hive worker";
    else if (!prepareCertificate(problem)) {}
    else if (!server_->listen(QHostAddress::AnyIPv4,HiveProtocol::ServicePort)) problem=server_->errorString();
    if (!problem.isEmpty()) { if (error) *error=problem; return false; }
    if (report_.encoders.isEmpty()) {
        QProcess p;
        p.start(ffmpeg_,{"-hide_banner","-encoders"});
        if (p.waitForFinished(8000)) report_.encoders=Codec::parseEncoderList(QString::fromUtf8(p.readAllStandardOutput()));
    }
    advertiseTimer_->start(); advertise();
    Q_EMIT stateChanged(true);
    return true;
}
void HiveWorker::disable() {
    advertiseTimer_->stop();
    for (const auto &socket:connections_) if (socket) socket->abort();
    connections_.clear();
    if (server_->isListening()) { server_->close(); Q_EMIT stateChanged(false); }
}
void HiveWorker::advertise() {
    if (!enabled()) return;
    const QJsonObject announce{{"kind","harbinger-hive"},{"protocol",HiveProtocol::Version},
        {"node",id_},{"name",QHostInfo::localHostName().left(80)},
        {"port",int(HiveProtocol::ServicePort)}};
    broadcast_->writeDatagram(QJsonDocument(announce).toJson(QJsonDocument::Compact),
                              QHostAddress::Broadcast,HiveProtocol::DiscoveryPort);
}
void HiveWorker::accepted(qintptr descriptor) {
    auto *socket=new QSslSocket(this);
    connections_.append(socket);
    connect(socket,&QObject::destroyed,this,[this] {
        connections_.removeIf([](const QPointer<QSslSocket> &s){return s.isNull();});
    });
    QFile key(keyPath_);
    const auto cert=QSslCertificate::fromPath(certPath_);
    if (cert.isEmpty() || !key.open(QIODevice::ReadOnly)) { delete socket; return; }
    socket->setLocalCertificate(cert.first());
    socket->setPrivateKey(QSslKey(key.readAll(),QSsl::Rsa,QSsl::Pem,QSsl::PrivateKey));
    if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
    if (!isLanAddress(socket->peerAddress()) ||
        (headless_ && !isAllowedHeadlessPeer(socket->peerAddress())) || connections_.size()>8) {
        socket->abort();socket->deleteLater();return;
    }
    socket->setPeerVerifyMode(QSslSocket::VerifyNone);
    socket->startServerEncryption();
    // Socket is parented to the session after creation; only encrypted traffic
    // reaches the application protocol.
    connect(socket,&QSslSocket::encrypted,this,[this,socket] {
        new HiveWorkerSession(this,socket);
    });
    connect(socket,&QSslSocket::sslErrors,socket,[socket](const QList<QSslError> &) {
        socket->disconnectFromHost();
    });
}
