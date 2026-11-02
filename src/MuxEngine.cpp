#include "MuxEngine.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QtGlobal>
#include <cmath>

namespace {
double durationSeconds(const QByteArray &time) {
    const auto parts = time.trimmed().split(':');
    if (parts.size() != 3) return -1;
    bool hOK = false, mOK = false, sOK = false;
    const double h = parts.at(0).toDouble(&hOK);
    const double m = parts.at(1).toDouble(&mOK);
    const double s = parts.at(2).toDouble(&sOK);
    return hOK && mOK && sOK ? h * 3600 + m * 60 + s : -1;
}
}
MuxEngine::MuxEngine(QObject *parent) : QObject(parent) {
    connect(&process_, &QProcess::readyReadStandardOutput, this, &MuxEngine::onProgress);
    connect(&process_, &QProcess::readyReadStandardError, this, &MuxEngine::onErrorOutput);
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MuxEngine::finish);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (active_ && err == QProcess::FailedToStart) {
            errors_ = process_.errorString();
            finish(-1, QProcess::CrashExit);
        }
    });
}
void MuxEngine::setExecutable(const QString &path) { executable_ = path; }
bool MuxEngine::start(const MuxPlan &plan) {
    if (active_) return false;
    const QString problem = Mux::validate(plan);
    if (!problem.isEmpty()) { Q_EMIT finished(false, false, problem, {}); return false; }
    finalOutput_ = QFileInfo(plan.output).absoluteFilePath();
    const QFileInfo fi(finalOutput_);
    temporaryOutput_ = QDir(fi.absolutePath()).filePath(fi.completeBaseName() + ".harbinger-part.mkv");
    if (QFileInfo::exists(temporaryOutput_)) {
        Q_EMIT finished(false, false, "A temporary output file already exists. Choose another output name.", {});
        return false;
    }
    maxDuration_ = 0.0;
    for (const auto &track : plan.tracks) {
        const double end = track.duration + static_cast<double>(track.delayMs) / 1000.0;
        if (std::isfinite(end) && end > maxDuration_) maxDuration_ = end;
    }
    stopping_ = false;
    active_ = true;
    errors_.clear();
    progressBuffer_.clear();
    Q_EMIT progressChanged(0);
    Q_EMIT logLine("Muxing " + QString::number(plan.tracks.size()) + " selected streams into " + finalOutput_);
    process_.start(executable_, Mux::arguments(plan, temporaryOutput_));
    return true;
}
void MuxEngine::cancel() {
    if (!active_ || stopping_) return;
    stopping_ = true;
    if (process_.state() != QProcess::NotRunning) {
        process_.terminate();
        QTimer::singleShot(3000, this, [this] {
            if (active_ && process_.state() != QProcess::NotRunning) process_.kill();
        });
    }
}
void MuxEngine::onProgress() {
    progressBuffer_.append(process_.readAllStandardOutput());
    if (progressBuffer_.size() > 65536) progressBuffer_.remove(0, progressBuffer_.size() - 8192);
    int pos;
    while ((pos = progressBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = progressBuffer_.left(pos).trimmed();
        progressBuffer_.remove(0, pos + 1);
        if (!line.startsWith("out_time=")) continue;
        const double seconds = durationSeconds(line.mid(9));
        if (seconds >= 0.0 && maxDuration_ > 0.0 && std::isfinite(maxDuration_))
            Q_EMIT progressChanged(qBound(0, static_cast<int>(100.0 * seconds / maxDuration_), 99));
    }
}
void MuxEngine::onErrorOutput() {
    const QString msg = QString::fromUtf8(process_.readAllStandardError());
    errors_ += msg;
    if (errors_.size() > 15000) errors_ = errors_.right(15000);
    for (const QString &line : msg.split('\n')) if (!line.trimmed().isEmpty()) Q_EMIT logLine(line.trimmed());
}
void MuxEngine::finish(int code, QProcess::ExitStatus status) {
    if (!active_) return;
    onProgress(); onErrorOutput();
    active_ = false;
    bool success = !stopping_ && code == 0 && status == QProcess::NormalExit && QFileInfo::exists(temporaryOutput_);
    QString problem = errors_.trimmed();
    if (success) {
        // Never replace existing output. Both paths share a directory, so rename is local.
        if (QFileInfo::exists(finalOutput_) || !QFile::rename(temporaryOutput_, finalOutput_)) {
            success = false;
            problem = "Muxing finished but the temporary file could not be renamed. It was kept at " + temporaryOutput_;
        }
    } else {
        QFile::remove(temporaryOutput_);
    }
    if (!success && problem.isEmpty()) problem = stopping_ ? "Muxing cancelled." : QString("FFmpeg exited with code %1.").arg(code);
    if (success) Q_EMIT progressChanged(100);
    Q_EMIT finished(success, stopping_, success ? QString("MKV created successfully.") : problem,
                    success ? finalOutput_ : QString{});
    finalOutput_.clear();
    temporaryOutput_.clear();
}
