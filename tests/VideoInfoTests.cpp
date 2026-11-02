#include "../src/VideoInfo.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QDebug>
#include <cstdlib>

static void expect(bool ok, const char *what) {
    if (!ok) { qCritical() << "VideoInfoTests FAILED:" << what; std::exit(1); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MediaStream h264;
    h264.index = 0; h264.type = "video"; h264.codec = "h264";
    h264.width = 1920; h264.height = 1080;
    VideoInfo::readMetadata(h264, QJsonObject{
        {"profile", "High"}, {"pix_fmt", "yuv420p"},
        {"avg_frame_rate", "30000/1001"}, {"bit_rate", "5200000"}
    });
    expect(VideoInfo::codecName(h264) == "H.264 / AVC (High)", "H.264 profile");
    expect(VideoInfo::bitrate(h264) == "5.20 Mbps", "H.264 per-stream bitrate");
    expect(VideoInfo::bitDepth(h264) == "8-bit", "8-bit YUV pixel format");
    expect(VideoInfo::resolution(h264).contains("29.97 fps"), "rational average frame rate");
    expect(VideoInfo::resolution(h264).contains("1920"), "resolution");
    MediaStream hevc;
    hevc.index = 2; hevc.type = "video"; hevc.codec = "hevc";
    hevc.width = 3840; hevc.height = 2160;
    VideoInfo::readMetadata(hevc, QJsonObject{
        {"profile", "Main 10"}, {"pix_fmt", "yuv420p10le"},
        {"avg_frame_rate", "24000/1001"}, {"tags", QJsonObject{{"BPS-eng", "8700000"}}}
    });
    expect(VideoInfo::codecName(hevc) == "H.265 / HEVC (Main 10)", "HEVC profile");
    expect(VideoInfo::bitDepth(hevc) == "10-bit", "10-bit HEVC pixel format");
    expect(VideoInfo::bitrate(hevc) == "8.70 Mbps (tag)", "unverified stream bitrate tag is labelled");
    expect(hevc.bitRateFromTag, "BPS tag provenance retained");
    hevc.bitRate = 1612345; hevc.bitRateFromTag = false; hevc.bitRateMeasured = true;
    expect(VideoInfo::bitrate(hevc) == "1.61 Mbps (avg)", "measured packet-average bitrate distinguished from metadata");
    expect(VideoInfo::toolTip(hevc).contains("compressed packet sizes"), "packet average provenance explained");
    expect(!h264.bitRateFromTag, "direct stream bitrate is not labelled as tag");
    expect(VideoInfo::resolution(hevc).contains("23.976 fps"), "23.976 fps");
    MediaStream unknown;
    unknown.type = "video"; unknown.codec = "av1";
    VideoInfo::readMetadata(unknown, QJsonObject{{"pix_fmt", "unknown"}, {"avg_frame_rate", "0/0"}});
    expect(VideoInfo::bitDepth(unknown) == "Not reported", "unknown depth is not guessed");
    expect(VideoInfo::bitrate(unknown) == "Not reported", "unknown stream bitrate is not container bitrate");
    expect(VideoInfo::resolution(unknown) == "Not reported", "unknown dimensions/frame rate");
    MediaStream p010; p010.type = "video";
    VideoInfo::readMetadata(p010, QJsonObject{{"pix_fmt", "p010le"}});
    expect(VideoInfo::bitDepth(p010) == "10-bit", "p010 pixel format");
    qInfo() << "VideoInfoTests PASSED";
    return 0;
}
