#pragma once
#include "MuxPlan.h"
#include <QObject>
#include <QProcess>

class MuxEngine final : public QObject {
    Q_OBJECT
public:
    explicit MuxEngine(QObject *parent = nullptr);
    void setExecutable(const QString &path);
    bool start(const MuxPlan &plan);
    void cancel();
    bool running() const { return active_; }
Q_SIGNALS:
    void progressChanged(int percentage);
    void logLine(const QString &message);
    void finished(bool success, bool cancelled, const QString &message, const QString &output);
private:
    void onProgress();
    void onErrorOutput();
    void finish(int code, QProcess::ExitStatus status);
    QProcess process_;
    QString executable_ = "ffmpeg";
    QString finalOutput_;
    QString temporaryOutput_;
    QString errors_;
    QByteArray progressBuffer_;
    bool active_ = false;
    bool stopping_ = false;
    double maxDuration_ = 0.0;
};
