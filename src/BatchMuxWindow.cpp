#include "BatchMuxWindow.h"
#include "WaveSync.h"
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QSet>
#include <QFileInfoList>
#include <QMetaObject>
#include <QSignalBlocker>
#include <algorithm>

namespace {
const QString mediaFilter="Media files (*.mkv *.mp4 *.mov *.avi *.webm *.m4v *.ts *.m2ts *.mpg *.flv *.mp3 *.m4a *.aac *.flac *.opus *.ogg *.wav);;All files (*)";
QStringList contents(const QString &directory) {
    QDir dir(directory);
    const auto entries=dir.entryInfoList({"*.mkv","*.mp4","*.mov","*.avi","*.webm","*.m4v","*.ts","*.m2ts","*.mpg","*.flv","*.mp3","*.m4a","*.aac","*.flac","*.opus","*.ogg","*.wav"},QDir::Files|QDir::Readable,QDir::Name|QDir::IgnoreCase);
    QStringList paths;
    for(const auto &fi:entries) if(!fi.fileName().endsWith(".harbinger-part.mkv",Qt::CaseInsensitive)) paths << fi.absoluteFilePath();
    return paths;
}
QString clipped(const QString &path) {return QFileInfo(path).fileName();}
}
BatchMuxWindow::BatchMuxWindow(const QString &ffmpeg,const QString &ffprobe,
                               const MuxPlan &sample,QWidget *parent)
    : QDialog(parent), ffmpeg_(ffmpeg),ffprobe_(ffprobe),sample_(sample),engine_(this) {
    setWindowTitle("Harbinger — Batch MKV Muxing");
    setWindowModality(Qt::WindowModal);
    setMinimumSize(1050,700);resize(1240,850);
    engine_.setExecutable(ffmpeg_);
    QSet<QString> seen;
    for(const auto &track:sample_.tracks) if(!seen.contains(track.source)) {
        seen.insert(track.source);groups_.append({track.source,{track.source}});
    }
    if(!sample_.chapterSource.isEmpty() && !seen.contains(sample_.chapterSource)) {
        groups_.append({sample_.chapterSource,{sample_.chapterSource}});
    }
    auto *root=new QVBoxLayout(this);
    auto *title=new QLabel("Batch MKV Muxing — one track template for the whole season");
    title->setObjectName("brand");root->addWidget(title);
    auto *help=new QLabel("Select the tracks for one example episode in the normal MKV muxer, then supply the remaining episodes for each source role here. Harbinger will pair files by episode number and inspect every track before processing.");
    help->setWordWrap(true);root->addWidget(help);
    root->addWidget(new QLabel("SOURCE ROLES — one set of episodes per template source"));
    sources_=new QTableWidget(static_cast<int>(groups_.size()),3);
    sources_->setHorizontalHeaderLabels({"Source role","Example episode","Selected files"});
    sources_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    sources_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    sources_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    sources_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sources_->setSelectionMode(QAbstractItemView::SingleSelection);
    sources_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sources_->setMaximumHeight(155);
    root->addWidget(sources_);refreshSources();
    auto *inputButtons=new QHBoxLayout;
    auto *folder=new QPushButton("Use folder for selected source");
    auto *files=new QPushButton("Choose files for selected source");
    inputButtons->addWidget(folder);inputButtons->addWidget(files);inputButtons->addStretch();root->addLayout(inputButtons);
    connect(folder,&QPushButton::clicked,this,&BatchMuxWindow::chooseFolder);
    connect(files,&QPushButton::clicked,this,&BatchMuxWindow::chooseFiles);
    auto *options=new QHBoxLayout;
    sorted_=new QCheckBox("Pair by sorted order (only if filenames have no episode numbers)");
    sorted_->setToolTip("Less safe than episode-number matching. Requires equal file counts. Review every row carefully before processing.");
    options->addWidget(sorted_);options->addStretch();root->addLayout(options);
    auto *syncRow=new QHBoxLayout;
    waveform_=new QCheckBox("Analyse waveform sync separately for every episode");
    waveform_->setChecked(true);
    syncRow->addWidget(waveform_);
    syncRow->addWidget(new QLabel("Reference audio:"));
    reference_=new QComboBox;
    for(int i=0;i<sample_.tracks.size();++i) {
        const auto &t=sample_.tracks[i];
        if(t.type=="audio") reference_->addItem(QString("%1 · %2 · #%3").arg(clipped(t.source),t.language).arg(t.streamIndex),i);
    }
    if(reference_->count()<2) {waveform_->setChecked(false);waveform_->setEnabled(false);reference_->setEnabled(false);}
    syncRow->addWidget(reference_,1);root->addLayout(syncRow);
    auto *outRow=new QHBoxLayout;
    outRow->addWidget(new QLabel("Output folder:"));
    output_=new QLineEdit(QDir(QFileInfo(groups_.front().samplePath).absolutePath()).filePath("Harbinger Batch Output"));
    outRow->addWidget(output_,1);
    auto *browse=new QPushButton("Browse…");outRow->addWidget(browse);root->addLayout(outRow);
    connect(browse,&QPushButton::clicked,this,&BatchMuxWindow::chooseOutput);
    connect(sorted_,&QCheckBox::toggled,this,&BatchMuxWindow::invalidate);
    connect(waveform_,&QCheckBox::toggled,this,&BatchMuxWindow::invalidate);
    connect(reference_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&BatchMuxWindow::invalidate);
    connect(output_,&QLineEdit::textChanged,this,&BatchMuxWindow::invalidate);
    auto *bar=new QHBoxLayout;
    bar->addWidget(new QLabel("EPISODE MATCHING & SYNC PREVIEW"));bar->addStretch();
    prepareButton_=new QPushButton("Analyse episodes & preview");prepareButton_->setObjectName("primarySubtle");
    bar->addWidget(prepareButton_);root->addLayout(bar);
    preview_=new QTableWidget(0,5);
    preview_->setHorizontalHeaderLabels({"Episode","Source files","Streams","Sync / review","Output"});
    preview_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    preview_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    preview_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    preview_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    preview_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    preview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(preview_,1);
    auto *runRow=new QHBoxLayout;
    progress_=new QProgressBar;progress_->setRange(0,100);progress_->setValue(0);runRow->addWidget(progress_,1);
    startButton_=new QPushButton("Start batch muxing");startButton_->setObjectName("primary");startButton_->setEnabled(false);
    cancelButton_=new QPushButton("Cancel");cancelButton_->setEnabled(false);
    runRow->addWidget(startButton_);runRow->addWidget(cancelButton_);root->addLayout(runRow);
    status_=new QLabel("Choose episode files for each source group, then analyse and review the matches.");status_->setWordWrap(true);root->addWidget(status_);
    log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(3000);log_->setFixedHeight(100);root->addWidget(log_);
    connect(prepareButton_,&QPushButton::clicked,this,&BatchMuxWindow::prepare);
    connect(startButton_,&QPushButton::clicked,this,&BatchMuxWindow::startBatch);
    connect(cancelButton_,&QPushButton::clicked,this,&BatchMuxWindow::cancelBatch);
    connect(&engine_,&MuxEngine::logLine,this,[this](const QString &text){log_->appendPlainText(text);});
    connect(&engine_,&MuxEngine::progressChanged,progress_,&QProgressBar::setValue);
    connect(&engine_,&MuxEngine::finished,this,&BatchMuxWindow::finishedJob);
}
BatchMuxWindow::~BatchMuxWindow() {
    if(stop_)stop_->store(true);
    if(prepareThread_ && prepareThread_->isRunning()) prepareThread_->wait();
}
void BatchMuxWindow::refreshSources() {
    for(int i=0;i<groups_.size();++i) {
        const auto &group=groups_[i];
        QStringList types;
        for(const auto &t:sample_.tracks) if(t.source==group.samplePath) types<<t.type+" #"+QString::number(t.streamIndex)+" ("+t.language+")";
        const QString label=types.isEmpty()?QString("Chapters"):types.join(", ");
        auto *role=new QTableWidgetItem(label);role->setToolTip(label);
        auto *example=new QTableWidgetItem(clipped(group.samplePath));example->setToolTip(group.samplePath);
        auto *count=new QTableWidgetItem(QString::number(group.files.size())+" file(s)");
        QString names;for(const auto &file:group.files) names+=file+"\n";count->setToolTip(names);
        sources_->setItem(i,0,role);sources_->setItem(i,1,example);sources_->setItem(i,2,count);
    }
    if(sources_->currentRow()<0 && sources_->rowCount()>0) sources_->selectRow(0);
}
void BatchMuxWindow::invalidate() {
    if(preparing_ || running_) return;
    ready_=false;rows_.clear();preview_->setRowCount(0);
    startButton_->setEnabled(false);
}
void BatchMuxWindow::chooseFiles() {
    if(preparing_||running_)return;
    const int row=sources_->currentRow();if(row<0||row>=groups_.size())return;
    auto paths=QFileDialog::getOpenFileNames(this,"Select all episode files for this source role",QFileInfo(groups_[row].samplePath).absolutePath(),mediaFilter);
    if(paths.isEmpty())return;
    groups_[row].files=paths;invalidate();refreshSources();
}
void BatchMuxWindow::chooseFolder() {
    if(preparing_||running_)return;
    const int row=sources_->currentRow();if(row<0||row>=groups_.size())return;
    const QString folder=QFileDialog::getExistingDirectory(this,"Select episode folder for this source role",QFileInfo(groups_[row].samplePath).absolutePath());
    if(folder.isEmpty())return;
    const QStringList paths=contents(folder);
    if(paths.isEmpty()) {QMessageBox::warning(this,"No media files","No supported media files found directly in that folder.");return;}
    groups_[row].files=paths;invalidate();refreshSources();
}
void BatchMuxWindow::chooseOutput() {
    if(preparing_||running_)return;
    const QString path=QFileDialog::getExistingDirectory(this,"Choose existing output folder",QFileInfo(groups_.front().samplePath).absolutePath());
    if(!path.isEmpty())output_->setText(path);
}
void BatchMuxWindow::setBusy() {
    const bool busy=preparing_||running_;
    sources_->setEnabled(!busy);output_->setEnabled(!busy);
    waveform_->setEnabled(!busy && reference_->count()>1);
    reference_->setEnabled(!busy && reference_->count()>1 && waveform_->isChecked());
    sorted_->setEnabled(!busy);
    prepareButton_->setEnabled(!busy);
    startButton_->setEnabled(!busy && ready_);
    cancelButton_->setEnabled(busy);
}
void BatchMuxWindow::prepare() {
    if(preparing_||running_)return;
    const QString out=output_->text().trimmed();
    if(out.isEmpty()) {QMessageBox::warning(this,"Output folder","Choose an output folder.");return;}
    // Output location must exist; no implicit creation or overwriting of files.
    if(!QDir(out).exists()) {
        if(QMessageBox::question(this,"Create output folder?", "The output folder does not exist. Create it now?",
                                 QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes)return;
        if(!QDir().mkpath(out)) {QMessageBox::warning(this,"Output folder","Cannot create the output folder.");return;}
    }
    ready_=false;preparing_=true;stop_=std::make_shared<std::atomic_bool>(false);setBusy();
    preview_->setRowCount(0);
    status_->setText("Matching episodes, inspecting tracks and analysing audio…");
    const auto groups=groups_;const auto sample=sample_;
    const QString ffprobe=ffprobe_,ffmpeg=ffmpeg_;
    const bool ordered=sorted_->isChecked(),sync=waveform_->isChecked();
    const int refIndex=reference_->currentData().toInt();
    const auto stop=stop_;
    auto *thread=QThread::create([this,groups,sample,out,ffprobe,ffmpeg,ordered,sync,refIndex,stop]() {
        QString error;
        auto rows=BatchMux::pairEpisodes(groups,ordered,&error);
        if(error.isEmpty()) {
            QMap<QString,MediaInfo> cache;
            int analysed=0;
            for(auto &episode:rows) {
                if(stop->load()) break;
                ++analysed;
                const int completed=analysed, total=static_cast<int>(rows.size());
                QMetaObject::invokeMethod(this,[this,completed,total] {
                    status_->setText(QString("Preparing episode %1 of %2 — inspecting media and checking synchronization…")
                         .arg(completed).arg(total));
                },Qt::QueuedConnection);
                if(!episode.issue.isEmpty())continue;
                for(const auto &path:episode.files) if(!path.isEmpty()&&!cache.contains(path))
                    cache.insert(path,BatchMux::inspect(ffprobe,path));
                episode.plan=BatchMux::makePlan(sample,groups,episode,cache,out,&episode.issue);
                if(!episode.issue.isEmpty()||!sync)continue;
                if(refIndex<0||refIndex>=episode.plan.tracks.size()) {episode.issue="Missing audio reference.";continue;}
                const MuxTrack reference=episode.plan.tracks.at(refIndex);
                if(reference.type!="audio") {episode.issue="Invalid audio reference.";continue;}
                for(int i=0;i<episode.plan.tracks.size();++i) {
                    if(stop->load())break;
                    auto &track=episode.plan.tracks[i];
                    if(track.type!="audio"||i==refIndex)continue;
                    const auto proposal=WaveSync::analyse(ffmpeg,reference,track,*stop);
                    if(proposal.reliable) {
                        track.delayMs=proposal.delayMs;
                        episode.syncReport+=QString("%1 #%2: %3 ms (score %4 / %5). ")
                            .arg(track.language).arg(track.streamIndex).arg(proposal.delayMs>0?"+"+QString::number(proposal.delayMs):QString::number(proposal.delayMs))
                            .arg(proposal.correlation,0,'f',2).arg(proposal.secondCorrelation,0,'f',2);
                    } else {
                        episode.issue=QString("Audio #%1: %2").arg(track.streamIndex).arg(proposal.explanation);
                        break; // Never mux an uncertain sync adjustment silently.
                    }
                }
            }
        }
        QMetaObject::invokeMethod(this,[this,rows,error,stop]() {
            preparing_=false;
            if(stop->load()) {status_->setText("Preparation cancelled.");rows_.clear();ready_=false;}
            else onPrepared(rows,error);
            setBusy();
        },Qt::QueuedConnection);
    });
    prepareThread_=thread;
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    connect(thread,&QObject::destroyed,this,[this]{prepareThread_=nullptr;});
    thread->start();
}
void BatchMuxWindow::onPrepared(const QVector<BatchEpisode> &rows,const QString &error) {
    rows_=rows;preview_->setRowCount(rows_.size());
    int valid=0;
    for(int i=0;i<rows_.size();++i) {
        const auto &row=rows_[i];
        auto *number=new QTableWidgetItem(QString("Episode %1").arg(row.number,2,10,QChar('0')));
        QString names;for(int g=0;g<row.files.size();++g)names+=QString("Group %1: %2\n").arg(g+1).arg(clipped(row.files[g]));
        auto *paths=new QTableWidgetItem(names.trimmed());paths->setToolTip(names);
        QString trackCounts="—";
        if(row.issue.isEmpty()) {
            int video=0,audio=0,subtitle=0;
            for(const auto &track:row.plan.tracks) {if(track.type=="video")++video;if(track.type=="audio")++audio;if(track.type=="subtitle")++subtitle;}
            trackCounts=QString("%1 video, %2 audio, %3 subs").arg(video).arg(audio).arg(subtitle);++valid;
        }
        QString sync=row.issue.isEmpty()?(row.syncReport.isEmpty()?"Ready (no automatic offsets)":row.syncReport):"SKIP — "+row.issue;
        auto *review=new QTableWidgetItem(sync);
        review->setToolTip(sync);
        preview_->setItem(i,0,number);preview_->setItem(i,1,paths);
        preview_->setItem(i,2,new QTableWidgetItem(trackCounts));preview_->setItem(i,3,review);
        preview_->setItem(i,4,new QTableWidgetItem(clipped(row.plan.output)));
    }
    preview_->resizeRowsToContents();
    ready_=valid>0;
    status_->setText(error.isEmpty()?QString("Preview ready: %1 episodes, %2 ready, %3 require manual review. Output files will never be overwritten.")
                     .arg(rows_.size()).arg(valid).arg(rows_.size()-valid):error);
    if(!error.isEmpty())QMessageBox::warning(this,"Cannot match episodes",error);
}
void BatchMuxWindow::startBatch() {
    if(!ready_||preparing_||running_)return;
    int valid=0,skipped=0;
    for(const auto &row:rows_) {if(row.issue.isEmpty()) ++valid; else ++skipped;}
    if(QMessageBox::question(this,"Start batch muxing",
       QString("Mux %1 episode(s)? %2 episode(s) needing review will be skipped.\n\n"
               "Review every file pairing and synchronization suggestion first. "
               "Stream-copy preserves video and audio; existing output files will not be replaced.")
       .arg(valid).arg(skipped),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
    running_=true;runningIndex_=-1;finishedCount_=failedCount_=0;setBusy();
    status_->setText("Batch muxing started…");startNext();
}
void BatchMuxWindow::startNext() {
    if(!running_)return;
    ++runningIndex_;
    while(runningIndex_<rows_.size() && !rows_[runningIndex_].issue.isEmpty()) ++runningIndex_;
    if(runningIndex_>=rows_.size()) {
        running_=false;ready_=false;setBusy();progress_->setValue(100);
        status_->setText(QString("Batch finished: %1 succeeded, %2 failed. Review skipped episodes manually.").arg(finishedCount_).arg(failedCount_));
        QMessageBox::information(this,"Batch completed",status_->text());return;
    }
    const auto &episode=rows_[runningIndex_];
    status_->setText(QString("Muxing episode %1 (%2 of %3)…").arg(episode.number).arg(runningIndex_+1).arg(rows_.size()));
    progress_->setValue(0);
    preview_->selectRow(runningIndex_);
    if(!engine_.start(episode.plan)) {
        // Validation emits finished synchronously, so the finishedJob handler advances.
        return;
    }
}
void BatchMuxWindow::finishedJob(bool success,bool cancelled,const QString &message,const QString &output) {
    if(!running_)return;
    if(success)++finishedCount_;else ++failedCount_;
    if(runningIndex_>=0 && runningIndex_<preview_->rowCount()) {
        const QString detail=success?("Completed: "+output):(cancelled?"Cancelled":"FAILED: "+message.left(180));
        preview_->item(runningIndex_,3)->setText(detail);
        preview_->item(runningIndex_,3)->setToolTip(detail);
    }
    if(!success)log_->appendPlainText("Episode failed: "+message);
    if(cancelled) {
        running_=false;ready_=false;setBusy();status_->setText("Batch cancelled. Completed outputs were retained.");return;
    }
    QTimer::singleShot(0,this,&BatchMuxWindow::startNext);
}
void BatchMuxWindow::cancelBatch() {
    if(preparing_ && stop_) {
        stop_->store(true);status_->setText("Cancelling analysis after the current sample…");
        cancelButton_->setEnabled(false);return;
    }
    if(running_) {
        cancelButton_->setEnabled(false);engine_.cancel();status_->setText("Cancelling current mux job…");
    }
}
void BatchMuxWindow::reject() {
    if(preparing_||running_) {
        QMessageBox::information(this,"Batch is active","Cancel the current batch or analysis and allow it to stop before closing this window.");return;
    }
    QDialog::reject();
}
