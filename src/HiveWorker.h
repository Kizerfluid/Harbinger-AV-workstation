#pragma once
#include "Hardware.h"
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <memory>
#include <QPointer>
#include <QList>
#include <QHostAddress>

class QTcpServer;
class QSslSocket;
class QUdpSocket;
class QTimer;
class HiveWorkerSession;

// A worker is disabled by default. Remote operations are TLS protected,
// require GUI approval for EACH connection, except for the explicitly allowlisted
// --hive-worker headless mode. Encode only inside an isolated temporary directory.
class HiveWorker final : public QObject {
    Q_OBJECT
public:
    explicit HiveWorker(QObject *parent = nullptr);
    ~HiveWorker() override;
    bool enable(QString *error = nullptr);
    void disable();
    bool enabled() const;
    QString fingerprint() const;
    QString nodeId() const;
    void setFFmpeg(const QString &path);
    // Headless mode is disabled by default; only a specified private IPv4
    // host may initiate a session. IP alone is not cryptographic host auth.
    bool setHeadlessAllowedHost(const QString &ipv4);
    bool isHeadless() const;
    bool isAllowedHeadlessPeer(const QHostAddress &peer) const;
    void setHardwareReport(const Hardware::Report &report);
    void setThreadLimit(int count);
    int threadLimit() const;
    void setAllowGpu(bool allowed);
    bool allowGpu() const;
    const Hardware::Report &hardwareReport() const;
    QString ffmpegPath() const;
    bool claim();
    void release();
Q_SIGNALS:
    void logMessage(const QString &message);
    void stateChanged(bool enabled);
private:
    friend class HiveWorkerSession;
    bool prepareCertificate(QString &problem);
    void accepted(qintptr descriptor);
    void advertise();
    QTcpServer *server_ = nullptr;
    QUdpSocket *broadcast_ = nullptr;
    QTimer *advertiseTimer_ = nullptr;
    QString ffmpeg_ = "ffmpeg";
    QString certPath_;
    QString keyPath_;
    QString fingerprint_;
    QString id_;
    Hardware::Report report_;
    bool claimed_ = false;
    int threadLimit_ = 0;
    bool allowGpu_ = true;
    bool headless_ = false;
    QHostAddress allowedHeadlessHost_;
    QList<QPointer<QSslSocket>> connections_;
};
