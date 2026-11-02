#include "../src/Hardware.h"
#include "../src/Codec.h"
#include <QCoreApplication>
#include <QDebug>

static void check(bool condition, const char *description) {
    if (!condition) qFatal("FAILED: %s", description);
}
static bool orderedBefore(const QStringList &list, const QString &first, const QString &later) {
    return list.indexOf(first) >= 0 && list.indexOf(later) > list.indexOf(first);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Hardware::Report report;
    report.encoders = {"libx264", "libx265", "libsvtav1", "libvpx-vp9", "mpeg4", "h264_nvenc", "hevc_nvenc"};
    Hardware::Device gpu;
    gpu.id = "nvenc:1"; gpu.vendor = "nvidia"; gpu.backend = "nvenc";
    gpu.locator = "1"; gpu.name = "Fake NVIDIA GPU"; gpu.selectable = true;
    Hardware::Capability h264;
    h264.encode = Hardware::State::Verified; h264.encoder = "h264_nvenc";
    h264.decode = Hardware::State::Verified; h264.decodeBackend = "cuda";
    gpu.codecs.insert("h264", h264);
    Hardware::Capability hevc;
    hevc.encode = Hardware::State::Listed; hevc.encoder = "hevc_nvenc";
    gpu.codecs.insert("hevc", hevc);
    report.devices.append(gpu);
    MediaInfo media;
    media.path = "/tmp/with space;special.mkv";
    media.videoCodec = "h264"; media.hasVideo = true; media.hasAudio = true;
    EncodeOptions opts;
    opts.container = "mkv"; opts.videoCodec = "h264";
    opts.videoQuality = 2; opts.encodeMode = "auto";
    auto result = Hardware::select(opts, media, report);
    check(result.ok && result.hardwareEncode && result.encoder == "h264_nvenc", "auto balanced uses verified GPU");
    check(result.hardwareDecode && result.decodeBackend == "cuda", "auto decoding uses verified GPU");
    check(result.locator == "1", "correct NVENC device index");
    const auto before = Hardware::inputArgs(result);
    check(orderedBefore(before, "-hwaccel", "cuda"), "HW decode inputs present");
    auto args = Codec::buildArgs(EncodeOptions{opts}, media, "/tmp/out.mkv", before, Hardware::outputArgs(result));
    check(args.contains("-gpu") && args.contains("1"), "NVENC GPU selector generated");
    check(args.indexOf("-hwaccel") < args.indexOf("-i"), "input HW options before input");
    opts.videoQuality = 0;
    result = Hardware::select(opts, media, report);
    check(result.ok && !result.hardwareEncode && result.encoder == "libx264", "auto quality mode uses CPU");
    opts.encodeMode = "gpu";
    result = Hardware::select(opts, media, report);
    check(result.ok && result.hardwareEncode, "explicit GPU overrides quality");
    opts.encodeMode = "cpu"; opts.decodeMode = "cpu";
    result = Hardware::select(opts, media, report);
    check(result.ok && !result.hardwareEncode && !result.hardwareDecode, "forced CPU for both directions");
    opts.videoCodec = "hevc"; opts.encodeMode = "gpu";
    result = Hardware::select(opts, media, report);
    check(!result.ok, "listed but unverified encoder not accepted as verified");
    opts.encodeMode = "auto";
    result = Hardware::select(opts, media, report);
    check(result.ok && result.encoder == "libx265", "unverified GPU falls back to CPU in auto mode");
    opts.videoCodec = "h264"; opts.gpuId = "nvenc:999";
    result = Hardware::select(opts, media, report);
    check(!result.ok, "missing device fails clearly");
    opts.gpuId.clear();
    opts.decodeMode = "gpu";
    media.videoCodec = "av1";
    result = Hardware::select(opts, media, report);
    check(!result.ok, "forced decode requires tested source decoder");
    opts.container = "mp3"; opts.audioEncoder = "libmp3lame";
    result = Hardware::select(opts, media, report);
    check(result.ok, "audio-only encode ignores video hardware modes");
    // Check VAAPI device initialization and upload filter syntax.
    Hardware::Choice vaapi;
    vaapi.hardwareEncode = true; vaapi.encoderBackend = "vaapi";
    vaapi.encoder = "h264_vaapi"; vaapi.locator = "/dev/dri/renderD129";
    const auto vaapiArgs = Codec::buildArgs(EncodeOptions{}, media, "/tmp/out.mkv",
        {"-init_hw_device", "vaapi=harb:/dev/dri/renderD129", "-filter_hw_device", "harb"}, {}, true);
    check(vaapiArgs.contains("format=nv12,hwupload"), "VAAPI frames upload filter");
    qInfo() << "Hardware selection and argument tests passed.";
    return 0;
}
