#include "../src/MuxPlan.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <cstdlib>

static void check(bool ok, const char *message) { if (!ok) qFatal("FAILED: %s", message); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    check(dir.isValid(), "temp directory");
    const QString a = dir.filePath("Source A (special chars);$test.mkv");
    const QString b = dir.filePath("Source B.mkv");
    const QString externalSub = dir.filePath("captions.srt");
    for (const auto &path : {a, b, externalSub}) {
        QFile f(path);
        check(f.open(QFile::WriteOnly), "create test input");
        f.write("test");
    }
    MuxPlan plan;
    plan.output = dir.filePath("new output.mkv");
    plan.tracks.append({a, 0, "video", "h264", "und", "", true, false, 40.0});
    plan.tracks.append({b, 3, "audio", "aac", "eng", "English", true, false, 40.0});
    plan.tracks.append({a, 5, "audio", "ac3", "jpn", "Japanese", false, false, 40.0});
    plan.tracks.append({externalSub, 0, "subtitle", "subrip", "eng", "CC", false, true, 40.0});
    plan.chapterSource = a;
    check(Mux::validate(plan).isEmpty(), "valid plan");
    const QString outTemp = dir.filePath("new output.harbinger-part.mkv");
    const QStringList args = Mux::arguments(plan, outTemp);
    check(args.indexOf("-n") >= 0 && args.indexOf("-c") >= 0 && args.indexOf("copy") >= 0, "stream-copy and no overwrite");
    check(args.count("-i") == 3 && args.contains(a) && args.contains(b) && args.contains(externalSub), "all sources loaded once");
    check(args.count("-map") == 4, "exactly four tracks");
    check(args.contains("0:0") && args.contains("1:3") && args.contains("0:5") && args.contains("2:0"), "uses absolute source stream IDs");
    check(args.contains("-disposition:2") && args.contains("0") && args.contains("forced"), "explicit flags");
    check(args.contains("-map_chapters") && args.contains("0"), "chapter mapping");
    check(args.last() == outTemp, "correct temp output");

    MuxPlan invalid = plan;
    invalid.tracks.append(invalid.tracks.first());
    check(!Mux::validate(invalid).isEmpty(), "duplicate track rejected");
    invalid = plan;
    invalid.tracks.append({b, 0, "video", "hevc", "und", "", false, false, 40.0});
    check(!Mux::validate(invalid).isEmpty(), "second video rejected");
    invalid = plan;
    invalid.tracks[2].isDefault = true;
    check(!Mux::validate(invalid).isEmpty(), "two default audio tracks rejected");
    invalid = plan;
    invalid.output = a;
    check(!Mux::validate(invalid).isEmpty(), "input cannot be overwritten");
    invalid = plan;
    invalid.tracks[1].language = "English";
    check(!Mux::validate(invalid).isEmpty(), "invalid language code rejected");
    invalid = plan;
    invalid.tracks[3].codec = "mov_text";
    const QStringList textArgs = Mux::arguments(invalid, outTemp);
    check(textArgs.contains("-c:3") && textArgs.contains("srt"), "mov_text conversion only for that subtitle");
    // Stream copy with per-track input timestamp shifting. A second audio track
    // from the same input must get its own input handle, not shift the video.
    MuxPlan shifted = plan;
    shifted.tracks[1].delayMs = 4000;  // separate input b with offset
    shifted.tracks[2].delayMs = 250;   // a with offset, while video a is unshifted
    const auto shiftedArgs = Mux::arguments(shifted, outTemp);
    check(Mux::validate(shifted).isEmpty(), "positive audio delays accepted");
    check(shiftedArgs.count("-itsoffset") == 2, "two distinct per-track offsets emitted");
    check(shiftedArgs.count("-i") == 4, "same source input duplicated only when delayed");
    check(shiftedArgs.contains("4.000") && shiftedArgs.contains("0.250"), "millisecond delays have precise seconds");
    check(shiftedArgs.contains("0:0") && shiftedArgs.contains("1:3") && shiftedArgs.contains("2:5") && shiftedArgs.contains("3:0"),
          "source indices re-mapped correctly after duplicated inputs");
    check(shiftedArgs.indexOf("-map_chapters") >= 0 && shiftedArgs.at(shiftedArgs.indexOf("-map_chapters") + 1) == "0",
          "chapters still mapped to original unshifted source");
    shifted.tracks[2].delayMs = -120;
    check(Mux::arguments(shifted, outTemp).contains("-0.120"), "negative manual delay supported");
    shifted.tracks[0].delayMs = 1000;
    check(!Mux::validate(shifted).isEmpty(), "no accidental video shift");
    shifted.tracks[0].delayMs = 0;
    shifted.tracks[1].delayMs = 50000000;
    check(!Mux::validate(shifted).isEmpty(), "out-of-range delay rejected");

    qInfo() << "All mux plan tests passed.";
    return EXIT_SUCCESS;
}
