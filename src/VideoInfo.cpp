#include "VideoInfo.h"
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace {
qint64 positiveInteger(const QJsonValue &v) {
    if (v.isDouble()) {
        const double n = v.toDouble();
        return n > 0 && n < 9e18 ? static_cast<qint64>(n) : 0;
    }
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().trimmed().toLongLong(&ok);
        return ok && n > 0 ? n : 0;
    }
    return 0;
}
int validDepth(const QJsonValue &value) {
    const qint64 n = positiveInteger(value);
    return n >= 1 && n <= 32 ? static_cast<int>(n) : 0;
}
int depthFromPixelFormat(const QString &fmt) {
    const QString f = fmt.toLower();
    if (f == "p010le" || f == "p010be" || f == "p210le" || f == "p210be") return 10;
    if (f == "p016le" || f == "p016be" || f == "p216le" || f == "p216be") return 16;
    if (f == "rgb48le" || f == "rgb48be" || f == "bgr48le" || f == "bgr48be" ||
        f == "rgba64le" || f == "rgba64be" || f == "bgra64le" || f == "bgra64be") return 16;
    static const QRegularExpression explicitDepth(R"(^(?:yuv|yuva|yuvj|gbrp|gbrap|gray|xyz|bayer)[a-z0-9_]*?(9|10|12|14|16)(?:le|be)?$)");
    const auto match = explicitDepth.match(f);
    if (match.hasMatch()) return match.captured(1).toInt();
    static const QRegularExpression eightBit(R"(^(?:yuvj?[0-9]+p|yuva[0-9]+p|gbrp|gbrap|gray|gray8|nv12|nv21|rgb24|bgr24|rgba|bgra|argb|abgr|yuyv422|uyvy422|pal8)$)");
    if (eightBit.match(f).hasMatch()) return 8;
    return 0; // Do not guess for unknown or GPU/hardware pixel formats.
}
QString frameRateText(const QString &rate) {
    if (rate.isEmpty()) return "";
    const QStringList parts = rate.split('/');
    bool okN = false, okD = false;
    const double num = parts.value(0).toDouble(&okN);
    const double den = parts.size() == 2 ? parts.value(1).toDouble(&okD) : 1.0;
    if (parts.size() == 1) okD = true;
    if (!okN || !okD || den <= 0 || !std::isfinite(num) || num <= 0) return "";
    const double fps = num / den;
    if (!std::isfinite(fps) || fps <= 0 || fps > 1000) return "";
    QString text = QString::number(fps, 'f', 3);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}
}
void VideoInfo::readMetadata(MediaStream &track, const QJsonObject &stream) {
    if (track.type != "video") return;
    track.codecProfile = stream.value("profile").toString().trimmed();
    track.pixelFormat = stream.value("pix_fmt").toString().trimmed();
    track.frameRate = stream.value("avg_frame_rate").toString().trimmed();
    if (frameRateText(track.frameRate).isEmpty())
        track.frameRate = stream.value("r_frame_rate").toString().trimmed();
    track.bitRate = positiveInteger(stream.value("bit_rate"));
    track.bitRateFromTag = false;
    track.bitRateMeasured = false;
    if (track.bitRate == 0) {
        const QJsonObject tags = stream.value("tags").toObject();
        const QStringList keys = {"BPS", "BPS-eng", "bps", "bps-eng"};
        for (const QString &key : keys) {
            track.bitRate = positiveInteger(tags.value(key));
            if (track.bitRate > 0) { track.bitRateFromTag = true; break; }
        }
    }
    track.bitDepth = validDepth(stream.value("bits_per_raw_sample"));
    if (track.bitDepth == 0) track.bitDepth = validDepth(stream.value("bits_per_sample"));
    if (track.bitDepth == 0) track.bitDepth = depthFromPixelFormat(track.pixelFormat);
}
QString VideoInfo::codecName(const MediaStream &track) {
    const QString codec = track.codec.toLower();
    QString name;
    if (codec == "h264") name = "H.264 / AVC";
    else if (codec == "hevc" || codec == "h265") name = "H.265 / HEVC";
    else if (codec == "av1") name = "AV1";
    else if (codec == "vp9") name = "VP9";
    else if (codec == "vp8") name = "VP8";
    else if (codec == "mpeg4") name = "MPEG-4 Part 2";
    else if (codec == "mpeg2video") name = "MPEG-2";
    else if (codec == "prores") name = "Apple ProRes";
    else if (codec == "ffv1") name = "FFV1";
    else name = track.codec.isEmpty() ? "Unknown" : track.codec.toUpper();
    const QString profile = track.codecProfile.trimmed();
    if (!profile.isEmpty() && profile.compare("unknown", Qt::CaseInsensitive) != 0)
        name += " (" + profile + ")";
    return name;
}
QString VideoInfo::bitrate(const MediaStream &track) {
    if (track.bitRate <= 0) return "Not reported";
    const QString value = track.bitRate >= 1000000
        ? QString::number(track.bitRate / 1000000.0, 'f', 2) + " Mbps"
        : QString::number(track.bitRate / 1000.0, 'f', track.bitRate % 1000 == 0 ? 0 : 1) + " kbps";
    return value + (track.bitRateMeasured ? " (avg)" : track.bitRateFromTag ? " (tag)" : "");
}
QString VideoInfo::bitDepth(const MediaStream &track) {
    return track.bitDepth > 0 ? QString("%1-bit").arg(track.bitDepth) : "Not reported";
}
QString VideoInfo::resolution(const MediaStream &track) {
    QString result = track.width > 0 && track.height > 0
        ? QString("%1 × %2").arg(track.width).arg(track.height) : "Not reported";
    const QString fps = frameRateText(track.frameRate);
    if (!fps.isEmpty()) result += " · " + fps + " fps";
    return result;
}
QString VideoInfo::toolTip(const MediaStream &track) {
    QString detail = QString("Source video stream #%1\nCodec: %2\nBitrate: %3\nBit depth: %4\nResolution: %5")
        .arg(track.index).arg(codecName(track)).arg(bitrate(track)).arg(bitDepth(track)).arg(resolution(track));
    if (track.bitRateMeasured)
        detail += "\nBitrate is measured from compressed packet sizes across this file (average, excluding container overhead).";
    else if (track.bitRateFromTag)
        detail += "\nBitrate comes from an unverified source BPS tag. This tag can be stale after transcoding.";
    if (!track.pixelFormat.isEmpty()) detail += "\nPixel format: " + track.pixelFormat;
    if (!track.title.isEmpty()) detail += "\nTrack name: " + track.title;
    return detail;
}
