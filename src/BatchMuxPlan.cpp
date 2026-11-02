#include "BatchMuxPlan.h"
#include "AudioInfo.h"
#include "VideoInfo.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace {
QString norm(const QString &path) { return QFileInfo(path).absoluteFilePath(); }
QString lang(QString s) { s = s.toLower().trimmed(); return s.isEmpty() ? QStringLiteral("und") : s; }
QString two(int n) { return QString("%1").arg(n, 2, 10, QChar('0')); }
QStringList sortedUnique(QStringList v) {
    for (QString &s : v) s = norm(s);
    v.removeDuplicates();
    std::sort(v.begin(), v.end(), [](const QString &a,const QString &b) {return QString::compare(a,b,Qt::CaseInsensitive)<0;});
    return v;
}
double durationTag(const QString &str) {
    const auto p=str.split(':'); if (p.size()!=3) return 0;
    bool a=false,b=false,c=false;
    const double h=p[0].toDouble(&a),m=p[1].toDouble(&b),s=p[2].toDouble(&c);
    return a&&b&&c ? h*3600+m*60+s : 0;
}
}
namespace BatchMux {
int episodeNumber(const QString &filename) {
    const QString base=QFileInfo(filename).completeBaseName();
    // Prefer unambiguous explicit markers over random numbers in quality/release tags.
    static const QList<QRegularExpression> strong {
        QRegularExpression(R"((?:^|[^\p{L}\p{N}])s\d{1,2}[ ._-]*e(?:p)?[ ._-]*(\d{1,3})(?=$|[^\p{L}\p{N}]))",QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"((?:^|[^\p{L}\p{N}])(?:episode|ep)[ ._#-]*(\d{1,3})(?=$|[^\p{L}\p{N}]))",QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"((?:^|[ _.-])[-–][ _.-]*(\d{1,3})(?=$|[ _.\[(]))"),
    };
    for (const auto &rx:strong) {const auto m=rx.match(base); if(m.hasMatch()) return m.captured(1).toInt();}
    // Use a single bracketed/isolated 1-3 digit token only when unambiguous.
    static const QRegularExpression token(R"((?:^|[ _\.\[(])([0-9]{1,3})(?=$|[ _\.\])]))");
    auto it=token.globalMatch(base); int number=-1;
    while(it.hasNext()) {
        const auto m=it.next(); const int n=m.captured(1).toInt();
        if(n==0 || n>500) continue;
        if(number!=-1 && number!=n) return -1;
        number=n;
    }
    return number;
}
QVector<BatchEpisode> pairEpisodes(const QVector<BatchSourceGroup> &groups, bool orderedFallback, QString *problem) {
    if (problem) problem->clear();
    if (groups.isEmpty()) {if(problem)*problem="No source groups selected.";return {};}
    QVector<QStringList> lists;
    for (const auto &group:groups) {
        const auto files=sortedUnique(group.files);
        if (files.isEmpty()) {if(problem)*problem="Select episode files for every source group.";return {};}
        for(const auto &file:files) if(!QFileInfo(file).isFile()) {if(problem)*problem="Missing source file: "+file;return {};}
        lists.append(files);
    }
    QVector<BatchEpisode> result;
    if (orderedFallback) {
        const int count=lists.front().size();
        for(const auto &files:lists) if(files.size()!=count) {
            if(problem)*problem="Sorted-order pairing requires the same file count in every source group.";return {};
        }
        QSet<int> seenNumbers;
        for(int i=0;i<count;++i) {
            BatchEpisode e;
            const int detected=episodeNumber(lists.front()[i]);
            e.number=detected>0?detected:i+1;
            if(seenNumbers.contains(e.number)) e.issue=QString("Duplicate episode number %1 in sorted-order pairing.").arg(e.number);
            seenNumbers.insert(e.number);
            for(const auto &files:lists) e.files << files[i];
            result.append(e);
        }
        return result;
    }
    QVector<QMap<int,QString>> indexed;
    QSet<int> numbers;
    for(int groupIndex=0;groupIndex<lists.size();++groupIndex) {
        QMap<int,QString> byEpisode;
        for(const auto &file:lists[groupIndex]) {
            const int episode=episodeNumber(file);
            if(episode<=0) {if(problem)*problem=QString("Cannot identify episode number in group %1: %2. Use explicit 'Episode 01' / 'S01E01' names or opt into sorted-order pairing.").arg(groupIndex+1).arg(QFileInfo(file).fileName());return {};}
            if(byEpisode.contains(episode)) {if(problem)*problem=QString("Duplicate episode %1 in source group %2.").arg(episode).arg(groupIndex+1);return {};}
            byEpisode.insert(episode,file); numbers.insert(episode);
        }
        indexed.append(byEpisode);
    }
    QList<int> episodes=numbers.values(); std::sort(episodes.begin(),episodes.end());
    for(int episode:episodes) {
        BatchEpisode row; row.number=episode;
        for(int g=0;g<indexed.size();++g) {
            const QString file=indexed[g].value(episode);
            row.files << file;
            if(file.isEmpty()) row.issue += QString("Missing episode %1 in source group %2. ").arg(episode).arg(g+1);
        }
        result.append(row);
    }
    return result;
}
MuxPlan makePlan(const MuxPlan &sample,const QVector<BatchSourceGroup> &groups,
                 const BatchEpisode &episode,const QMap<QString,MediaInfo> &probes,
                 const QString &outputDirectory,QString *problem) {
    MuxPlan plan; if(problem)problem->clear();
    if(!episode.issue.isEmpty()) {if(problem)*problem=episode.issue;return {};}
    plan.preserveGlobalMetadata=sample.preserveGlobalMetadata;
    plan.output=QDir(outputDirectory).filePath("Episode "+two(episode.number)+".mkv");
    for(const MuxTrack &original:sample.tracks) {
        int groupIndex=-1;
        for(int i=0;i<groups.size();++i) if(norm(groups[i].samplePath)==norm(original.source)) {groupIndex=i;break;}
        if(groupIndex<0 || groupIndex>=episode.files.size()) {if(problem)*problem="A template track has no corresponding source group.";return {};}
        const QString path=episode.files[groupIndex];
        const auto it=probes.constFind(path);
        if(it==probes.constEnd() || !it->error.isEmpty()) {if(problem)*problem="Cannot inspect: "+QFileInfo(path).fileName();return {};}
        const MediaInfo &info=it.value();
        QList<MediaStream> matches;
        for(const auto &s:info.streams) if(s.type==original.type && s.codec==original.codec && lang(s.language)==lang(original.language)) matches.append(s);
        // Best signal is original absolute stream index + type + language + codec.
        const MediaStream *found=nullptr;
        for(const auto &s:matches) if(s.index==original.streamIndex) {found=&s;break;}
        if(!found && matches.size()==1) found=&matches.front();
        if(!found) {
            if(problem)*problem=QString("Track mismatch in %1: cannot uniquely find %2 %3 (%4). Review this episode manually.")
               .arg(QFileInfo(path).fileName(),original.language,original.type,original.codec);
            return {};
        }
        MuxTrack track=original;
        track.source=path;track.streamIndex=found->index;track.codec=found->codec;
        track.duration=found->duration>0?found->duration:info.duration;
        track.durationEstimated=!found->durationKnown;track.startTime=found->startTime;
        // Template offsets may reflect episode one's timing: never apply blindly.
        track.delayMs=0;
        plan.tracks.append(track);
    }
    if(!sample.chapterSource.isEmpty()) {
        for(int i=0;i<groups.size();++i) if(norm(groups[i].samplePath)==norm(sample.chapterSource)) {
            plan.chapterSource=episode.files.value(i);break;
        }
        if(plan.chapterSource.isEmpty()) {if(problem)*problem="Chapter source is outside the template groups.";return {};}
    }
    const QString issue=Mux::validate(plan);
    if(!issue.isEmpty() && problem)*problem=issue;
    return plan;
}
MediaInfo inspect(const QString &ffprobe,const QString &path) {
    MediaInfo info; info.path=path;
    QProcess process;
    process.start(ffprobe,{"-v","error","-show_format","-show_streams","-show_chapters","-of","json",path});
    if(!process.waitForStarted(10000)) {info.error="FFprobe failed to start: "+process.errorString();return info;}
    if(!process.waitForFinished(20000)) {process.kill();process.waitForFinished(2000);info.error="FFprobe timed out.";return info;}
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
        info.error=QString::fromUtf8(process.readAllStandardError()).left(500);if(info.error.isEmpty())info.error="FFprobe failed.";return info;
    }
    QJsonParseError error;
    const auto data=QJsonDocument::fromJson(process.readAllStandardOutput(),&error);
    if(error.error!=QJsonParseError::NoError || !data.isObject()) {info.error="FFprobe returned invalid metadata.";return info;}
    const auto root=data.object();
    info.duration=root.value("format").toObject().value("duration").toString().toDouble();
    info.hasChapters=!root.value("chapters").toArray().isEmpty();
    for(const auto &v:root.value("streams").toArray()) {
        const auto s=v.toObject();
        const QString type=s.value("codec_type").toString();
        if(type!="video" && type!="audio" && type!="subtitle")continue;
        if(type=="video" && s.value("disposition").toObject().value("attached_pic").toInt()!=0)continue;
        MediaStream t;
        t.type=type;t.index=s.value("index").toInt(-1);t.codec=s.value("codec_name").toString();
        const auto tags=s.value("tags").toObject();
        t.language=tags.value("language").toString("und");t.title=tags.value("title").toString();
        const auto disp=s.value("disposition").toObject();
        t.isDefault=disp.value("default").toInt()==1;t.isForced=disp.value("forced").toInt()==1;
        t.startTime=s.value("start_time").toString().toDouble();
        t.duration=s.value("duration").toString().toDouble();
        if(t.duration>0) t.durationKnown=true;
        else {
            const double end=durationTag(tags.value("DURATION").toString());
            if(end>t.startTime) {t.duration=end-t.startTime;t.durationKnown=true;}
        }
        info.streams.append(t);
        if(t.type=="audio")info.hasAudio=true;
        if(t.type=="video")info.hasVideo=true;
    }
    if(info.streams.isEmpty()) info.error="No usable media streams.";
    return info;
}
}
