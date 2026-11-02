#pragma once
#include "Types.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QRegularExpression>
#include <QStringList>
#include <QtEndian>
#include <QtGlobal>

// Versioned, length-prefixed JSON frames. Transfer chunks are 48 KiB binary
// encoded as base64, so no whole-media file is ever held in memory.
namespace HiveProtocol {
constexpr quint16 DiscoveryPort = 36839;
constexpr quint16 ServicePort = 36840;
// v5: explicit output bit depth and fractional/custom FPS for Advanced jobs.
// Both Hive peers must be upgraded; v4 remains incompatible.
constexpr int Version = 5;
constexpr int MaxFrame = 256 * 1024;
constexpr int ChunkSize = 48 * 1024;
constexpr qint64 MaxFileSize = qint64(500) * 1024 * 1024 * 1024;
inline QByteArray frame(const QJsonObject &obj) {
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (body.size() > MaxFrame) return {};
    QByteArray output(4, '\0');
    qToBigEndian<quint32>(quint32(body.size()), reinterpret_cast<uchar *>(output.data()));
    output.append(body);
    return output;
}
inline bool consume(QByteArray &buffer, QList<QJsonObject> &messages, QString &problem) {
    if (buffer.size() > MaxFrame * 2) { problem = "Protocol input buffer exceeded limit"; return false; }
    while (buffer.size() >= 4) {
        const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(buffer.constData()));
        if (!size || size > MaxFrame) { problem = "Invalid frame size"; return false; }
        if (buffer.size() < qint64(size) + 4) return true;
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(buffer.mid(4, int(size)), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) { problem = "Invalid protocol JSON"; return false; }
        messages << doc.object();
        buffer.remove(0, int(size) + 4);
    }
    return true;
}
inline bool validContainer(const QString &s) { return QStringList{"mkv","mp4","webm","mov","mp3","m4a","ogg","flac","wav","ac3","eac3","dts"}.contains(s); }
inline bool audioOnlyContainer(const QString &s) { return !QStringList{"mkv","mp4","webm","mov"}.contains(s); }
inline bool validFamily(const QString &s) { return QStringList{"h264","hevc","av1","vp9","mpeg4"}.contains(s); }
inline bool validAudio(const QString &s) { return QStringList{"aac","libmp3lame","libopus","libvorbis","flac","alac","ac3","eac3","dca","pcm_s16le","pcm_s24le","pcm_f32le","none"}.contains(s); }
inline bool validFrameRate(const QString &s) {
    if (s.isEmpty()) return true;
    static const QRegularExpression re(R"(^(\d{1,6})(?:\.(\d{1,3}))?(?:/(\d{1,5}))?$)");
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    const double numerator = m.captured(1).toDouble() +
        (m.captured(2).isEmpty() ? 0.0 : ("0." + m.captured(2)).toDouble());
    const double denominator = m.captured(3).isEmpty() ? 1.0 : m.captured(3).toDouble();
    const double fps = denominator > 0 ? numerator / denominator : 0.0;
    return fps >= 1.0 && fps <= 240.0;
}
inline QJsonObject encodeOptions(const EncodeOptions &o) {
    return {{"container",o.container},{"video",o.videoCodec},{"audio",o.audioEncoder},
        {"videoAction",o.videoAction},{"audioAction",o.audioAction},
        {"decode",o.decodeMode},{"encode",o.encodeMode},{"quality",o.videoQuality},
        {"speed",o.speed},{"height",o.height},{"fps",o.frameRate},
        {"fpsExact",o.frameRateOverride},{"videoBitDepth",o.videoBitDepth},
        {"bitrate",o.audioBitrate},
        {"rate",o.sampleRate},{"channels",o.channels},{"metadata",o.preserveMetadata},
        {"fallback",o.cpuFallback},{"smartFinalise",o.smart},
        {"videoKbps",o.videoBitrateKbps},{"preserveBitDepth",o.smartPreserveBitDepth}};
}
inline bool decodeOptions(const QJsonObject &o, EncodeOptions &out) {
    out.container = o.value("container").toString();
    out.videoCodec = o.value("video").toString();
    out.audioEncoder = o.value("audio").toString();
    out.videoAction = o.value("videoAction").toString("transcode");
    out.audioAction = o.value("audioAction").toString("transcode");
    // Existing Hive v0.4 clients could express no-audio via encoder=none.
    if (out.audioEncoder == "none") out.audioAction = "remove";
    out.decodeMode = o.value("decode").toString();
    out.encodeMode = o.value("encode").toString();
    if (!validContainer(out.container) ||
        (out.videoAction == "transcode" && !audioOnlyContainer(out.container)
            && !validFamily(out.videoCodec)) ||
        (!out.videoCodec.isEmpty() && !validFamily(out.videoCodec)) ||
        (out.audioAction == "transcode" && !validAudio(out.audioEncoder)) ||
        (!out.audioEncoder.isEmpty() && !validAudio(out.audioEncoder)) ||
        !QStringList{"transcode","copy","remove"}.contains(out.videoAction) ||
        !QStringList{"transcode","copy","remove"}.contains(out.audioAction) ||
        (out.videoAction == "remove" && out.audioAction == "remove") ||
        (audioOnlyContainer(out.container) && out.audioAction == "remove") ||
        (out.audioAction == "transcode" && o.value("channels").toInt() > 0 &&
            ((QStringList{"ac3","eac3","dca"}.contains(out.audioEncoder) && o.value("channels").toInt() > 6) ||
             (out.audioEncoder == "libmp3lame" && o.value("channels").toInt() > 2))) ||
        !QStringList{"auto","cpu","gpu"}.contains(out.decodeMode) ||
        !QStringList{"auto","cpu","gpu"}.contains(out.encodeMode)) return false;
    if (out.videoCodec.isEmpty()) out.videoCodec="h264";
    out.videoQuality = qBound(0,o.value("quality").toInt(2),3);
    out.speed = qBound(0,o.value("speed").toInt(1),2);
    out.height = qBound(0,o.value("height").toInt(),4320);
    out.frameRate = qBound(0,o.value("fps").toInt(),240);
    out.frameRateOverride = o.value("fpsExact").toString();
    if (!validFrameRate(out.frameRateOverride)) return false;
    out.videoBitDepth = o.value("videoBitDepth").toInt(0);
    if (out.videoBitDepth != 0 && out.videoBitDepth != 8 && out.videoBitDepth != 10) return false;
    if (out.videoBitDepth == 10 && out.videoAction == "transcode" &&
        !QStringList{"hevc", "av1", "vp9"}.contains(out.videoCodec)) return false;
    out.audioBitrate = qBound(6,o.value("bitrate").toInt(192),1536);
    out.sampleRate = qBound(0,o.value("rate").toInt(),192000);
    out.channels = qBound(0,o.value("channels").toInt(),8);
    out.preserveMetadata = o.value("metadata").toBool(true);
    out.cpuFallback = o.value("fallback").toBool(true);
    // Only fixed, already-planned Smart values are accepted from a paired host.
    // Do not accept untrusted FFmpeg arguments or arbitrarily large bitrates.
    const int videoKbps = o.value("videoKbps").toInt(0);
    if (videoKbps < 0 || videoKbps > 200000) return false;
    out.videoBitrateKbps = videoKbps;
    out.smart = o.value("smartFinalise").toBool(false);
    out.smartPreserveBitDepth = o.value("preserveBitDepth").toBool(false);
    if (out.smart && out.container != "mkv") return false;
    return true;
}
inline QString cleanSuffix(const QString &name) {
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (QStringList{"mkv","mp4","webm","mov","mp3","m4a","ogg","flac","wav","ac3","eac3","dts","srt","ass","m4v","ts","avi"}.contains(suffix))
        return "." + suffix;
    return ".mkv";
}
inline QString sha256(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()); }
}
