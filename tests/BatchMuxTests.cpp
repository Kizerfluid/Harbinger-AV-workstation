#include "../src/BatchMuxPlan.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDebug>

static void check(bool good,const QString &message) {if(!good)qFatal("FAILED: %s",qPrintable(message));}
static QString file(const QString &dir,const QString &name) {
    const QString path=QDir(dir).filePath(name);QFile f(path);check(f.open(QIODevice::WriteOnly),"Create test file");return path;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    check(BatchMux::episodeNumber("[Cleo]Tsugumomo_-_01_(Dual Audio_10bit_BD1080p_x265).mkv")==1,"Cleo anime episode");
    check(BatchMux::episodeNumber("Show.S01E13.1080p.mkv")==13,"season-episode");
    check(BatchMux::episodeNumber("show Episode 04 [1080p].mkv")==4,"explicit episode");
    check(BatchMux::episodeNumber("show.1080p.x265.mkv")==-1,"quality tokens are not episodes");
    QTemporaryDir dir;check(dir.isValid(),"temp dir");
    const QString a1=file(dir.path(),"Video - 01.mkv"),a2=file(dir.path(),"Video - 02.mkv");
    const QString b1=file(dir.path(),"Dub S01E01.mkv"),b2=file(dir.path(),"Dub S01E02.mkv");
    const QString c1=file(dir.path(),"JP Episode 01.mkv"),c2=file(dir.path(),"JP Episode 02.mkv");
    QVector<BatchSourceGroup> groups{{a1,{a1,a2}},{b1,{b1,b2}},{c1,{c1,c2}}};
    QString issue;
    auto episodes=BatchMux::pairEpisodes(groups,false,&issue);
    check(issue.isEmpty() && episodes.size()==2,"multi-group episode pairing");
    check(episodes[1].files==QStringList({a2,b2,c2}),"episode 2 correct sources");
    MuxPlan sample;sample.output=QDir(dir.path()).filePath("unused.mkv");
    sample.tracks.append({a1,0,"video","hevc","und","",true,false,30});
    sample.tracks.append({b1,1,"audio","aac","eng","Dub",true,false,30});
    sample.tracks.append({c1,2,"audio","aac","jpn","Original",false,false,30});
    sample.tracks.append({c1,3,"subtitle","ass","eng","Signs",true,false,30});
    QMap<QString,MediaInfo> media;
    for(const auto &episode:episodes) {
        for(int g=0;g<3;++g) {
            MediaInfo info;info.path=episode.files[g];info.duration=30;
            if(g==0)info.streams.append({0,"video","hevc","und"});
            if(g==1)info.streams.append({1,"audio","aac","eng"});
            if(g==2) {
                info.streams.append({2,"audio","aac","jpn"});
                info.streams.append({3,"subtitle","ass","eng"});
            }
            media.insert(info.path,info);
        }
        QString failed;
        const auto plan=BatchMux::makePlan(sample,groups,episode,media,dir.path(),&failed);
        check(failed.isEmpty(),"plan must validate: "+failed);
        check(plan.tracks.size()==4 && plan.tracks[1].source==episode.files[1],"plan should follow selected roles");
        check(plan.tracks[2].language=="jpn" && plan.tracks[3].type=="subtitle","audio/subtitle selection retained");
    }
    // Reject changed track languages rather than silently assigning another audio.
    auto wrong=media;wrong[c2].streams[0].language="eng";
    const auto invalid=BatchMux::makePlan(sample,groups,episodes[1],wrong,dir.path(),&issue);
    check(!issue.isEmpty() && invalid.tracks.size()<4,"changed track requires review");
    // A changed absolute stream index can be resolved if codec/language identify
    // exactly one matching track; two indistinguishable tracks must be rejected.
    auto moved=media; moved[c2].streams[0].index=9;
    issue.clear();auto corrected=BatchMux::makePlan(sample,groups,episodes[1],moved,dir.path(),&issue);
    check(issue.isEmpty() && corrected.tracks[2].streamIndex==9,"uniquely identifiable shifted audio stream");
    moved[c2].streams.append({10,"audio","aac","jpn"});
    issue.clear();BatchMux::makePlan(sample,groups,episodes[1],moved,dir.path(),&issue);
    check(!issue.isEmpty(),"ambiguous shifted audio stream must fail");
    groups[2].files.removeLast();issue.clear();episodes=BatchMux::pairEpisodes(groups,false,&issue);
    check(issue.isEmpty() && episodes.size()==2 && !episodes[1].issue.isEmpty(),"missing episode visible");
    qInfo()<<"BatchMuxTests passed";return 0;
}
