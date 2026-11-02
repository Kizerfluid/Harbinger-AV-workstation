#include "../src/SmartEncoding.h"
#include "../src/Codec.h"
#include <QCoreApplication>
#include <QDebug>

static void ensure(bool condition, const char *name) {
    if (!condition) qFatal("FAILED: %s", name);
}
static MediaInfo fakeEpisode() {
    MediaInfo m;
    m.path = "episode.mkv";
    m.duration = 24 * 60.0;
    m.hasVideo = true; m.width = 1920; m.height = 1080;
    m.hasAudio = true;
    MediaStream v; v.type = "video"; v.codec = "hevc"; v.width = 1920; v.height = 1080;
    v.bitDepth = 10; v.frameRate = "24000/1001";
    m.streams << v;
    MediaStream eng; eng.type = "audio"; eng.codec = "aac"; eng.channels = 2; eng.bitRate = 192000;
    m.streams << eng;
    MediaStream jp = eng; jp.codec = "opus"; jp.bitRate = 160000;
    m.streams << jp;
    MediaStream sub; sub.type = "subtitle"; sub.codec = "ass";
    m.streams << sub << sub;
    return m;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    EncodeOptions smart;
    smart.smart = true;
    smart.container = "mkv";
    smart.smartVideo = "auto";
    smart.smartAudio = "auto";
    smart.videoCodec = "hevc";
    smart.audioEncoder = "libopus";
    smart.smartPriority = 1;
    smart.speed = 1;
    smart.smartTargetMB = 350.0;
    const auto m = fakeEpisode();
    auto balanced = SmartEncoding::makePlan(smart, m);
    ensure(balanced.ok && balanced.options.videoBitrateKbps > 0, "target produces bitrate plan");
    ensure(balanced.options.audioEncoder == "libopus" && balanced.options.container == "mkv", "source streams use MKV with Opus");
    ensure(balanced.options.channels == 0 && balanced.options.frameRate == 0, "keep source layout and FPS");
    const auto args = Codec::buildArgs(balanced.options, m, "out.mkv");
    ensure(args.contains("0:s?") && args.contains("0:t?"), "retain subtitles attachments");
    ensure(args.contains("-b:v") && !args.contains("-crf"), "target bitrate not CRF");
    ensure(args.contains("-b:a") && args.contains(QString::number(balanced.options.audioBitrate) + "k"), "audio setting carried to encoder");
    ensure(args.contains("yuv420p10le"), "10bit input retained in HEVC encode");
    smart.smartPriority = 0;
    const auto visual = SmartEncoding::makePlan(smart, m);
    smart.smartPriority = 2;
    const auto audio = SmartEncoding::makePlan(smart, m);
    ensure(visual.ok && audio.ok, "priority plans usable");
    ensure(visual.options.videoBitrateKbps > audio.options.videoBitrateKbps, "visual priority gives more bits to video");
    smart.smartTargetMB = 10.0;
    ensure(!SmartEncoding::makePlan(smart, m).ok, "impossible target rejected");
    smart.smartTargetMB = 0;
    auto untargeted = SmartEncoding::makePlan(smart, m);
    ensure(untargeted.ok && untargeted.options.videoBitrateKbps == 0, "quality mode chooses quality control");
    smart.smartTargetMB = 350;
    smart.smartVideo = "copy";
    ensure(!SmartEncoding::makePlan(smart, m).ok, "video copy cannot promise size");
    smart.smartVideo = "auto";
    smart.smartAudio = "copy";
    auto copied = SmartEncoding::makePlan(smart, m);
    ensure(copied.ok && copied.options.audioAction == "copy" && copied.options.videoBitrateKbps > 0,
           "known-bitrate audio can be copied with planned video");
    smart.smartAudio = "flac";
    ensure(!SmartEncoding::makePlan(smart,m).ok, "lossless audio forbidden for strict size target");
    smart.smartAudio = "auto";
    MediaInfo unknown = m; unknown.duration = 0;
    ensure(!SmartEncoding::makePlan(smart, unknown).ok, "reject unknown duration");
    MediaInfo audioOnly = m; audioOnly.hasVideo = false;
    audioOnly.streams.removeFirst();
    ensure(SmartEncoding::makePlan(smart, audioOnly).ok, "smart audio-only source");
    qInfo() << "Smart encoding planner tests passed";
}
