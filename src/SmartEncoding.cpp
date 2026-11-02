#include "SmartEncoding.h"
#include "Codec.h"
#include <QFileInfo>
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace {
int countType(const MediaInfo &source, const QString &type) {
    int count = 0;
    for (const MediaStream &s : source.streams) if (s.type == type) ++count;
    return count;
}

double sourceFps(const MediaInfo &source) {
    for (const MediaStream &s : source.streams) {
        if (s.type != "video") continue;
        const QStringList parts = s.frameRate.split('/');
        bool ok = false;
        double rate = parts.value(0).toDouble(&ok);
        if (!ok) continue;
        if (parts.size() == 2) {
            const double den = parts[1].toDouble(&ok);
            if (!ok || den <= 0) continue;
            rate /= den;
        }
        if (rate > 0 && std::isfinite(rate)) return rate;
    }
    return 25.0;
}

int desiredAudioKbps(const MediaStream &track, const QString &encoder, int priority) {
    const int channels = qMax(1, track.channels);
    int kbps = (priority == 0 ? 96 : priority == 2 ? 192 : 128);
    if (channels == 1) kbps = priority == 2 ? 112 : 64;
    else if (channels > 2) kbps = qBound(160, kbps * channels / 2, 640);
    if (encoder == "aac" || encoder == "libmp3lame") kbps = static_cast<int>(kbps * 1.25);
    // Compressing a 96k source into a 320k destination cannot restore its detail.
    // Don't waste a scarce bitrate budget on such gratuitous increases.
    if (track.bitRate > 0 && track.codec != "flac" && track.codec != "alac" &&
        !track.codec.startsWith("pcm_"))
        kbps = qMin(kbps, qMax(48, static_cast<int>((track.bitRate + 999) / 1000)));
    return Codec::normaliseAudioBitrate(encoder, kbps);
}

int audioRateWithinBudget(const QString &encoder, int desired, int maximum) {
    const auto spec = Codec::audioBitrateSpec(encoder);
    if (!spec.enabled()) return 0;
    const int upper = qMin(desired, maximum);
    if (upper < spec.minimum) return 0;
    if (spec.discrete.isEmpty()) return qMin(spec.maximum, upper);
    int candidate = 0;
    for (int rate : spec.discrete) if (rate <= upper) candidate = qMax(candidate, rate);
    return candidate;
}

int safeAudioKbps(const MediaStream &track) {
    // Stream-copy bitrates must be known for an honest size calculation.
    return track.bitRate > 0 ? static_cast<int>(qMin<qint64>(track.bitRate / 1000, 100000)) : 0;
}
}

namespace SmartEncoding {
Plan makePlan(const EncodeOptions &request, const MediaInfo &source) {
    Plan r;
    r.options = request;
    auto &s = r.options;
    s.smart = false; // resolved output is ordinary Codec / Hardware input
    s.container = "mkv"; // lossless stream / subtitle / attachment preservation
    s.speed = qBound(0, request.speed, 2);
    s.videoQuality = request.smartPriority == 0 ? 1 : request.smartPriority == 2 ? 2 : 2;
    s.height = 0;
    s.frameRate = 0;
    s.frameRateOverride.clear();
    s.videoBitDepth = 0; // Smart is source-aware; ignore Advanced-only bit-depth overrides.
    s.sampleRate = 0;
    s.channels = 0;
    s.preserveMetadata = true;
    s.videoBitrateKbps = 0;
    s.smartPreserveBitDepth = true;
    s.cpuFallback = true;
    s.gpuId.clear();
    s.decodeMode = "auto";
    s.encodeMode = s.speed == 2 ? "cpu" : "auto";
    s.videoAction = source.hasVideo ? "transcode" : "remove";
    s.audioAction = source.hasAudio ? "transcode" : "remove";
    if (!source.hasVideo && !source.hasAudio) {
        r.error = "No video or audio streams were detected.";
        return r;
    }
    if (source.hasVideo) {
        s.videoCodec = request.smartVideo == "auto" ? request.videoCodec : request.smartVideo;
        if (s.videoCodec.isEmpty()) {
            r.error = "No video encoder was selected or detected.";
            return r;
        }
        if (s.videoCodec == "copy") s.videoAction = "copy";
    }
    if (source.hasAudio) {
        s.audioEncoder = request.smartAudio == "auto" ? request.audioEncoder : request.smartAudio;
        if (s.audioEncoder.isEmpty()) {
            r.error = "No compatible audio encoder was selected or detected.";
            return r;
        }
        if (s.audioEncoder == "copy") s.audioAction = "copy";
    }
    if (s.videoAction == "copy") s.videoCodec = "hevc"; // unused placeholder
    if (s.audioAction == "copy") s.audioEncoder = "aac"; // unused placeholder

    const double duration = source.duration;
    const int aCount = countType(source, "audio");
    // Never silently omit streams to make a desired target fit.
    int desiredRate = 0;
    int copiedAudioRate = 0;
    int totalAudioRate = 0;
    for (const MediaStream &track : source.streams) {
        if (track.type != "audio") continue;
        if (s.audioAction == "copy") {
            const int b = safeAudioKbps(track);
            if (b == 0 && request.smartTargetMB > 0) {
                r.error = "Cannot estimate file size with audio passthrough: a source audio bitrate is unknown. "
                          "Use an audio encoder or Advanced Encoding.";
                return r;
            }
            copiedAudioRate += b;
        } else if (s.audioAction == "transcode") {
            desiredRate = qMax(desiredRate, desiredAudioKbps(track, s.audioEncoder, request.smartPriority));
        }
    }
    if (s.audioAction == "transcode") {
        if (!Codec::audioBitrateSpec(s.audioEncoder).enabled() && request.smartTargetMB > 0) {
            r.error = "A fixed file-size target cannot be reliably allocated to a lossless audio encoder. "
                      "Choose AAC or Opus, or remove the size target.";
            return r;
        }
        s.audioBitrate = desiredRate > 0 ? desiredRate : 128;
        totalAudioRate = s.audioBitrate * qMax(1, aCount);
    } else if (s.audioAction == "copy") totalAudioRate = copiedAudioRate;

    if (request.smartTargetMB < 0 || !std::isfinite(request.smartTargetMB)) {
        r.error = "Invalid file size target.";
        return r;
    }
    if (request.smartTargetMB > 0) {
        if (!(duration >= 0.5) || !std::isfinite(duration)) {
            r.error = "A duration could not be measured. Target-size mode needs a known source duration.";
            return r;
        }
        if (countType(source, "video") > 1) {
            r.error = "This input has multiple video streams. A single file-size budget for all video "
                      "streams is not supported; use Advanced Encoding or remove the size target.";
            return r;
        }
        if (s.videoAction == "copy") {
            r.error = "The output size cannot be controlled while video is set to Passthrough. "
                      "Choose a video codec or disable the size target.";
            return r;
        }
        const double totalKbps = request.smartTargetMB * 8000.0 / duration;
        // Reserve a margin for Matroska overhead, fonts, subtitles, metadata, chapters.
        // The final size is still approximate: external attachment sizes can be large.
        const double overheadKbps = qMax(12.0, totalKbps * 0.035 + countType(source, "subtitle") * 6.0);
        const double availableKbps = totalKbps - overheadKbps - copiedAudioRate;
        if (availableKbps < 96) {
            r.error = "The file-size target is too small to retain the selected audio and other streams.";
            return r;
        }
        if (s.audioAction == "transcode") {
            const int minimumPerAudio = qMax(32, s.audioEncoder == "libopus" ? 48 : 64);
            if (s.videoAction == "transcode") {
                // Preference influences the audio/video split, but reserves a real
                // lower bound for video instead of squeezing video to zero bitrate.
                const double reserveVideo = qMax(160.0, availableKbps * (request.smartPriority == 2 ? 0.40 : 0.55));
                const int maxPerAudio = static_cast<int>(qFloor((availableKbps - reserveVideo) / qMax(1, aCount)));
                if (maxPerAudio < minimumPerAudio) {
                    r.error = "Not enough bitrate remains for the audio tracks and a usable video stream. "
                              "Increase the target file size.";
                    return r;
                }
                s.audioBitrate = audioRateWithinBudget(s.audioEncoder, s.audioBitrate, maxPerAudio);
                if (s.audioBitrate == 0) {
                    r.error = "The selected audio codec has no bitrate compatible with this target size.";
                    return r;
                }
            } else {
                s.audioBitrate = audioRateWithinBudget(s.audioEncoder, s.audioBitrate,
                    static_cast<int>(availableKbps / qMax(1, aCount)));
                if (s.audioBitrate == 0) {
                    r.error = "The selected audio codec cannot fit the target size.";
                    return r;
                }
            }
            totalAudioRate = s.audioBitrate * qMax(1, aCount);
        }
        if (s.videoAction == "transcode") {
            const double videoKbps = totalKbps - overheadKbps - totalAudioRate;
            if (!(videoKbps >= 160.0) || videoKbps > 1000000) {
                r.error = "The requested size cannot leave enough bitrate for the video and retained tracks.";
                return r;
            }
            s.videoBitrateKbps = static_cast<int>(qRound(videoKbps));
            // For very small targets, prefer a lower output resolution over
            // allocating implausibly few bits per pixel per frame.
            const double fps = sourceFps(source);
            int width = source.width, height = source.height;
            if (width <= 0 || height <= 0) {
                for (const MediaStream &v : source.streams) if (v.type == "video") {
                    width = v.width; height = v.height; break;
                }
            }
            const double minBpp = s.videoCodec == "h264" ? 0.025 : 0.018;
            for (int h : {2160, 1440, 1080, 720, 480, 360}) {
                if (height <= h) continue; // never upscale
                const double bpp = videoKbps * 1000.0 / (qMax(1, width) * qMax(1, height) * fps);
                if (bpp >= minBpp) break;
                s.height = h;
                width = qMax(2, static_cast<int>(qRound(width * static_cast<double>(h) / height)));
                height = h;
            }
        }
        r.estimatedSizeMB = request.smartTargetMB;
        r.explanation = QString("%1 target · %2 kbps video · %3 kbps audio per track · %4. "
            "Final size may differ because of codec rate control and Matroska overhead.")
            .arg(QString::number(request.smartTargetMB, 'f', 1))
            .arg(s.videoAction == "transcode" ? QString::number(s.videoBitrateKbps) : "copied/none")
            .arg(s.audioAction == "transcode" ? QString::number(s.audioBitrate) : "copied/none")
            .arg(s.height ? QString("downscale to %1p").arg(s.height) : "original resolution");
    } else {
        const QString audioDescription = s.audioAction == "copy" ? "passthrough" :
            s.audioAction == "remove" ? "not present" :
            Codec::audioBitrateSpec(s.audioEncoder).enabled() ?
                QString::number(s.audioBitrate) + " kbps per track" : "lossless";
        r.explanation = QString("Quality-controlled encoding · original resolution · %1 audio%2. "
            "Output size depends on the material, not a fixed bitrate budget.")
            .arg(audioDescription)
            .arg(s.speed == 2 ? " · CPU quality priority" : "");
    }
    if (source.hasVideo && s.videoAction == "transcode") {
        bool input10Bit = false;
        for (const auto &v : source.streams)
            if (v.type == "video" && v.bitDepth >= 10) input10Bit = true;
        if (input10Bit && s.videoCodec != "hevc" && s.videoCodec != "av1")
            r.explanation += " Note: the selected video codec currently uses 8-bit output in Harbinger.";
    }
    r.ok = true;
    return r;
}
}
