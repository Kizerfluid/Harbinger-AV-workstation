#include "Engine.h"
#include "Codec.h"
#include "SmartEncoding.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtGlobal>
#include <cmath>

namespace {
double parseTimestamp(const QByteArray &v) {
    const QList<QByteArray> parts = v.trimmed().split(':');
    if (parts.size() != 3) return -1.0;
    bool ok1 = false, ok2 = false, ok3 = false;
    const double h = parts[0].toDouble(&ok1);
    const double m = parts[1].toDouble(&ok2);
    const double s = parts[2].toDouble(&ok3);
    return ok1 && ok2 && ok3 ? h * 3600.0 + m * 60.0 + s : -1.0;
}
}
Engine::Engine(QObject *parent) : QObject(parent) {
    connect(&process_, &QProcess::readyReadStandardOutput, this, &Engine::parseProgress);
    connect(&process_, &QProcess::readyReadStandardError, this, &Engine::logStderr);
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Engine::finishActive);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && active_) {
            lastError_ = process_.errorString();
            finishActive(-1, QProcess::CrashExit);
        }
    });
}
void Engine::setExecutable(const QString &path) { executable_ = path; }
bool Engine::running() const { return running_; }
void Engine::start(const QVector<EncodeTask> &jobs, const EncodeOptions &options, const QString &folder, const Hardware::Report &report) {
    if (running_ || jobs.isEmpty()) return;
    items_ = jobs; settings_ = options; folder_ = folder; hardware_ = report;
    position_ = 0; stopping_ = false; running_ = true;
    reserved_.clear();
    next();
}
QString Engine::reserveOutput(const QString &input) {
    const QString base = QFileInfo(input).completeBaseName() + "_encoded";
    const QString ext = Codec::extension(settings_.container);
    for (int i = 0; i < 100000; ++i) {
        const QString suffix = i == 0 ? "" : "_" + QString::number(i);
        const QString proposed = QDir(folder_).filePath(base + suffix + "." + ext);
        const QString partial = QDir(folder_).filePath(base + suffix + ".harbinger-part." + ext);
        const QString remux = QDir(folder_).filePath(base + suffix + ".harbinger-remux." + ext);
        // Never overwrite a previous incomplete encode/remux (or another
        // process's temporary file). Keep both temporary paths unique.
        if (!QFileInfo::exists(proposed) && !QFileInfo::exists(partial) &&
            !QFileInfo::exists(remux) && !reserved_.contains(proposed)) {
            reserved_.insert(proposed);
            partialOutput_ = partial;
            remuxOutput_ = remux;
            return proposed;
        }
    }
    return {};
}
void Engine::next() {
    if (!running_ || active_) return;
    if (stopping_ || position_ >= items_.size()) {
        running_ = false;
        Q_EMIT queueFinished();
        return;
    }
    current_ = items_.at(position_++);
    activeIndex_ = current_.index;
    currentDuration_ = current_.info.duration;
    activeOptions_ = settings_;
    if (settings_.smart) {
        const auto plan = SmartEncoding::makePlan(settings_, current_.info);
        if (!plan.ok) {
            Q_EMIT jobFinished(activeIndex_, false, false, "Smart Encoding: " + plan.error, {});
            QTimer::singleShot(0, this, [this] { next(); });
            return;
        }
        activeOptions_ = plan.options;
        Q_EMIT logLine("Smart Encoding plan for " + QFileInfo(current_.info.path).fileName() + ": " + plan.explanation);
    }
    if (!current_.decodeOverride.isEmpty() && !settings_.smart) activeOptions_.decodeMode = current_.decodeOverride;
    if (!current_.encodeOverride.isEmpty() && !settings_.smart) activeOptions_.encodeMode = current_.encodeOverride;
    if (!settings_.smart) {
        if (current_.gpuOverride == "default") activeOptions_.gpuId.clear();
        else if (!current_.gpuOverride.isEmpty()) activeOptions_.gpuId = current_.gpuOverride;
    }
    const bool needsVideo = !Codec::isAudioOnly(activeOptions_.container) && activeOptions_.videoAction != "remove";
    const bool needsAudio = activeOptions_.audioAction != "remove";
    if (!QFileInfo::exists(current_.info.path) || (needsVideo && !current_.info.hasVideo) ||
        (needsAudio && !current_.info.hasAudio && !needsVideo) || (!needsVideo && !needsAudio)) {
        Q_EMIT jobFinished(activeIndex_, false, false, "Missing input file or required media track.", {});
        QTimer::singleShot(0, this, [this] { next(); });
        return;
    }
    selected_ = Hardware::select(activeOptions_, current_.info, hardware_);
    if (!selected_.ok) {
        Q_EMIT jobFinished(activeIndex_, false, false, selected_.error, {});
        QTimer::singleShot(0, this, [this] { next(); });
        return;
    }
    finalOutput_ = reserveOutput(current_.info.path);
    if (finalOutput_.isEmpty()) {
        Q_EMIT jobFinished(activeIndex_, false, false, "Unable to allocate a unique output filename.", {});
        QTimer::singleShot(0, this, [this] { next(); });
        return;
    }
    attempt_ = 0;
    phase_ = Phase::Encoding;
    // The complete multi-track MKV remux resolved the reported VLC corruption
    // without touching the compressed packets. Limit this safeguard to local
    // Smart MKV jobs; leave Advanced/Hive workflows unchanged.
    finalizeSmartMkv_ = settings_.smart && activeOptions_.container == "mkv";
    active_ = true;
    Q_EMIT jobStarted(activeIndex_, finalOutput_);
    Q_EMIT logLine("Processing " + QFileInfo(current_.info.path).fileName());
    launchActive();
}
void Engine::launchActive() {
    if (stopping_) { finishActive(-1, QProcess::CrashExit); return; }
    phase_ = Phase::Encoding;
    lastError_.clear();
    progressBuffer_.clear();
    QFile::remove(partialOutput_); // Only the temporary output assigned to this job.
    EncodeOptions resolved = activeOptions_;
    if (!Codec::isAudioOnly(resolved.container) && resolved.videoAction == "transcode")
        resolved.videoEncoder = selected_.encoder;
    QStringList args = Codec::buildArgs(resolved, current_.info, partialOutput_,
        Hardware::inputArgs(selected_) + (selected_.hardwareEncode && selected_.encoderBackend == "vaapi"
            ? QStringList{"-init_hw_device", "vaapi=harb:" + selected_.locator, "-filter_hw_device", "harb"} : QStringList{}),
        Hardware::outputArgs(selected_), selected_.hardwareEncode && selected_.encoderBackend == "vaapi");
    if (resolved.videoAction != "transcode" || Codec::isAudioOnly(resolved.container)) {
        Q_EMIT logLine("Video: " + QString(Codec::isAudioOnly(resolved.container) || resolved.videoAction == "remove" ? "removed" : "stream copy (no re-encode)") +
                       "; audio: " + (resolved.audioAction == "copy" ? "stream copy (no re-encode)" :
                       resolved.audioAction == "remove" ? "removed" : "transcode") + ".");
    } else {
        Q_EMIT logLine(QString("Using %1 encoding (%2); %3 decoding. (attempt %4)")
            .arg(selected_.hardwareEncode ? "GPU" : "CPU", resolved.videoEncoder,
                 selected_.hardwareDecode ? selected_.decodeBackend : "CPU", QString::number(attempt_ + 1)));
        Q_EMIT logLine(resolved.audioAction == "copy" ? "Audio: passthrough (no re-encoding)." :
                       resolved.audioAction == "remove" ? "Audio: removed." : "Audio: transcoding.");
    }
    if (resolved.videoBitrateKbps > 0 && resolved.videoAction == "transcode")
        Q_EMIT logLine(QString("Smart video target: %1 kbps (single-pass approximate size; not exact).")
            .arg(resolved.videoBitrateKbps));
    if (resolved.audioAction == "transcode") {
        const QString rate = Codec::audioBitrates(resolved.audioEncoder).isEmpty()
            ? "encoder-controlled (lossless)"
            : QString("%1 kbps target per audio stream").arg(resolved.audioBitrate);
        Q_EMIT logLine("Audio encoding settings: " + resolved.audioEncoder + " / " + rate + ".");
    }
    if (resolved.container == "mkv") {
        Q_EMIT logLine("MKV stream mapping: preserve regular video/audio streams, subtitles, attachments and chapters unless explicitly removed.");
    } else if (current_.info.streams.size() > 2 || current_.info.hasChapters) {
        Q_EMIT logLine("Note: non-MKV output may omit extra audio, subtitles or attachments; use MKV to preserve all supported tracks.");
    }
    process_.start(executable_, args);
}
void Engine::launchMkvFinalization() {
    if (stopping_) { finishActive(-1, QProcess::CrashExit); return; }
    phase_ = Phase::Remuxing;
    lastError_.clear();
    progressBuffer_.clear();
    // The encode succeeded and the source below is a complete *temporary*
    // output. FFmpeg copies all of its tracks, tags, chapters and attachments
    // into fresh Matroska structures. No stream is decoded or re-encoded.
    Q_EMIT logLine("Smart MKV finalisation: lossless stream-copy remux (no extra dependency).");
    Q_EMIT jobProgress(activeIndex_, 95);
    const QStringList args {
        "-hide_banner", "-nostdin", "-n", "-loglevel", "warning",
        "-stats_period", "0.25", "-progress", "pipe:1",
        "-i", partialOutput_,
        "-map", "0", "-c", "copy",
        "-map_metadata", "0", "-map_chapters", "0",
        remuxOutput_
    };
    process_.start(executable_, args);
}
bool Engine::retryWithSoftware() {
    // Never re-encode because the *final remux* failed. The successfully
    // encoded temporary MKV is left available for diagnosis/recovery.
    if (phase_ != Phase::Encoding || stopping_ || !settings_.cpuFallback ||
        Codec::isAudioOnly(settings_.container) ||
        activeOptions_.videoAction != "transcode") return false;
    if (attempt_ == 0 && selected_.hardwareDecode) {
        selected_.hardwareDecode = false; selected_.decodeBackend.clear(); selected_.decodeLocator.clear();
    } else if (selected_.hardwareEncode && !selected_.softwareEncoder.isEmpty()) {
        selected_.hardwareEncode = false;
        selected_.encoder = selected_.softwareEncoder;
        selected_.encoderBackend.clear(); selected_.locator.clear();
        selected_.hardwareDecode = false;
        selected_.decodeBackend.clear(); selected_.decodeLocator.clear();
    } else return false;
    ++attempt_;
    Q_EMIT logLine("Hardware operation failed; retrying with a safer CPU path (same output codec).");
    QTimer::singleShot(0, this, [this] {
        if (active_ && !stopping_) launchActive();
    });
    return true;
}
void Engine::parseProgress() {
    progressBuffer_.append(process_.readAllStandardOutput());
    // Prevent unbounded memory on malformed output.
    if (progressBuffer_.size() > 65536) progressBuffer_.remove(0, progressBuffer_.size() - 8192);
    int end = -1;
    while ((end = progressBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = progressBuffer_.left(end).trimmed();
        progressBuffer_.remove(0, end + 1);
        if (line.startsWith("out_time=")) {
            const double t = parseTimestamp(line.mid(9));
            if (t >= 0.0 && currentDuration_ > 0.0 && std::isfinite(currentDuration_)) {
                // Reserve the final five percent for Smart MKV finalisation.
                // A remux reads/writes the whole file but does no encoding.
                const double fraction = qBound(0.0, t / currentDuration_, 1.0);
                const double percent = finalizeSmartMkv_
                    ? (phase_ == Phase::Encoding ? 95.0 * fraction : 95.0 + 4.0 * fraction)
                    : 100.0 * fraction;
                Q_EMIT jobProgress(activeIndex_, qBound(0, static_cast<int>(percent), 99));
            }
        }
    }
}
void Engine::logStderr() {
    const QString text = QString::fromUtf8(process_.readAllStandardError());
    lastError_ += text;
    if (lastError_.size() > 12000) lastError_ = lastError_.right(12000);
    for (const QString &line : text.split('\n')) if (!line.trimmed().isEmpty()) Q_EMIT logLine(line.trimmed());
}
void Engine::finishActive(int code, QProcess::ExitStatus status) {
    if (!active_) return;
    parseProgress(); logStderr();
    const bool cancelled = stopping_;
    bool success = !cancelled && code == 0 && status == QProcess::NormalExit &&
        QFileInfo::exists(phase_ == Phase::Remuxing ? remuxOutput_ : partialOutput_) &&
        QFileInfo(phase_ == Phase::Remuxing ? remuxOutput_ : partialOutput_).size() > 0;

    if (phase_ == Phase::Encoding) {
        if (!success && retryWithSoftware()) return;
        if (success && finalizeSmartMkv_) {
            // Keep the encoded file until the remux has completed successfully.
            // This is asynchronous and cancellable like the encode itself.
            launchMkvFinalization();
            return;
        }
    }

    active_ = false;
    QString problem = lastError_.trimmed();
    const bool wasRemux = phase_ == Phase::Remuxing;
    const QString publishFrom = wasRemux ? remuxOutput_ : partialOutput_;
    if (success) {
        if (QFileInfo::exists(finalOutput_) || !QFile::rename(publishFrom, finalOutput_)) {
            success = false;
            problem = "Completed, but could not publish temporary file to " + finalOutput_ +
                ". Temporary output kept at: " + publishFrom;
        } else {
            // Only remove the intermediate after the remux has been published.
            if (wasRemux && !QFile::remove(partialOutput_))
                Q_EMIT logLine("Warning: final MKV published, but could not remove encoded intermediate: " + partialOutput_);
            if (settings_.smart && settings_.smartTargetMB > 0) {
                const double actualMB = QFileInfo(finalOutput_).size() / 1000000.0;
                const double delta = 100.0 * (actualMB / settings_.smartTargetMB - 1.0);
                Q_EMIT logLine(QString("Smart size result: %1 MB vs %2 MB target (%3%4%). "
                    "Target-size mode is approximate, especially for hardware encoding.")
                    .arg(actualMB, 0, 'f', 1).arg(settings_.smartTargetMB, 0, 'f', 1)
                    .arg(delta >= 0 ? "+" : "").arg(delta, 0, 'f', 1));
            }
            if (wasRemux) Q_EMIT logLine("Smart MKV finalisation complete; original stream data preserved.");
        }
    } else if (cancelled) {
        // Cancel discards only the temporary outputs assigned to this job.
        QFile::remove(partialOutput_);
        QFile::remove(remuxOutput_);
    } else if (wasRemux) {
        // A remux failure must not be silently reported as a successful job.
        // Preserve the already-encoded temporary file for manual remux/recovery.
        QFile::remove(remuxOutput_);
        problem = "MKV finalisation failed. The encoded intermediate is preserved at: " +
            partialOutput_ + (problem.isEmpty() ? QString{} : "\nFFmpeg: " + problem);
    } else {
        QFile::remove(partialOutput_);
    }
    if (!success && problem.isEmpty())
        problem = cancelled ? "Cancelled by user." : QString("FFmpeg failed (exit code %1).").arg(code);
    if (!success && !cancelled) Q_EMIT logLine(problem);
    Q_EMIT jobFinished(activeIndex_, success, cancelled, success ? QString{} : problem,
                       success ? finalOutput_ : QString{});
    finalOutput_.clear();
    partialOutput_.clear();
    remuxOutput_.clear();
    finalizeSmartMkv_ = false;
    phase_ = Phase::Encoding;
    activeIndex_ = -1;
    QTimer::singleShot(0, this, [this] { next(); });
}
void Engine::cancel() {
    if (!running_) return;
    stopping_ = true;
    if (active_ && process_.state() != QProcess::NotRunning) {
        process_.terminate();
        QTimer::singleShot(3000, this, [this] {
            if (active_ && process_.state() != QProcess::NotRunning) process_.kill();
        });
    } else if (active_) {
        // A hardware-to-CPU retry may be scheduled but not started yet.
        // Do not leave an active queue stuck in this small gap.
        finishActive(-1, QProcess::CrashExit);
    } else {
        next();
    }
}
