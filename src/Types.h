#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QSet>

// FFprobe stream index is absolute within its input, not the per-type ordinal.
struct MediaStream {
    int index = -1;
    QString type;          // video, audio, subtitle
    QString codec;
    QString language = "und";
    QString title;
    int width = 0;
    int height = 0;
    int channels = 0;
    QString channelLayout; // FFprobe channel_layout, e.g. 5.1(side); empty if unavailable
    QString codecProfile;  // Video/audio profile; only claim extensions explicitly reported
    QString pixelFormat;   // Video pixel format (e.g. yuv420p10le)
    QString frameRate;     // Video average frame rate as a ratio, if known
    qint64 bitRate = 0;   // Bits/sec for this track; 0 = not reported
    bool bitRateFromTag = false; // BPS metadata is unverified, potentially stale after transcoding
    bool bitRateMeasured = false; // Actual packet-size average, not an encoder target
    int bitDepth = 0;    // 0 = not reported; do not infer from compressed sample_fmt
    double duration = 0.0;
    bool durationKnown = false; // true only for per-stream metadata, not container estimates
    double startTime = 0.0; // FFprobe stream start_time, in seconds
    bool isDefault = false;
    bool isForced = false;
};

struct MediaInfo {
    QString path;
    QString videoCodec;
    QString audioCodec;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;
    QString error;
    QList<MediaStream> streams;
    bool hasChapters = false;
};

struct EncodeOptions {
    QString container = "mp4";
    QString videoCodec = "h264";      // UI codec family (h264/hevc/av1/vp9/mpeg4)
    QString videoEncoder = "libx264"; // resolved FFmpeg implementation
    QString decodeMode = "auto";      // auto / gpu / cpu
    QString encodeMode = "auto";      // auto / gpu / cpu
    QString gpuId;                   // empty = first matching GPU
    bool cpuFallback = true;
    // These are independent stream operations. "copy" means FFmpeg stream copy,
    // without decoding/encoding; "remove" discards that media stream.
    QString videoAction = "transcode";  // transcode / copy / remove
    QString audioAction = "transcode";  // transcode / copy / remove
    QString audioEncoder = "aac";
    int videoQuality = 2;    // 0 = best, 1 = high, 2 = balanced, 3 = compact
    int speed = 1;           // 0 = faster, 1 = balanced, 2 = slower
    int height = 0;          // 0 = original
    int frameRate = 0;       // 0 = original; legacy whole-number FPS
    QString frameRateOverride; // Advanced: exact FFmpeg rate, e.g. 24000/1001
    int videoBitDepth = 0;   // 0 = previous automatic behaviour; 8 or 10 = explicit output
    int audioBitrate = 192;  // kbps
    int sampleRate = 0;      // 0 = source
    int channels = 0;        // 0 = source
    bool preserveMetadata = true;
    // Smart UI values. Hive transmits a resolved per-source Smart plan and
    // uses smart=true only to request lossless MKV finalisation on workers.
    bool smart = false;
    double smartTargetMB = 0.0; // 0 = quality target rather than size target
    int smartPriority = 1;      // 0 video, 1 balanced, 2 audio
    QString smartVideo = "auto";
    QString smartAudio = "auto";
    // Planned video average bitrate (kbps) for a target-size job.
    // 0 = quality-controlled / Advanced Encoder path.
    int videoBitrateKbps = 0;
    bool smartPreserveBitDepth = false; // only Smart attempts to keep 10-bit sources
};

enum class JobState { Probing, Ready, Queued, Encoding, Completed, Failed, Cancelled };

struct Job {
    QString input;
    QString output;
    MediaInfo info;
    JobState state = JobState::Probing;
    int progress = 0;
    QString error;
    // Empty = follow global option; otherwise per-job override.
    QString decodeOverride;
    QString encodeOverride;
    QString gpuOverride; // "default" = auto device; empty = global setting
};

struct CodecEntry {
    QString name;
    QString encoder;
};
