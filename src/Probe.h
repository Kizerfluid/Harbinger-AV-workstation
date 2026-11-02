#pragma once
#include "Types.h"
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>

class Probe final : public QObject {
    Q_OBJECT
public:
    explicit Probe(QObject *parent = nullptr);
    void setExecutable(const QString &path);
    void enqueue(const QString &path);
    bool isBusy() const;
Q_SIGNALS:
    void probed(const QString &path, const MediaInfo &info);
private:
    void next();
    void complete(const QString &problem = {});
    QProcess process_;
    QTimer timer_;
    bool timedOut_ = false;
    QQueue<QString> queue_;
    QString executable_ = "ffprobe";
    QString active_;
    bool busy_ = false;
};
