#pragma once
#include "MuxPlan.h"
#include "Types.h"
#include <QStringList>
#include <QVector>
#include <QMap>

struct BatchSourceGroup {
    QString samplePath;
    QStringList files;  // One file per episode for this role
};
struct BatchEpisode {
    int number = -1;
    QStringList files; // In group order
    QString issue;
    MuxPlan plan;
    QString syncReport;
};
namespace BatchMux {
int episodeNumber(const QString &filename);
QVector<BatchEpisode> pairEpisodes(const QVector<BatchSourceGroup> &groups, bool orderedFallback, QString *problem);
// Never guess which of several seemingly similar audio streams was intended.
MuxPlan makePlan(const MuxPlan &sample, const QVector<BatchSourceGroup> &groups,
                 const BatchEpisode &episode, const QMap<QString,MediaInfo> &probes,
                 const QString &outputDirectory, QString *problem);
MediaInfo inspect(const QString &ffprobe, const QString &path);
}
