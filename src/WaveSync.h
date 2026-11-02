#pragma once
#include "MuxPlan.h"
#include <QString>
#include <QVector>
#include <atomic>

namespace WaveSync {
struct Suggestion {
    QString source;
    int streamIndex = -1;
    bool reliable = false;
    qint64 delayMs = 0;
    double correlation = 0;
    double secondCorrelation = 0;
    QString explanation;
};
// Blocking: call only from a background thread. Samples are always bounded
// and decoded to temporary memory; final muxing still copies original packets.
Suggestion analyse(const QString &ffmpeg, const MuxTrack &reference,
                   const MuxTrack &target, const std::atomic_bool &cancel);
}
