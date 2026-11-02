#pragma once
#include "Types.h"
#include <QObject>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSet>
#include <QSslSocket>

struct HiveRemoteTask {
    QString source;
    QString output;
    MediaInfo media;
    EncodeOptions options;
    QString exactEncoder; // set only for split/compatibility mode
    double start = 0;
    double length = 0;
    bool segment = false;
};

// Host side of a single approved, certificate-pinned TLS connection.
// A connection can encode multiple tasks, reusing an uploaded input file.
class HiveClient final : public QObject {
    Q_OBJECT
public:
    HiveClient(QString nodeId, QString hostname, QString ip, quint16 port, QObject *parent = nullptr);
    ~HiveClient() override;
    void connectToWorker();
    QString nodeId() const;
    QString label() const;
    QString ip() const;
    bool isReady() const;
    bool isBusy() const;
    bool isConnectedOrConnecting() const;
    QSet<QString> encoders() const;
    QString gpuSummary() const;
    void acceptFingerprint(bool accepted);
    bool startTask(const HiveRemoteTask &task);
    void disconnectWorker();
Q_SIGNALS:
    void fingerprintNeeded(const QString &nodeId, const QString &sha256);
    void ready();
    void taskProgress(int percent);
    void transferStatus(const QString &description);
    void taskDone(bool success, const QString &message, const QString &output);
    void disconnected(const QString &reason);
    void logMessage(const QString &message);
private:
    void send(const QJsonObject &obj);
    void receive();
    void handle(const QJsonObject &obj);
    void startUpload();
    void pumpUpload();
    void startEncoding();
    void failTask(const QString &reason);
    QString node_, name_, address_, fingerprint_, cachedSource_, lastError_, gpuSummary_;
    QSet<QString> encoders_;
    quint16 port_;
    QSslSocket socket_;
    QByteArray incoming_;
    HiveRemoteTask task_;
    QFile sourceFile_,resultFile_;
    QCryptographicHash sourceHash_{QCryptographicHash::Sha256};
    QCryptographicHash resultHash_{QCryptographicHash::Sha256};
    qint64 sourceBytes_ = 0, resultBytes_ = 0, expectedBytes_ = 0;
    int lastTransferPercent_ = -1;
    bool verified_ = false, approved_ = false, ready_ = false;
    bool busy_ = false, uploading_ = false, downloading_ = false;
};
