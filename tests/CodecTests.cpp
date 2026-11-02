#include "../src/Codec.h"
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>

static void check(bool success, const char *message) {
    if (!success) qFatal("FAILED: %s", message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto available = Codec::parseEncoderList(
        " V....D libx264      H.264\n"
        " V..... libsvtav1    AV1\n"
        " V....D libvpx-vp9   VP9\n"
        " A..... aac          AAC\n"
        " A..... libopus      Opus\n"
        " A..... libmp3lame   MP3\n"
        " A..... pcm_s16le    PCM\n"
        " A..... pcm_s24le    PCM 24\n"
        " A..... pcm_f32le    PCM float\n"
        " A..... ac3          AC3\n"
        " A..... eac3         EAC3\n"
        " A..X.D dca          DTS\n"
    );
    check(available.contains("libx264") && available.contains("aac"), "encoder discovery");
    check(Codec::videoOptions("webm", available).size() == 2, "webm video options");
    check(Codec::audioOptions("webm", available).size() == 1, "webm audio encoder options");
    check(Codec::audioOptions("mp3", available).size() == 1, "mp3 audio restrictions");
    check(Codec::isAudioOnly("wav"), "wav audio-only");
    check(Codec::isAudioOnly("dts") && Codec::isAudioOnly("ac3") && Codec::isAudioOnly("eac3"), "surround audio-only formats");
    check(Codec::audioOptions("mkv", available).size() >= 5, "mkv offers Dolby DTS and PCM");
    check(Codec::audioOptions("dts", available).size() == 1, "DTS-only container exposes DCA");
    check(Codec::audioOptions("wav", available).size() == 3, "WAV PCM variants");
    check(Codec::audioMaxChannels("dca") == 6 && Codec::audioMaxChannels("ac3") == 6,
          "DTS and AC3 limited to 5.1");
    check(Codec::audioMaxChannels("flac") == 8 && Codec::audioMaxChannels("pcm_s24le") == 8,
          "FLAC and PCM allow 7.1");
    check(Codec::audioBitrates("ac3").contains(640) && Codec::audioBitrates("dca").contains(1536),
          "surround bitrate presets");
    check(!Codec::isAudioOnly("mkv"), "mkv video container");
    const auto opusSpec = Codec::audioBitrateSpec("libopus");
    check(opusSpec.enabled() && opusSpec.minimum == 6 && opusSpec.maximum == 510 &&
          opusSpec.discrete.isEmpty(), "Opus supports fine whole-kbps targets");
    check(Codec::normaliseAudioBitrate("libopus", 137) == 137 &&
          Codec::normaliseAudioBitrate("libopus", 512) == 510,
          "fine Opus rate and upper bound");
    check(Codec::normaliseAudioBitrate("aac", 193) == 193,
          "AAC arbitrary target bitrate");
    check(Codec::normaliseAudioBitrate("ac3", 500) == 512 &&
          Codec::normaliseAudioBitrate("libmp3lame", 159) == 160,
          "fixed-rate encoders snap to valid values");
    check(!Codec::audioBitrateSpec("flac").enabled() &&
          !Codec::audioBitrateSpec("copy").enabled(),
          "lossless and stream-copy disable bitrate controls");
    // Fine-grained values must survive command construction unchanged.
    MediaInfo fineMedia;
    fineMedia.path = "/tmp/audio-source.mkv";
    fineMedia.hasAudio = true;
    EncodeOptions fine;
    fine.container = "mkv";
    fine.videoAction = "remove";
    fine.audioAction = "transcode";
    fine.audioEncoder = "libopus";
    fine.audioBitrate = 137;
    QStringList fineArgs = Codec::buildArgs(fine, fineMedia, "/tmp/audio-137.mkv");
    const int bitratePos = fineArgs.indexOf("-b:a");
    check(bitratePos >= 0 && fineArgs.value(bitratePos + 1) == "137k",
          "custom 137 kbps Opus target passed exactly to FFmpeg");
    fine.audioAction = "copy";
    fineArgs = Codec::buildArgs(fine, fineMedia, "/tmp/audio-copy.mkv");
    check(!fineArgs.contains("-b:a"), "passthrough excludes bitrate");
    fine.audioAction = "transcode";
    fine.audioEncoder = "flac";
    fineArgs = Codec::buildArgs(fine, fineMedia, "/tmp/audio-lossless.mkv");
    check(!fineArgs.contains("-b:a"), "FLAC excludes bitrate");

    MediaInfo media;
    media.path = "/home/user/My Video (test);$name.mp4";
    media.hasVideo = true;
    media.hasAudio = true;
    media.duration = 60.0;
    EncodeOptions options;
    options.container = "mp4";
    options.videoEncoder = "libx264";
    options.audioEncoder = "aac";
    QStringList args = Codec::buildArgs(options, media, "/tmp/output.mp4");
    check(args.contains(media.path), "input passed as a single argument");
    check(args.contains("-n"), "overwrite protection");
    check(!args.contains("-y"), "no destructive overwrite flag");
    check(args.contains("-crf") && args.contains("-c:v"), "quality settings");
    check(args.last() == "/tmp/output.mp4", "output is final positional argument");

    options.container = "ogg";
    options.audioEncoder = "libopus";
    args = Codec::buildArgs(options, media, "/tmp/output.ogg");
    check(args.contains("-vn"), "audio-only discards video");
    check(!args.contains("-c:v"), "audio-only does not encode video");
    check(args.contains("0:a:0"), "audio-only requires audio track");
    // Passthrough never adds encoder, quality, filter, frame rate or pixel format options.
    options.container = "mkv";
    options.videoAction = "copy";
    options.audioAction = "copy";
    options.videoEncoder = "hevc_nvenc";
    options.audioEncoder = "aac";
    options.height = 720;
    options.frameRate = 30;
    options.sampleRate = 48000;
    options.channels = 2;
    args = Codec::buildArgs(options, media, "/tmp/copy.mkv", {"-hwaccel", "cuda"}, {"-gpu", "0"});
    check(args.contains("-c:v") && args.contains("-c:a") && args.count("copy") == 2, "video and audio stream copy");
    for (const auto &forbidden : {"-crf", "-qp", "-vf", "-r", "-ar", "-ac", "-pix_fmt", "-hwaccel", "-gpu"})
        check(!args.contains(forbidden), "copy omits re-encode and GPU settings");
    check(args.contains("-n"), "copy respects no-overwrite rule");

    options.videoAction = "copy";
    options.audioAction = "transcode";
    options.audioEncoder = "aac";
    args = Codec::buildArgs(options, media, "/tmp/audio.mkv");
    check(args.contains("-c:v") && args.contains("-c:a") && args.contains("aac"), "video copy with audio transcode");
    check(!args.contains("-vf") && !args.contains("-r"), "video remains untouched");

    options.videoAction = "transcode";
    options.videoEncoder = "libx264";
    options.audioAction = "copy";
    args = Codec::buildArgs(options, media, "/tmp/video.mkv");
    check(args.contains("-crf") && args.contains("-c:v") && args.contains("libx264"), "video transcode");
    check(args.contains("-c:a") && args.contains("copy") && !args.contains("-ar") && !args.contains("-ac"),
        "audio copy bypasses audio conversion");

    options.videoAction = "remove";
    options.audioAction = "copy";
    args = Codec::buildArgs(options, media, "/tmp/audio-only.mkv");
    check(args.contains("-vn") && !args.contains("-c:v") && args.contains("-c:a"), "remove video");
    options.videoAction = "copy";
    options.audioAction = "remove";
    args = Codec::buildArgs(options, media, "/tmp/video-only.mkv");
    check(args.contains("-an") && !args.contains("-c:a") && args.contains("-c:v"), "remove audio");

    options.container = "flac";
    options.audioAction = "copy";
    args = Codec::buildArgs(options, media, "/tmp/copy.flac");
    check(args.contains("-vn") && args.contains("-c:a") && args.contains("copy"), "audio-only passthrough");
    check(!args.contains("-c:v"), "audio-only cannot encode video");

    // A 7.1 source must downmix automatically for 5.1-only encoders.
    MediaStream surround;
    surround.index = 1;
    surround.type = "audio";
    surround.channels = 8;
    media.streams = {surround};
    media.audioStreamIndex = 1;
    options.container = "mkv";
    options.videoAction = "copy";
    options.audioAction = "transcode";
    options.channels = 0;
    options.audioBitrate = 640;
    options.audioEncoder = "ac3";
    args = Codec::buildArgs(options, media, "/tmp/5.1.mkv");
    check(args.contains("ac3") && args.contains("640k") && args.contains("-ac") && args.contains("6"),
          "AC3 7.1 source downmixed to 5.1");
    options.audioEncoder = "eac3";
    args = Codec::buildArgs(options, media, "/tmp/5.1-plus.mkv");
    check(args.contains("eac3") && args.contains("6"), "EAC3 5.1 constraints");
    options.audioEncoder = "dca";
    options.audioBitrate = 1536;
    args = Codec::buildArgs(options, media, "/tmp/5.1-dts.mkv");
    check(args.contains("dca") && args.contains("-strict") && args.contains("-2") && args.contains("1536k"),
          "DTS Core experimental settings");
    options.audioEncoder = "pcm_s24le";
    options.channels = 8;
    args = Codec::buildArgs(options, media, "/tmp/7.1-pcm.mkv");
    check(args.contains("pcm_s24le") && args.contains("8") && !args.contains("-b:a"),
          "PCM retains 7.1 and does not use bitrate targeting");
    options.audioAction = "copy";
    args = Codec::buildArgs(options, media, "/tmp/passthrough.mkv");
    check(args.contains("-c:a") && args.contains("copy") && !args.contains("-ac") && !args.contains("-b:a"),
          "multichannel passthrough remains untouched");

    // Regression: converting audio in a multi-track MKV must preserve EVERY
    // language track, subtitle stream, attachment and chapter.
    MediaInfo multi;
    multi.path = "/tmp/anime-s01e01.mkv";
    multi.hasVideo = multi.hasAudio = true;
    multi.videoStreamIndex = 0;
    multi.audioStreamIndex = 1;
    multi.hasChapters = true;
    MediaStream v; v.index = 0; v.type = "video"; v.codec = "hevc";
    MediaStream eng; eng.index = 1; eng.type = "audio"; eng.codec = "aac"; eng.channels = 2;
    MediaStream jpn; jpn.index = 2; jpn.type = "audio"; jpn.codec = "opus"; jpn.channels = 2;
    MediaStream sub; sub.index = 3; sub.type = "subtitle"; sub.codec = "ass";
    multi.streams = {v,eng,jpn,sub};
    options.container = "mkv";
    options.videoAction = "copy";
    options.audioAction = "transcode";
    options.audioEncoder = "libopus";
    options.audioBitrate = 128;
    options.sampleRate = 0;
    options.channels = 0;
    args = Codec::buildArgs(options, multi, "/tmp/converted.mkv");
    check(args.contains("0:V?") && args.contains("0:a?") && args.contains("0:s?") && args.contains("0:t?"),
          "mkv preserves every video audio subtitle and attachment stream");
    check(args.contains("-map_chapters") && args.contains("-c:s") && args.contains("-c:t"),
          "mkv preserves chapter and auxiliary tracks by copying");
    check(args.contains("-b:a") && args.contains("128k") && !args.contains("192k"),
          "opus target bitrate is explicitly passed as 128k");
    check(!args.contains("-sn") && !args.contains("-dn"), "mkv subtitles are not suppressed");
    // Re-encoding must remove old stream statistics (while keeping title,
    // language and chapter metadata); otherwise the inspector shows stale BPS.
    check(args.contains("-metadata:s:a") && args.contains("BPS=") &&
          args.contains("NUMBER_OF_BYTES="), "audio transcode clears stale BPS/byte tags");
    check(!args.contains("-metadata:s:v"), "copied video keeps its stream tags");
    options.videoAction = "transcode";
    options.videoEncoder = "libx265";
    options.videoCodec = "hevc";
    const auto bothTranscoded = Codec::buildArgs(options, multi, "/tmp/both-converted.mkv");
    check(bothTranscoded.contains("-metadata:s:v") && bothTranscoded.contains("-metadata:s:a"),
          "video and audio transcode both clear old stream statistics");
    options.videoAction = "copy";
    check(!args.contains("-ac"), "original stereo tracks stay stereo");
    jpn.channels = 8; multi.streams = {v,eng,jpn,sub};
    options.audioEncoder = "ac3";
    options.audioBitrate = 640;
    args = Codec::buildArgs(options, multi, "/tmp/ac3.mkv");
    check(args.contains("-ac:a:1") && !args.contains("-ac:a:0"),
          "original layout downmix only for unsupported multichannel track");

    // v0.6.3: explicit Advanced FPS/bit depth must survive argument construction
    // for both software and GPU outputs, without disturbing stream-copy.
    EncodeOptions advanced;
    advanced.container = "mkv";
    advanced.videoCodec = "hevc";
    advanced.videoAction = "transcode";
    advanced.audioAction = "copy";
    advanced.videoEncoder = "hevc_nvenc";
    advanced.frameRateOverride = "24000/1001";
    advanced.videoBitDepth = 10;
    auto advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/10bit-nvenc.mkv");
    check(advancedArgs.contains("-r") && advancedArgs.contains("24000/1001"),
          "fractional 23.976 fps output is passed exactly to FFmpeg");
    check(advancedArgs.contains("-pix_fmt") && advancedArgs.contains("p010le"),
          "NVENC 10-bit output uses p010le, not incompatible yuv420p10le");
    advanced.videoEncoder = "libx265";
    advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/10bit-x265.mkv");
    check(advancedArgs.contains("yuv420p10le"), "x265 10-bit output uses yuv420p10le");
    advanced.videoBitDepth = 8;
    advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/8bit-x265.mkv");
    check(advancedArgs.contains("yuv420p") && !advancedArgs.contains("yuv420p10le"),
          "8-bit override remains 8-bit even with a 10-bit source");
    advanced.videoBitDepth = 10;
    advanced.videoEncoder = "hevc_vaapi";
    advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/10bit-vaapi.mkv", {}, {}, true);
    check(advancedArgs.contains("format=p010le,hwupload"), "VAAPI 10-bit uses P010 upload");
    advanced.videoEncoder = "hevc_qsv";
    advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/10bit-qsv.mkv");
    check(advancedArgs.contains("p010le"), "QSV 10-bit output uses P010");
    advanced.videoEncoder = "hevc_nvenc";
    advanced.videoAction = "copy";
    advancedArgs = Codec::buildArgs(advanced, multi, "/tmp/copy-video.mkv");
    check(!advancedArgs.contains("-pix_fmt") && !advancedArgs.contains("-r"),
          "bit-depth and FPS overrides never re-encode a copied stream");
    check(Codec::supports10Bit("hevc") && Codec::supports10Bit("vp9") &&
          !Codec::supports10Bit("h264") && !Codec::supports10Bit("mpeg4"),
          "restrict explicit 10-bit to supported encoder families");

    check(Codec::humanDuration(3661) == "01:01:01", "human duration");

    qInfo() << "All codec and argument construction tests passed.";
    return EXIT_SUCCESS;
}
