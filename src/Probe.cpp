#include "Probe.h"
#include "AudioInfo.h"
#include "VideoInfo.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cmath>

namespace {
// Matroska frequently exposes a DURATION tag but omits stream.duration.
// The tag denotes an end timestamp (HH:MM:SS.nanoseconds) in typical FFmpeg MKVs.
double taggedEndSeconds(const QString &value) {
    const QStringList p = value.split(':');
    if (p.size() != 3) return 0.0;
    bool a = false, b = false, c = false;
    const double h = p.at(0).toDouble(&a);
    const double m = p.at(1).toDouble(&b);
    const double s = p.at(2).toDouble(&c);
    const double n = h * 3600.0 + m * 60.0 + s;
    return a && b && c && std::isfinite(n) && n > 0 ? n : 0.0;
}
}
Probe::Probe(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (busy_ && process_.state() != QProcess::NotRunning) { timedOut_ = true; process_.kill(); }
    });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            complete(timedOut_ ? "FFprobe timed out after 20 seconds."
                : status == QProcess::NormalExit && code == 0 ? QString{}
                : QString::fromUtf8(process_.readAllStandardError()).trimmed());
        });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(process_.errorString());
    });
}
void Probe::setExecutable(const QString &path) { executable_ = path; }
void Probe::enqueue(const QString &path) { queue_.enqueue(path); if (!busy_) next(); }
bool Probe::isBusy() const { return busy_ || !queue_.isEmpty(); }
void Probe::next() {
    if (busy_ || queue_.isEmpty()) return;
    active_ = queue_.dequeue();
    busy_ = true;
    timedOut_ = false;
    timer_.start(20000);
    process_.start(executable_, {"-v", "error", "-show_format", "-show_streams", "-show_chapters", "-of", "json", active_});
}
void Probe::complete(const QString &problem) {
    if (!busy_) return;
    busy_ = false;
    timer_.stop();
    MediaInfo info;
    info.path = active_;
    if (!problem.isEmpty()) info.error = problem;
    else {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(process_.readAllStandardOutput(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) info.error = "Could not read FFprobe metadata.";
        else {
            const QJsonObject data = doc.object();
            const QJsonObject fmt = data.value("format").toObject();
            info.duration = fmt.value("duration").toString().toDouble();
            info.hasChapters = !data.value("chapters").toArray().isEmpty();
            for (const QJsonValue &value : data.value("streams").toArray()) {
                const QJsonObject stream = value.toObject();
                const QString type = stream.value("codec_type").toString();
                const bool attachedPicture = stream.value("disposition").toObject().value("attached_pic").toInt() == 1;
                if ((type == "video" && !attachedPicture) || type == "audio" || type == "subtitle") {
                    MediaStream track;
                    track.index = stream.value("index").toInt(-1);
                    track.type = type;
                    track.codec = stream.value("codec_name").toString("unknown");
                    track.width = stream.value("width").toInt();
                    track.height = stream.value("height").toInt();
                    track.channels = stream.value("channels").toInt();
                    AudioInfo::readMetadata(track, stream);
                    VideoInfo::readMetadata(track, stream);
                    const QJsonObject tags = stream.value("tags").toObject();
                    track.language = tags.value("language").toString("und");
                    track.title = tags.value("title").toString();
                    const QJsonObject disp = stream.value("disposition").toObject();
                    track.isDefault = disp.value("default").toInt() == 1;
                    track.isForced = disp.value("forced").toInt() == 1;
                    const QJsonValue st = stream.value("start_time");
                    track.startTime = st.isString() ? st.toString().toDouble() : st.toDouble();
                    if (!std::isfinite(track.startTime)) track.startTime = 0.0;
                    const QJsonValue d = stream.value("duration");
                    track.duration = d.isString() ? d.toString().toDouble() : d.toDouble();
                    if (std::isfinite(track.duration) && track.duration > 0.0) {
                        track.durationKnown = true;
                    } else {
                        // FFprobe can put a per-stream ending timestamp in tags.DURATION
                        // for Matroska. Subtract an existing start time to recover length.
                        const double end = taggedEndSeconds(tags.value("DURATION").toString());
                        if (end > track.startTime) {
                            track.duration = end - track.startTime;
                            track.durationKnown = true;
                        }
                    }
                    info.streams.append(track);
                }
                if (type == "video" && !info.hasVideo && stream.value("disposition").toObject().value("attached_pic").toInt() != 1) {
                    info.hasVideo = true;
                    info.videoCodec = stream.value("codec_name").toString();
                    info.videoStreamIndex = stream.value("index").toInt(-1);
                    info.width = stream.value("width").toInt();
                    info.height = stream.value("height").toInt();
                } else if (type == "audio" && !info.hasAudio) {
                    info.hasAudio = true;
                    info.audioCodec = stream.value("codec_name").toString();
                    info.audioStreamIndex = stream.value("index").toInt(-1);
                }
                if (info.duration <= 0.0) {
                    const QJsonValue d = stream.value("duration");
                    const double streamDuration = d.isString() ? d.toString().toDouble() : d.toDouble();
                    if (streamDuration > info.duration) info.duration = streamDuration;
                }
            }
            if (info.streams.isEmpty()) info.error = "No video, audio or subtitle tracks found.";
        }
    }
    Q_EMIT probed(active_, info);
    active_.clear();
    QTimer::singleShot(0, this, [this] { next(); });
}
