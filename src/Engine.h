#pragma once
#include "Types.h"
#include "Hardware.h"
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QVector>

struct EncodeTask {
    int index = -1;
    MediaInfo info;
    QString decodeOverride;
    QString encodeOverride;
    QString gpuOverride;
};

class Engine final : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject *parent = nullptr);
    void setExecutable(const QString &path);
    void start(const QVector<EncodeTask> &jobs, const EncodeOptions &options, const QString &folder, const Hardware::Report &report);
    void cancel();
    bool running() const;
Q_SIGNALS:
    void jobStarted(int index, const QString &output);
    void jobProgress(int index, int percent);
    void jobFinished(int index, bool success, bool cancelled, const QString &error, const QString &output);
    void queueFinished();
    void logLine(const QString &line);
private:
    void next();
    void launchActive();
    void launchMkvFinalization();
    bool retryWithSoftware();
    void finishActive(int code, QProcess::ExitStatus status);
    void parseProgress();
    void logStderr();
    QString reserveOutput(const QString &input);
    QProcess process_;
    QVector<EncodeTask> items_;
    EncodeTask current_;
    Hardware::Report hardware_;
    Hardware::Choice selected_;
    EncodeOptions activeOptions_;
    int attempt_ = 0;
    EncodeOptions settings_;
    QString executable_ = "ffmpeg";
    QString folder_;
    QString finalOutput_;
    QString partialOutput_;
    QString remuxOutput_;
    enum class Phase { Encoding, Remuxing };
    Phase phase_ = Phase::Encoding;
    bool finalizeSmartMkv_ = false;
    QString lastError_;
    QByteArray progressBuffer_;
    QSet<QString> reserved_;
    int position_ = 0;
    int activeIndex_ = -1;
    bool active_ = false;
    bool running_ = false;
    bool stopping_ = false;
    double currentDuration_ = 0.0;
};
