#include "AudioInfo.h"
#include <QJsonValue>
#include <QRegularExpression>

namespace {
qint64 positiveInteger(const QJsonValue &v) {
    if (v.isDouble()) {
        const double n = v.toDouble();
        return n > 0 && n < 9e18 ? static_cast<qint64>(n) : 0;
    }
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().trimmed().toLongLong(&ok);
        if (ok && n > 0) return n;
    }
    return 0;
}
int safeBitDepth(const QJsonValue &v) {
    const qint64 n = positiveInteger(v);
    return n >= 1 && n <= 64 ? static_cast<int>(n) : 0;
}
QString prettyLanguage(const QString &lang) {
    return lang.isEmpty() || lang == "und" ? "Not reported" : lang;
}
}

void AudioInfo::readMetadata(MediaStream &track, const QJsonObject &stream) {
    if (track.type != "audio") return;
    track.codecProfile = stream.value("profile").toString().trimmed();
    track.bitRate = positiveInteger(stream.value("bit_rate"));
    track.bitRateFromTag = false;
    track.bitRateMeasured = false;
    const QJsonObject tags = stream.value("tags").toObject();
    // Matroska can store per-track bitrates as BPS / BPS-eng tags when
    // FFprobe's stream.bit_rate is not populated. Never use format.bit_rate;
    // that describes the whole file, not a specific audio stream.
    if (track.bitRate == 0) {
        const QStringList names = {"BPS", "BPS-eng", "bps", "bps-eng"};
        for (const QString &name : names) {
            track.bitRate = positiveInteger(tags.value(name));
            if (track.bitRate > 0) { track.bitRateFromTag = true; break; }
        }
    }
    track.bitDepth = safeBitDepth(stream.value("bits_per_raw_sample"));
    if (track.bitDepth == 0) track.bitDepth = safeBitDepth(stream.value("bits_per_sample"));
    // FFprobe may omit bit depth for raw PCM even though it is explicitly
    // encoded in the PCM codec ID. Do NOT use sample_fmt for compressed audio:
    // it describes the decoder's sample representation, not source precision.
    if (track.bitDepth == 0 && track.codec.startsWith("pcm_")) {
        static const QRegularExpression pcmDepth(R"(^pcm_[suf](8|16|24|32|64)(?:le|be)?$)");
        const auto match = pcmDepth.match(track.codec);
        if (match.hasMatch()) track.bitDepth = match.captured(1).toInt();
    }
    track.channelLayout = stream.value("channel_layout").toString().trimmed();
    if (track.channelLayout.isEmpty()) {
        // Newer FFprobe builds may expose layout through ch_layout.
        track.channelLayout = stream.value("ch_layout").toObject().value("layout").toString().trimmed();
    }
    if (track.channels == 0)
        track.channels = stream.value("ch_layout").toObject().value("nb_channels").toInt();
}

QString AudioInfo::codecName(const MediaStream &track) {
    const QString codec = track.codec.toLower();
    const QString profile = track.codecProfile.trimmed();
    if (codec == "dts") {
        if (profile.contains("DTS:X", Qt::CaseInsensitive)) return "DTS:X (" + profile + ")";
        if (profile.contains("DTS-HD MA", Qt::CaseInsensitive) || profile.contains("Master Audio", Qt::CaseInsensitive))
            return "DTS-HD Master Audio";
        if (profile.contains("DTS-HD HRA", Qt::CaseInsensitive) || profile.contains("High Resolution", Qt::CaseInsensitive))
            return "DTS-HD High Resolution";
        return profile.isEmpty() || profile == "unknown" ? "DTS" : "DTS (" + profile + ")";
    }
    QString name;
    if (codec == "ac3") name = "Dolby Digital (AC-3)";
    else if (codec == "eac3") name = "Dolby Digital Plus (E-AC-3)";
    else if (codec == "truehd") name = "Dolby TrueHD";
    else if (codec == "aac") name = "AAC";
    else if (codec == "flac") name = "FLAC";
    else if (codec == "opus") name = "Opus";
    else if (codec == "mp3") name = "MP3";
    else if (codec.startsWith("pcm_")) name = "PCM";
    else name = track.codec.isEmpty() ? "Unknown" : track.codec.toUpper();
    // Only identify Atmos or object extensions when FFprobe explicitly reports
    // them; regular E-AC-3/TrueHD metadata is not evidence of Atmos.
    if (!profile.isEmpty() && profile.compare("unknown", Qt::CaseInsensitive) != 0 &&
        (profile.contains("Atmos", Qt::CaseInsensitive) || profile.contains("DTS:X", Qt::CaseInsensitive)))
        return name + " (" + profile + ")";
    return name;
}

QString AudioInfo::bitrate(const MediaStream &track) {
    if (track.bitRate <= 0) return "Not reported";
    const QString value = track.bitRate >= 1000000
        ? QString::number(track.bitRate / 1000000.0, 'f', 2) + " Mbps"
        : QString::number(track.bitRate / 1000.0, 'f', track.bitRate % 1000 == 0 ? 0 : 1) + " kbps";
    return value + (track.bitRateMeasured ? " (avg)" : track.bitRateFromTag ? " (tag)" : "");
}
QString AudioInfo::bitDepth(const MediaStream &track) {
    return track.bitDepth > 0 ? QString("%1-bit").arg(track.bitDepth) : "Not reported";
}
QString AudioInfo::channels(const MediaStream &track) {
    if (track.channels <= 0 && track.channelLayout.isEmpty()) return "Not reported";
    const QString label = track.channelLayout;
    if (!label.isEmpty())
        return track.channels > 0 ? QString("%1 (%2 channels)").arg(label).arg(track.channels) : label;
    if (track.channels == 1) return "Mono (1 channel)";
    if (track.channels == 2) return "Stereo (2 channels)";
    // Six or eight channels aren't necessarily 5.1 or 7.1. Report exact
    // counts if FFprobe did not supply a channel layout.
    return QString("%1 channels (layout not reported)").arg(track.channels);
}
QString AudioInfo::toolTip(const MediaStream &track) {
    return QString("Source audio track #%1\nCodec: %2\nBitrate: %3%8\nBit depth: %4\nChannels: %5\nLanguage: %6%7")
        .arg(track.index)
        .arg(codecName(track))
        .arg(bitrate(track))
        .arg(bitDepth(track))
        .arg(channels(track))
        .arg(prettyLanguage(track.language))
        .arg(track.title.isEmpty() ? QString{} : "\nTrack name: " + track.title)
        .arg(track.bitRateMeasured
            ? "\nMeasured average from compressed packets (excludes container overhead)."
            : track.bitRateFromTag
            ? "\nSource BPS tag is unverified and may have been copied from an older encode."
            : QString{});
}
