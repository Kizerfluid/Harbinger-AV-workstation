#include "HiveProtocol.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <cstdlib>
static void check(bool success) { if (!success) qFatal("Hive protocol regression test failed"); }

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto f=HiveProtocol::frame({{"type","hello"},{"version",HiveProtocol::Version}});
    check(f.size()>4);
    QByteArray data;
    QList<QJsonObject> received;
    QString err;
    data.append(f.left(3));
    check(HiveProtocol::consume(data,received,err));
    check(received.empty());
    data.append(f.mid(3));
    check(HiveProtocol::consume(data,received,err));
    check(received.size()==1 && received[0].value("type").toString()=="hello");
    received.clear();
    data=QByteArray(4,'\x7f');
    check(!HiveProtocol::consume(data,received,err));
    EncodeOptions opts;
    opts.container="mkv";opts.videoCodec="hevc";opts.audioEncoder="aac";
    opts.encodeMode="gpu";opts.decodeMode="cpu";opts.height=720;
    EncodeOptions output;
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.container==opts.container && output.videoCodec==opts.videoCodec);
    check(output.encodeMode=="gpu" && output.decodeMode=="cpu" && output.height==720);
    opts.smart=true;opts.videoBitrateKbps=1554;opts.smartPreserveBitDepth=true;
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.smart && output.videoBitrateKbps==1554 && output.smartPreserveBitDepth);
    opts.smart=false;opts.videoBitrateKbps=0;opts.smartPreserveBitDepth=false;
    opts.videoAction="copy"; opts.audioAction="transcode";
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts), output));
    check(output.videoAction=="copy" && output.audioAction=="transcode");
    opts.videoAction="transcode"; opts.audioAction="copy";
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts), output));
    check(output.videoAction=="transcode" && output.audioAction=="copy");
    opts.videoAction="remove"; opts.audioAction="remove";
    check(!HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts), output));
    opts.videoAction="copy"; opts.audioAction="transcode";
    QJsonObject malicious=HiveProtocol::encodeOptions(opts);
    malicious["audio"]="-i /private/secret";
    check(!HiveProtocol::decodeOptions(malicious,output));
    opts.container="mkv";
    opts.videoAction="copy";
    opts.audioAction="transcode";
    opts.audioEncoder="dca";
    opts.audioBitrate=1536;
    opts.channels=6;
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.audioEncoder=="dca" && output.channels==6 && output.audioBitrate==1536);
    opts.channels=8;
    check(!HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    opts.audioEncoder="flac";
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.channels==8);
    opts.container="dts";
    opts.audioEncoder="dca";
    opts.channels=6;
    opts.videoAction="remove";
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.container=="dts");
    malicious=HiveProtocol::encodeOptions(opts);
    malicious["videoKbps"] = 9999999;
    check(!HiveProtocol::decodeOptions(malicious,output));
    opts.container="mkv";opts.videoCodec="hevc";opts.videoAction="transcode";
    opts.audioAction="copy";opts.audioEncoder="libopus";opts.channels=0;
    opts.videoBitDepth=10;opts.frameRateOverride="24000/1001";
    check(HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    check(output.videoBitDepth==10 && output.frameRateOverride=="24000/1001");
    malicious=HiveProtocol::encodeOptions(opts);
    malicious["fpsExact"]="-i private.mp4";
    check(!HiveProtocol::decodeOptions(malicious,output));
    malicious=HiveProtocol::encodeOptions(opts);
    malicious["fpsExact"]="24000/0";
    check(!HiveProtocol::decodeOptions(malicious,output));
    malicious=HiveProtocol::encodeOptions(opts);
    malicious["videoBitDepth"]=12;
    check(!HiveProtocol::decodeOptions(malicious,output));
    opts.videoCodec="h264";
    check(!HiveProtocol::decodeOptions(HiveProtocol::encodeOptions(opts),output));
    qInfo()<<"Hive v5 protocol frame, Advanced 8/10-bit/FPS and validation: PASSED";
}
