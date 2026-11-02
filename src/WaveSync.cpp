#include "WaveSync.h"
#include "WaveMatch.h"
#include <QByteArray>
#include <QProcess>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
struct Extracted {
    std::vector<double> fullMix;
    std::vector<double> side;
    QString error;
};

// Decode stereo even for originally mono sources. Left-minus-right tends to
// cancel centered dubbed dialogue, leaving common music and spatial effects.
// This is a heuristic, not source separation; mono/centered effects may vanish.
Extracted extract(const QString &ffmpeg, const MuxTrack &t, double start,
                  double duration, const std::atomic_bool &cancel) {
    Extracted result;
    if (cancel.load()) { result.error = "Cancelled"; return result; }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args{
        "-hide_banner", "-nostdin", "-loglevel", "error",
        "-ss", QString::number(start, 'f', 3), "-i", t.source,
        "-map", QString("0:%1").arg(t.streamIndex),
        "-t", QString::number(duration, 'f', 3),
        "-vn", "-sn", "-dn", "-ac", "2", "-ar", QString::number(WaveMatch::kSampleRate),
        "-c:a", "pcm_f32le", "-f", "f32le", "pipe:1"
    };
    process.start(ffmpeg, args);
    if (!process.waitForStarted(10000)) { result.error = "FFmpeg could not start: " + process.errorString(); return result; }
    int elapsed = 0;
    while (process.state() != QProcess::NotRunning && elapsed < 90000) {
        process.waitForFinished(200);
        elapsed += 200;
        if (cancel.load()) { process.kill(); process.waitForFinished(1000); result.error="Cancelled"; return result; }
    }
    if (process.state()!=QProcess::NotRunning) {
        process.kill(); process.waitForFinished(1000); result.error="Audio analysis timed out"; return result;
    }
    if (process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
        result.error = QString::fromUtf8(process.readAllStandardError()).trimmed().left(350);
        if (result.error.isEmpty()) result.error="FFmpeg could not decode the selected audio stream";
        return result;
    }
    const QByteArray raw = process.readAllStandardOutput();
    constexpr qsizetype kMaxFrames = 8000 * 90;
    const qsizetype frames = std::min<qsizetype>(raw.size()/8, kMaxFrames);
    std::vector<float> mid(static_cast<size_t>(frames));
    std::vector<float> side(static_cast<size_t>(frames));
    double midPower=0.0, sidePower=0.0;
    for (qsizetype i=0;i<frames;++i) {
        float left=0.0f, right=0.0f;
        // Avoid unaligned float access from QByteArray.
        std::memcpy(&left, raw.constData()+i*8, sizeof(float));
        std::memcpy(&right, raw.constData()+i*8+4, sizeof(float));
        if (!std::isfinite(left)) left=0.0f;
        if (!std::isfinite(right)) right=0.0f;
        mid[static_cast<size_t>(i)] = 0.5f*(left+right);
        side[static_cast<size_t>(i)] = 0.5f*(left-right);
        midPower += double(mid[static_cast<size_t>(i)]) * mid[static_cast<size_t>(i)];
        sidePower += double(side[static_cast<size_t>(i)]) * side[static_cast<size_t>(i)];
    }
    result.fullMix=WaveMatch::loudnessFeatures(mid);
    // Skip near-mono/empty stereo difference signals; these contain no useful
    // common background to match and amplify tiny codec noise.
    if (sidePower > 1e-8 * std::max(midPower, 1e-8) &&
        sidePower > 1e-8 * std::max<qsizetype>(frames,1))
        result.side=WaveMatch::loudnessFeatures(side);
    return result;
}

struct Hit {
    WaveMatch::Result result;
    QString method;
    int anchor=0;
};

bool usable(const Hit &hit) {
    return hit.result.found && hit.result.score >= 0.34 && hit.result.distinctiveness >= 0.025;
}
}

namespace WaveSync {
Suggestion analyse(const QString &ffmpeg, const MuxTrack &reference,
                   const MuxTrack &target, const std::atomic_bool &cancel) {
    Suggestion suggestion;
    suggestion.source = target.source;
    suggestion.streamIndex = target.streamIndex;
    if (!(reference.duration > 12 && target.duration > 12)) {
        suggestion.explanation="Tracks need at least 12 seconds of audio for comparison.";
        return suggestion;
    }
    const double shared=std::min(reference.duration,target.duration);
    const double searchRadius=std::min(30.0,std::max(3.0,shared*0.09));
    const double sampleDuration=std::min(18.0,std::max(8.0,shared*0.28));
    // Three opportunities for matching common music/effects in a long dub.
    // Multiple points also help flag timing drift and alternate edits.
    std::vector<double> anchors{std::min(shared*0.20,90.0)};
    if (shared>65) anchors.push_back(shared*0.52);
    if (shared>180) anchors.push_back(shared*0.78);
    std::vector<Hit> hits;
    double topScore=-1.0;
    for (int anchorNumber=0;anchorNumber<static_cast<int>(anchors.size());++anchorNumber) {
        if (cancel.load()) {suggestion.explanation="Cancelled";return suggestion;}
        const double anchor=std::clamp(anchors[anchorNumber],0.0,
                                        std::max(0.0,reference.duration-sampleDuration));
        const double candidateStart=std::max(0.0,anchor-searchRadius);
        const double candidateEnd=std::min(target.duration,anchor+sampleDuration+searchRadius);
        if (candidateEnd-candidateStart < sampleDuration) continue;
        const auto ref=extract(ffmpeg,reference,anchor,sampleDuration,cancel);
        if (!ref.error.isEmpty()) {suggestion.explanation=ref.error;return suggestion;}
        const auto other=extract(ffmpeg,target,candidateStart,candidateEnd-candidateStart,cancel);
        if (!other.error.isEmpty()) {suggestion.explanation=other.error;return suggestion;}
        auto compare=[&](const std::vector<double> &r, const std::vector<double> &o, const QString &method) {
            if (r.empty() || o.empty()) return;
            const auto match=WaveMatch::correlate(r,anchor,o,candidateStart);
            if (!match.found) return;
            topScore=std::max(topScore,match.score);
            Hit h{match,method,anchorNumber};
            if (usable(h)) hits.push_back(h);
        };
        // Prefer common stereo ambience and music to dialogue-dominated mono.
        compare(ref.side,other.side,"stereo background");
        compare(ref.fullMix,other.fullMix,"full mix");
    }

    // Use a consistent match at two separate points rather than trusting the
    // highest correlation in one potentially repetitive music/dialogue passage.
    double bestWeight=-1.0;
    Hit bestA{},bestB{};
    bool paired=false;
    for (size_t i=0;i<hits.size();++i) {
        for (size_t j=i+1;j<hits.size();++j) {
            if (hits[i].anchor==hits[j].anchor) continue;
            if (std::abs(hits[i].result.offsetSeconds-hits[j].result.offsetSeconds)>0.20) continue;
            const double weight=hits[i].result.score+hits[j].result.score+
                                std::min(hits[i].result.distinctiveness,0.3)+
                                std::min(hits[j].result.distinctiveness,0.3);
            if (weight>bestWeight) {bestWeight=weight;bestA=hits[i];bestB=hits[j];paired=true;}
        }
    }
    if (!paired && anchors.size()>1) {
        suggestion.explanation=QString("No consistent cross-language match across the sampled points "
              "(best waveform correlation %1). Different dialogue mixes, edits or timing drift "
              "can prevent reliable matching. Try an opening/music cue or a manual offset.")
              .arg(std::max(0.0,topScore),0,'f',2);
        return suggestion;
    }
    // For a short clip with one checkpoint, require a strong, distinct peak.
    Hit winner{};
    if (!paired) {
        for (const auto &h:hits)
            if (h.result.score>=0.60 && h.result.distinctiveness>=0.07 &&
                h.result.score>winner.result.score) winner=h;
        if (!winner.result.found) {
            suggestion.explanation="No clear matching soundtrack events found in the short excerpt.";
            return suggestion;
        }
    }
    const double lag=paired ? (bestA.result.offsetSeconds+bestB.result.offsetSeconds)/2.0
                            : winner.result.offsetSeconds;
    suggestion.correlation=paired ? bestA.result.score : winner.result.score;
    suggestion.secondCorrelation=paired ? bestB.result.score : 0.0;
    // Warn instead of applying a delay when another strong checkpoint disagrees.
    for (const auto &h:hits) {
        if ((paired && (h.anchor==bestA.anchor || h.anchor==bestB.anchor)) ||
            (!paired && h.anchor==winner.anchor)) continue;
        if (h.result.score>=0.65 && h.result.distinctiveness>=0.07 &&
            std::abs(h.result.offsetSeconds-lag)>0.30) {
            suggestion.explanation="Strong matches disagree across the timeline. The releases may have different cuts or audio drift; one constant offset is unsafe.";
            return suggestion;
        }
    }
    const double proposed=reference.delayMs/1000.0 + reference.startTime-target.startTime+lag;
    if (!std::isfinite(proposed) || std::abs(proposed)>43200.0) {
        suggestion.explanation="Calculated delay exceeds the supported range.";
        return suggestion;
    }
    suggestion.delayMs=static_cast<qint64>(std::llround(proposed*1000.0));
    suggestion.reliable=true;
    suggestion.explanation=QString("Matched %1 sampled positions using %2; proposed %3 ms. Verify dialogue sync before using the output.")
        .arg(paired?2:1).arg(paired ? bestA.method + " + " + bestB.method : winner.method)
        .arg(suggestion.delayMs);
    return suggestion;
}
}
