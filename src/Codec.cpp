#include "Codec.h"
#include <QtGlobal>
#include <QRegularExpression>
#include <QMap>
#include <cmath>

namespace {
using Entry = CodecEntry;
const QList<Entry> videos {
    {"H.264 / AVC", "h264"},
    {"H.265 / HEVC", "hevc"},
    {"AV1", "av1"},
    {"VP9", "vp9"},
    {"MPEG-4 Part 2", "mpeg4"},
};
const QMap<QString, QStringList> implementations {
    {"h264", {"libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_vaapi"}},
    {"hevc", {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_vaapi"}},
    {"av1", {"libsvtav1", "libaom-av1", "av1_nvenc", "av1_qsv", "av1_amf", "av1_vaapi"}},
    {"vp9", {"libvpx-vp9", "vp9_qsv", "vp9_vaapi"}},
    {"mpeg4", {"mpeg4"}},
};
const QList<Entry> audios {
    {"AAC · Compatible", "aac"},
    {"MP3 (LAME)", "libmp3lame"},
    {"Opus · Modern", "libopus"},
    {"Vorbis", "libvorbis"},
    {"FLAC · Lossless", "flac"},
    {"ALAC · Lossless", "alac"},
    {"Dolby Digital (AC-3) · up to 5.1", "ac3"},
    {"Dolby Digital Plus (E-AC-3) · up to 5.1", "eac3"},
    {"DTS Core (DCA) · up to 5.1 · experimental", "dca"},
    {"PCM 16-bit · WAV / lossless", "pcm_s16le"},
    {"PCM 24-bit · WAV / lossless", "pcm_s24le"},
    {"PCM float 32-bit · WAV / lossless", "pcm_f32le"},
};
QStringList allowedVideo(const QString &container) {
    if (container == "mp4") return {"h264", "hevc", "av1", "mpeg4"};
    if (container == "webm") return {"vp9", "av1"};
    if (container == "mov") return {"h264", "hevc", "mpeg4"};
    return {"h264", "hevc", "av1", "vp9", "mpeg4"};
}
QStringList allowedAudio(const QString &container) {
    if (container == "mp4" || container == "mov") return {"aac", "libmp3lame", "alac", "ac3", "eac3"};
    if (container == "webm") return {"libopus", "libvorbis"};
    if (container == "mkv") return {"aac", "libmp3lame", "libopus", "libvorbis", "flac", "alac", "ac3", "eac3", "dca", "pcm_s16le", "pcm_s24le", "pcm_f32le"};
    if (container == "mp3") return {"libmp3lame"};
    if (container == "m4a") return {"aac", "alac"};
    if (container == "ogg") return {"libopus", "libvorbis", "flac"};
    if (container == "flac") return {"flac"};
    if (container == "wav") return {"pcm_s16le", "pcm_s24le", "pcm_f32le"};
    if (container == "ac3") return {"ac3"};
    if (container == "eac3") return {"eac3"};
    if (container == "dts") return {"dca"};
    return {};
}
int crf(const QString &encoder, int quality) {
    quality = qBound(0, quality, 3);
    if (encoder == "libx264") { const int q[] = {17, 20, 23, 28}; return q[quality]; }
    if (encoder == "libx265") { const int q[] = {20, 24, 28, 32}; return q[quality]; }
    if (encoder == "libsvtav1") { const int q[] = {22, 27, 33, 39}; return q[quality]; }
    if (encoder == "libvpx-vp9") { const int q[] = {19, 25, 32, 39}; return q[quality]; }
    if (encoder == "mpeg4") { const int q[] = {2, 4, 6, 9}; return q[quality]; }
    const int q[] = {17, 20, 24, 29};
    return q[quality];
}
}

namespace Codec {
QStringList containers() { return {"mp4", "mkv", "webm", "mov", "mp3", "m4a", "ogg", "flac", "wav", "ac3", "eac3", "dts"}; }
QString extension(const QString &container) { return container; }
bool isAudioOnly(const QString &container) { return !QStringList{"mp4", "mkv", "webm", "mov"}.contains(container); }
bool supports10Bit(const QString &videoCodec) {
    // H.264 High10 is not universally available (not on Turing NVENC);
    // MPEG-4 Part 2 has no 10-bit output in our supported encoder set.
    return QStringList{"hevc", "av1", "vp9"}.contains(videoCodec);
}

QList<CodecEntry> videoOptions(const QString &container, const QSet<QString> &available) {
    QList<CodecEntry> result;
    for (const auto &e : videos) {
        if (!allowedVideo(container).contains(e.encoder)) continue;
        for (const auto &impl : implementations.value(e.encoder)) {
            if (available.contains(impl)) { result.append(e); break; }
        }
    }
    return result;
}
QList<CodecEntry> audioOptions(const QString &container, const QSet<QString> &available) {
    QList<CodecEntry> result;
    for (const auto &e : audios) if (allowedAudio(container).contains(e.encoder) && available.contains(e.encoder)) result.append(e);
    return result;
}

QSet<QString> parseEncoderList(const QString &output) {
    QSet<QString> result;
    // Each encoder row starts with a 6-character capability flag sequence.
    const QRegularExpression lineRe(R"(^\s*[VASD\.][F\.][S\.][X\.][B\.][D\.]\s+([^\s]+))");
    for (const QString &line : output.split('\n')) {
        auto m = lineRe.match(line);
        if (m.hasMatch()) result.insert(m.captured(1));
    }
    return result;
}

int audioMaxChannels(const QString &encoder) {
    // Encoder-specific limits; these are not container limits. In particular,
    // FFmpeg's ordinary E-AC-3 encoder does not create 7.1/Atmos bitstreams.
    if (encoder == "ac3" || encoder == "eac3" || encoder == "dca") return 6;
    if (encoder == "libmp3lame") return 2;
    return 8;
}

QList<int> audioBitrates(const QString &encoder) {
    if (encoder == "ac3") return {192, 256, 320, 384, 448, 512, 576, 640};
    if (encoder == "eac3") return {192, 256, 320, 384, 448, 512, 640, 768, 1024};
    if (encoder == "dca") return {768, 1024, 1411, 1536};
    if (encoder == "libmp3lame") return {96, 128, 160, 192, 256, 320};
    if (encoder == "libopus") return {96, 128, 160, 192, 256, 320, 384, 448, 510};
    if (encoder == "aac" || encoder == "libvorbis") return {96, 128, 160, 192, 256, 320, 384, 448, 512, 640};
    return {}; // Lossless formats have no user-controlled target bitrate.
}

AudioBitrateSpec audioBitrateSpec(const QString &encoder) {
    if (encoder == "libopus") return {6, 510, {}};
    if (encoder == "aac" || encoder == "libvorbis") return {32, 640, {}};
    if (encoder == "libmp3lame")
        return {32, 320, {32,40,48,56,64,80,96,112,128,160,192,224,256,320}};
    if (encoder == "ac3")
        return {32, 640, {32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640}};
    if (encoder == "eac3") {
        QList<int> values;
        for (int b = 64; b <= 1024; b += 32) values.append(b);
        return {64, 1024, values};
    }
    if (encoder == "dca") return {768, 1536, {768,1024,1411,1536}};
    return {}; // Lossless PCM / FLAC / ALAC and stream-copy have no target bitrate.
}

int normaliseAudioBitrate(const QString &encoder, int requestedKbps) {
    const auto spec = audioBitrateSpec(encoder);
    if (!spec.enabled()) return requestedKbps;
    const int bounded = qBound(spec.minimum, requestedKbps, spec.maximum);
    if (spec.discrete.isEmpty()) return bounded;
    int best = spec.discrete.first();
    for (int value : spec.discrete)
        if (qAbs(value - bounded) < qAbs(best - bounded)) best = value;
    return best;
}

QStringList audioEncodeArgs(const EncodeOptions &s, const MediaInfo &input) {
    QStringList a {"-c:a", s.audioEncoder};
    if (s.audioEncoder == "dca") {
        // FFmpeg's native DTS Core encoder is marked experimental on some builds.
        a << "-strict" << "-2";
    }
    if (!audioBitrates(s.audioEncoder).isEmpty())
        a << "-b:a" << QString::number(s.audioBitrate) + "k";
    if (s.audioEncoder == "flac") a << "-compression_level" << "5";
    if (s.sampleRate > 0) a << "-ar" << QString::number(s.sampleRate);
    const int limit = audioMaxChannels(s.audioEncoder);
    if (s.channels > 0) {
        // An explicit channel choice applies to every converted audio stream.
        a << "-ac" << QString::number(qMin(s.channels, limit));
    } else if (s.container == "mkv" && !input.streams.isEmpty()) {
        // Multiple audio streams may have different layouts. With Original,
        // constrain ONLY streams the encoder cannot handle, avoiding an
        // accidental stereo -> 5.1 upmix of the other language tracks.
        int audioOrdinal = 0;
        for (const auto &track : input.streams) {
            if (track.type != "audio") continue;
            if (track.channels > limit)
                a << QString("-ac:a:%1").arg(audioOrdinal) << QString::number(limit);
            ++audioOrdinal;
        }
    } else if (s.channels == 0) {
        // Existing single-stream behaviour for non-MKV containers.
        for (const auto &track : input.streams) {
            if (track.type == "audio" && (input.audioStreamIndex < 0 || track.index == input.audioStreamIndex)) {
                if (track.channels > limit) a << "-ac" << QString::number(limit);
                break;
            }
        }
    }
    return a;
}

QStringList buildArgs(const EncodeOptions &s, const MediaInfo &input, const QString &output,
                      const QStringList &preInput, const QStringList &extraEncoder, bool vaapiUpload) {
    QStringList a {"-hide_banner", "-nostdin", "-n", "-loglevel", "warning", "-stats_period", "0.25", "-progress", "pipe:1"};
    // Only active when transcoding video. Copy mode must never initialise a GPU decoder.
    const bool audioOnly = isAudioOnly(s.container);
    const bool videoEnabled = !audioOnly && s.videoAction != "remove";
    const bool videoTranscode = videoEnabled && s.videoAction == "transcode";
    const bool audioEnabled = s.audioAction != "remove" && s.audioEncoder != "none";
    const bool audioTranscode = audioEnabled && s.audioAction == "transcode";
    // Matroska is the archival workflow: never silently reduce a multi-track
    // MKV to one video and one audio stream. Optional maps also tolerate inputs
    // that have only audio or only subtitles. Uppercase V excludes cover art.
    const bool preserveMkvTracks = s.container == "mkv";
    a << (videoTranscode ? preInput : QStringList{}) << "-i" << input.path;
    if (videoEnabled) {
        a << "-map" << (preserveMkvTracks ? QString("0:V?") :
            input.videoStreamIndex >= 0 ? "0:" + QString::number(input.videoStreamIndex) : QString("0:v:0"));
        if (videoTranscode) {
            a << "-c:v" << s.videoEncoder;
            const int q = crf(s.videoEncoder, s.videoQuality);
            const bool targetSize = s.videoBitrateKbps > 0;
            if (targetSize) a << "-b:v" << QString::number(s.videoBitrateKbps) + "k";
            // Size-target jobs use average-bitrate rate control instead of CRF/
            // constant-QP. FFmpeg still cannot guarantee an exact output size.
            if (s.videoEncoder == "libx264" || s.videoEncoder == "libx265") {
                if (!targetSize) a << "-crf" << QString::number(q);
                a << "-preset" << (s.speed == 0 ? "veryfast" : s.speed == 2 ? "slow" : "medium");
            } else if (s.videoEncoder == "libsvtav1") {
                if (!targetSize) a << "-crf" << QString::number(q);
                a << "-preset" << (s.speed == 0 ? "10" : s.speed == 2 ? "5" : "8");
            } else if (s.videoEncoder == "libvpx-vp9") {
                if (!targetSize) a << "-b:v" << "0" << "-crf" << QString::number(q);
                a << "-deadline" << "good"
                  << "-cpu-used" << (s.speed == 0 ? "5" : s.speed == 2 ? "1" : "3");
            } else if (s.videoEncoder == "mpeg4") {
                if (!targetSize) a << "-q:v" << QString::number(q);
            } else if (s.videoEncoder.endsWith("_nvenc")) {
                if (targetSize) a << "-rc" << "vbr";
                else a << "-rc" << "constqp" << "-qp" << QString::number(q);
                a << "-preset" << (s.speed == 0 ? "p1" : s.speed == 2 ? "p6" : "p4");
            }
            QStringList filters;
            if (s.height > 0) filters << QString("scale=-2:%1:flags=lanczos").arg(s.height);
            bool source10Bit = false;
            for (const MediaStream &v : input.streams)
                if (v.type == "video" && v.bitDepth >= 10) { source10Bit = true; break; }
            // Keep Smart's established source-depth behaviour unchanged; only
            // explicit Advanced 10-bit opens the VP9 high-bit-depth path.
            const bool smart10BitCapable = s.videoCodec == "hevc" || s.videoCodec == "av1" ||
                s.videoEncoder == "libx265" || s.videoEncoder == "libsvtav1" ||
                s.videoEncoder == "libaom-av1";
            const bool use10Bit = s.videoBitDepth == 10 ||
                (s.videoBitDepth == 0 && s.smartPreserveBitDepth && source10Bit &&
                 smart10BitCapable);
            // Video bit depth is an *encoding* choice. Never alter copied streams.
            if (vaapiUpload) filters << (use10Bit ? "format=p010le" : "format=nv12") << "hwupload";
            if (!filters.isEmpty()) a << "-vf" << filters.join(',');
            const QString fps = s.frameRateOverride.isEmpty()
                ? (s.frameRate > 0 ? QString::number(s.frameRate) : QString{})
                : s.frameRateOverride;
            if (!fps.isEmpty()) a << "-r" << fps;
            if (s.videoEncoder == "libaom-av1" && !targetSize)
                a << "-crf" << QString::number(q) << "-b:v" << "0";
            if (s.videoEncoder.endsWith("_qsv") || s.videoEncoder.endsWith("_amf")) {
                a << "-pix_fmt" << (use10Bit ? "p010le" : "nv12");
            } else if (!vaapiUpload && s.videoEncoder != "mpeg4") {
                a << "-pix_fmt" << (use10Bit
                    ? (s.videoEncoder.endsWith("_nvenc") ? "p010le" : "yuv420p10le")
                    : "yuv420p");
            }
            a << extraEncoder;
        } else {
            a << "-c:v" << "copy";
        }
    } else a << "-vn";

    if (audioEnabled) {
        a << "-map" << (preserveMkvTracks ? QString("0:a?") :
            input.audioStreamIndex >= 0 ? "0:" + QString::number(input.audioStreamIndex)
            : (videoEnabled ? QString("0:a:0?") : QString("0:a:0")));
        if (audioTranscode) {
            a << audioEncodeArgs(s, input);
        } else a << "-c:a" << "copy";
    } else a << "-an";

    if (preserveMkvTracks) {
        // Matroska subtitles (ASS, SRT, PGS, etc.) and attached fonts are
        // copied byte-for-byte, irrespective of the audio/video encode mode.
        // FFmpeg copies per-stream language/title/disposition with the mapping.
        a << "-map" << "0:s?" << "-map" << "0:t?"
          << "-c:s" << "copy" << "-c:t" << "copy"
          << "-map_chapters" << "0";
        // Arbitrary data streams are not mapped here; Matroska's muxer does
        // not support every data format. Never claim these are preserved.
    } else {
        // Existing compact-container behaviour: subtitle and data muxing is
        // container/codec-dependent and is not silently converted here.
        a << "-sn" << "-dn";
    }
    if (s.preserveMetadata) a << "-map_metadata" << "0";
    else a << "-map_metadata" << "-1";
    // Input MKVs often carry BPS, NUMBER_OF_BYTES and NUMBER_OF_FRAMES tags
    // created for their ORIGINAL bitstreams. FFmpeg normally copies these
    // metadata fields even after transcoding. Then FFprobe may report no
    // stream.bit_rate and our inspector can accidentally show the OLD values.
    // Remove only obsolete statistics for streams actually re-encoded; keep
    // language, title, disposition, chapters, fonts, subtitle and other tags.
    if (preserveMkvTracks) {
        static const QStringList statsTags = {
            "BPS", "BPS-eng", "NUMBER_OF_BYTES", "NUMBER_OF_BYTES-eng",
            "NUMBER_OF_FRAMES", "NUMBER_OF_FRAMES-eng",
            "DURATION", "DURATION-eng"
        };
        for (const QString &key : statsTags) {
            if (videoTranscode) a << "-metadata:s:v" << key + "=";
            if (audioTranscode) a << "-metadata:s:a" << key + "=";
        }
    }
    if (s.container == "mp4" || s.container == "mov" || s.container == "m4a") a << "-movflags" << "+faststart";
    a << output;
    return a;
}

QString humanDuration(double s) {
    if (!(s > 0.0) || !std::isfinite(s)) return "Unknown";
    const qint64 seconds = static_cast<qint64>(s);
    return QString("%1:%2:%3").arg(seconds / 3600, 2, 10, QChar('0'))
        .arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
QString defaultPreset(const QString &container) {
    if (container == "mp4") return "h264";
    if (container == "webm") return "vp9";
    if (container == "mkv") return "hevc";
    return "h264";
}
} // namespace Codec
