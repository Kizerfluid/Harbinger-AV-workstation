#include "../src/AudioInfo.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QDebug>
#include <cstdlib>

static void expect(bool ok, const char *what) {
    if (!ok) { qCritical() << "AudioInfoTests FAILED:" << what; std::exit(1); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MediaStream dts;
    dts.type = "audio";
    dts.index = 2;
    dts.codec = "dts";
    dts.language = "eng";
    dts.channels = 8;
    AudioInfo::readMetadata(dts, QJsonObject{
        {"profile", "DTS-HD MA"}, {"bit_rate", "4500000"},
        {"bits_per_raw_sample", "24"}, {"channel_layout", "7.1"}
    });
    expect(AudioInfo::codecName(dts) == "DTS-HD Master Audio", "DTS-HD profile label");
    expect(AudioInfo::bitrate(dts) == "4.50 Mbps", "stream bitrate");
    expect(AudioInfo::bitDepth(dts) == "24-bit", "raw bitdepth");
    expect(AudioInfo::channels(dts) == "7.1 (8 channels)", "7.1 channel layout");
    expect(AudioInfo::toolTip(dts).contains("Bitrate: 4.50 Mbps"), "tooltip bitrate");

    MediaStream lossy;
    lossy.type = "audio";
    lossy.codec = "eac3";
    lossy.channels = 6;
    AudioInfo::readMetadata(lossy, QJsonObject{{"sample_fmt", "fltp"}, {"bits_per_sample", 0}});
    expect(AudioInfo::bitDepth(lossy) == "Not reported", "do not assume bit depth from decoded sample_fmt");
    expect(AudioInfo::bitrate(lossy) == "Not reported", "missing per-stream bitrate stays unknown");
    expect(AudioInfo::channels(lossy).contains("6 channels (layout not reported)"), "do not assume 6 channels are 5.1");
    expect(!AudioInfo::codecName(lossy).contains("Atmos"), "do not invent Atmos metadata");

    MediaStream pcm;
    pcm.type = "audio";
    pcm.codec = "pcm_s24le";
    pcm.channels = 2;
    AudioInfo::readMetadata(pcm, QJsonObject{
        {"tags", QJsonObject{{"BPS-eng", "2304000"}}},
        {"channel_layout", "stereo"}
    });
    expect(pcm.bitDepth == 24, "PCM bit depth from codec id");
    expect(pcm.bitRate == 2304000 && pcm.bitRateFromTag, "BPS-eng tag fallback labelled");
    expect(AudioInfo::bitrate(pcm) == "2.30 Mbps (tag)", "metadata rate is unverified");
    expect(!dts.bitRateFromTag, "direct audio bitrate not marked as tag");
    pcm.bitRate = 137000; pcm.bitRateFromTag = false; pcm.bitRateMeasured = true;
    expect(AudioInfo::bitrate(pcm) == "137 kbps (avg)", "measured audio average is labelled");
    expect(AudioInfo::toolTip(pcm).contains("Measured average"), "audio measurement provenance shown");
    expect(AudioInfo::channels(pcm) == "stereo (2 channels)", "channel layout from metadata");

    MediaStream atmos;
    atmos.type = "audio";
    atmos.codec = "truehd";
    AudioInfo::readMetadata(atmos, QJsonObject{{"profile", "MLP FBA 16-ch Dolby Atmos"}});
    expect(AudioInfo::codecName(atmos).contains("Atmos"), "show explicitly reported Atmos profile");
    qInfo() << "AudioInfoTests PASSED";
    return 0;
}
