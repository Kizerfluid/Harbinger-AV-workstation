#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

// Independent of GUI and external FFmpeg binaries, so plan construction is testable.
struct MuxTrack {
    QString source;
    int streamIndex = -1; // absolute FFprobe stream index
    QString type;         // video / audio / subtitle
    QString codec;
    QString language = "und";
    QString title;
    bool isDefault = false;
    bool isForced = false;
    double duration = 0.0;
    qint64 delayMs = 0; // Audio track timestamp offset; positive = start later
    bool durationEstimated = false; // Fall back to container duration if individual timing unknown
    double startTime = 0.0; // Existing source stream start PTS; needed for end alignment
};

struct MuxPlan {
    QVector<MuxTrack> tracks; // output order is the order of these tracks
    QString output;          // always a .mkv
    QString chapterSource;   // empty means don't copy chapters
    bool preserveGlobalMetadata = true;
};

namespace Mux {
QString validate(const MuxPlan &plan);
QStringList arguments(const MuxPlan &plan, const QString &temporaryOutput);
QString suggestedOutput(const QString &source);
QString descriptiveTrack(const MuxTrack &track);
} // namespace Mux
