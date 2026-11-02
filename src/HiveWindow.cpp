#include "HiveWindow.h"
#include "HiveProtocol.h"
#include "Codec.h"
#include "SmartEncoding.h"
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkDatagram>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUdpSocket>
#include <QJsonArray>
#include <cmath>
#include <algorithm>

HiveWindow::HiveWindow(HiveWorker *worker,const QString &ffmpeg,const QString &ffprobe,const QString &folder,
                       const QVector<EncodeTask> &sources,const EncodeOptions &options,
                       const Hardware::Report &hardware,QWidget *parent)
    : QDialog(parent),worker_(worker),ffmpeg_(ffmpeg),ffprobe_(ffprobe),folder_(folder),sources_(sources),
      options_(options),hardware_(hardware),localEngine_(this) {
    setWindowTitle("Harbinger Hive — LAN transcoding");
    resize(1020,760);
    setAttribute(Qt::WA_DeleteOnClose,false);
    auto *root=new QVBoxLayout(this);
    auto *heading=new QLabel("Harbinger Hive");
    QFont font=heading->font();font.setPointSize(font.pointSize()+7);font.setBold(true);heading->setFont(font);
    root->addWidget(heading);
    root->addWidget(new QLabel("Share spare computing power or distribute video encoding across trusted computers on your LAN."));
    auto *workerGroup=new QGroupBox("This computer as a worker");
    auto *wlayout=new QVBoxLayout(workerGroup);
    sharing_=new QCheckBox("Allow other Harbinger computers to request encoding work (requires your approval)");
    identity_=new QLabel("Sharing disabled. TLS certificate not loaded.");
    identity_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity_->setWordWrap(true);
    auto *resourceLimits=new QHBoxLayout;
    shareGpu_=new QCheckBox("Allow GPU encoding on this worker");
    shareGpu_->setChecked(worker_->allowGpu());
    workerThreads_=new QSpinBox;
    workerThreads_->setRange(0,64);
    workerThreads_->setSpecialValueText("Auto");
    workerThreads_->setValue(worker_->threadLimit());
    workerThreads_->setToolTip("Maximum FFmpeg codec thread setting; 0 means FFmpeg decides. Not a guaranteed CPU utilisation percentage.");
    resourceLimits->addWidget(shareGpu_);
    resourceLimits->addStretch();
    resourceLimits->addWidget(new QLabel("CPU threads:"));
    resourceLimits->addWidget(workerThreads_);
    wlayout->addWidget(sharing_);wlayout->addLayout(resourceLimits);wlayout->addWidget(identity_);
    connect(shareGpu_,&QCheckBox::toggled,worker_,&HiveWorker::setAllowGpu);
    connect(workerThreads_,QOverload<int>::of(&QSpinBox::valueChanged),worker_,&HiveWorker::setThreadLimit);
    root->addWidget(workerGroup);
    connect(sharing_,&QCheckBox::toggled,this,[this] { updateSharing(); });
    sharing_->setChecked(worker_->enabled());
    auto *networkGroup=new QGroupBox("Available Hive computers");
    auto *nlayout=new QVBoxLayout(networkGroup);
    nodes_=new QTreeWidget;
    nodes_->setColumnCount(4);nodes_->setHeaderLabels({"Use","Computer","IP address","GPU / encoder hardware"});
    nodes_->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    nodes_->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    nodes_->header()->setSectionResizeMode(3,QHeaderView::Stretch);
    nodes_->setRootIsDecorated(false);
    nlayout->addWidget(nodes_);
    auto *manual=new QHBoxLayout;
    manualIp_=new QLineEdit;
    manualIp_->setPlaceholderText("Worker IP address (manual connection if discovery is blocked)");
    auto *addIp=new QPushButton("Add IP");
    manual->addWidget(manualIp_);manual->addWidget(addIp);
    nlayout->addLayout(manual);
    connect(addIp,&QPushButton::clicked,this,[this] {
        QHostAddress addr;
        if (!addr.setAddress(manualIp_->text().trimmed()) || addr.protocol()!=QAbstractSocket::IPv4Protocol) {
            QMessageBox::warning(this,"Invalid address","Enter a numeric IPv4 address on your LAN.");return;
        }
        const QString ip=addr.toString(),node="manual:"+ip;
        if (seenIp_.contains(node)) return;
        seenIp_[node]=ip;
        auto *row=new QTreeWidgetItem(nodes_,{"","Manual worker",ip});
        row->setFlags(row->flags()|Qt::ItemIsUserCheckable);row->setCheckState(0,Qt::Checked);
        row->setData(0,Qt::UserRole,node);
    });
    root->addWidget(networkGroup,2);
    auto *optionsLine=new QHBoxLayout;
    local_=new QCheckBox("Include this computer in encoding");local_->setChecked(true);
    split_=new QCheckBox("Split ONE video into 60-second pieces (experimental, matching CPU encoders only)");
    optionsLine->addWidget(local_);optionsLine->addWidget(split_,1);
    if (options_.smart) {
        split_->setChecked(false);
        split_->setEnabled(false);
        split_->setToolTip("Smart Encoding distributes whole files, one episode per machine. Segment splitting is not supported.");

    }
    root->addLayout(optionsLine);
    state_=new QLabel("No distributed job started.");root->addWidget(state_);
    taskTree_=new QTreeWidget;
    taskTree_->setColumnCount(3);taskTree_->setHeaderLabels({"Encoding job / segment","Status","Progress"});
    taskTree_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    root->addWidget(taskTree_,2);
    auto *controls=new QHBoxLayout;
    runButton_=new QPushButton("Start Hive encoding");
    cancelButton_=new QPushButton("Cancel Hive jobs");cancelButton_->setEnabled(false);
    controls->addWidget(runButton_);controls->addWidget(cancelButton_);controls->addStretch();
    root->addLayout(controls);
    log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);log_->setMaximumHeight(125);root->addWidget(log_);
    connect(runButton_,&QPushButton::clicked,this,&HiveWindow::run);
    connect(cancelButton_,&QPushButton::clicked,this,[this] {
        stopping_=true; appendLog("Cancelling distributed jobs…");
        for (auto *c:clients_) c->disconnectWorker();
        localEngine_.cancel();localSegment_.kill();assembler_.kill();validator_.kill();
        if (localIndex_<0 && !localEngine_.running() && assembler_.state()==QProcess::NotRunning) finishBatch();
    });
    discovery_=new QUdpSocket(this);
    if (!discovery_->bind(QHostAddress::AnyIPv4,HiveProtocol::DiscoveryPort,
            QUdpSocket::ShareAddress|QUdpSocket::ReuseAddressHint))
        appendLog("LAN discovery could not bind; add workers by IP instead.");
    else connect(discovery_,&QUdpSocket::readyRead,this,&HiveWindow::announceReceived);
    localEngine_.setExecutable(ffmpeg_);
    connect(&localEngine_,&Engine::jobFinished,this,[this](int,bool ok,bool cancelled,const QString &err,const QString &output) {
        if (localIndex_>=0) taskFinished(localIndex_,ok,cancelled?"Cancelled":err,output);
    });
    connect(&localEngine_,&Engine::queueFinished,this,[this] { localIndex_=-1;schedule(); });
    connect(&localEngine_,&Engine::logLine,this,[this](const QString &s) { appendLog(s); });
    connect(&localEngine_,&Engine::jobProgress,this,[this](int index,int p) {
        if (index>=0 && index<tasks_.size()) {tasks_[index].progress=p;refreshTasks();}
    });
    connect(&localSegment_,&QProcess::readyReadStandardOutput,this,[this] {
        const QByteArray output=localSegment_.readAllStandardOutput();
        if (localIndex_<0 || localIndex_>=tasks_.size()) return;
        for (const auto &line:output.split('\n')) {
            if (!line.startsWith("out_time=")) continue;
            const auto parts=line.mid(9).trimmed().split(':');
            if (parts.size()!=3) continue;
            const double secs=parts[0].toDouble()*3600+parts[1].toDouble()*60+parts[2].toDouble();
            const double length=tasks_[localIndex_].task.length;
            if (length>0) {tasks_[localIndex_].progress=qBound(0,int(100*secs/length),99);refreshTasks();}
        }
    });
    connect(&localSegment_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,&HiveWindow::localFinished);
    connect(&validator_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
        this,&HiveWindow::validationFinished);
    connect(&validator_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e) {
        if (e==QProcess::FailedToStart && active_) {
            appendLog("FFprobe could not start: "+validator_.errorString());finishBatch();
        }
    });
    connect(&localSegment_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e) {
        if (e==QProcess::FailedToStart && localIndex_>=0) {
            const int index=localIndex_;localIndex_=-1;
            taskFinished(index,false,localSegment_.errorString(),{});schedule();
        }
    });
    connect(&assembler_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e) {
        if (e==QProcess::FailedToStart && active_) {
            appendLog("FFmpeg assembler could not start: "+assembler_.errorString());finishBatch();
        }
    });
    connect(&assembler_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
      [this](int code,QProcess::ExitStatus status) {
        const QString partial=splitOutput_+".harbinger-hive-part.mkv";
        if (!stopping_ && code==0 && status==QProcess::NormalExit && QFileInfo::exists(partial) &&
            !QFileInfo::exists(splitOutput_) && QFile::rename(partial,splitOutput_)) {
            assembled_=true;appendLog("Merged output: "+splitOutput_);
        } else {
            QFile::remove(partial);appendLog("Segment assembly failed: "+QString::fromUtf8(assembler_.readAllStandardError()).right(2000));
        }
        finishBatch();
      });
    connect(worker_,&HiveWorker::logMessage,this,&HiveWindow::appendLog);
    if (options_.smart) appendLog("Smart Hive: per-episode bitrate plans, whole-file distribution and lossless MKV remux on each worker.");
    for (const auto &src:sources_) {
        auto *item=new QTreeWidgetItem(taskTree_,{QFileInfo(src.info.path).fileName(),"Waiting","0%"});
        Q_UNUSED(item);
    }
    if (sources_.isEmpty()) state_->setText("Add files to the main encoding queue, then open Hive.");
    if (worker_->enabled()) updateSharing();
}
HiveWindow::~HiveWindow() {
    for (auto *c:clients_) c->disconnectWorker();
    localEngine_.cancel();localSegment_.kill();assembler_.kill();validator_.kill();
}
void HiveWindow::updateSharing() {
    if (sharing_->isChecked() && !worker_->enabled()) {
        QString error;
        if (!worker_->enable(&error)) {
            QMessageBox::warning(this,"Hive worker unavailable",error);
            sharing_->blockSignals(true);sharing_->setChecked(false);sharing_->blockSignals(false);
        }
    } else if (!sharing_->isChecked() && worker_->enabled()) worker_->disable();
    if (worker_->enabled()) {
        const QString hash=worker_->fingerprint();
        identity_->setText("Worker ready. Give the connecting host this verification code: " + hash.left(16).toUpper() +
            "\n(TLS SHA-256 certificate: "+hash.toUpper()+")\nApprove each connection on THIS computer.");
    } else identity_->setText("Worker sharing is off. No other computer can start new jobs here.");
}
void HiveWindow::announceReceived() {
    while (discovery_->hasPendingDatagrams()) {
        const auto d=discovery_->receiveDatagram();
        const auto msg=QJsonDocument::fromJson(d.data()).object();
        if (msg.value("kind").toString()!="harbinger-hive" || msg.value("protocol").toInt()!=HiveProtocol::Version) continue;
        const QString node=msg.value("node").toString();
        if (node.isEmpty() || node==worker_->nodeId() || seenIp_.contains(node)) continue;
        seenIp_[node]=d.senderAddress().toString();
        auto *row=new QTreeWidgetItem(nodes_,{"",msg.value("name").toString("Harbinger worker"),seenIp_.value(node)});
        row->setFlags(row->flags()|Qt::ItemIsUserCheckable);row->setCheckState(0,Qt::Checked);
        row->setData(0,Qt::UserRole,node);
        appendLog("Discovered "+row->text(1)+" ("+row->text(2)+")");
    }
}
QString HiveWindow::reserveOutput(const QString &source,const QString &ext) const {
    const QString base=QFileInfo(source).completeBaseName()+"_hive";
    for (int n=0;n<10000;++n) {
        const QString file=QDir(folder_).filePath(base+(n?"_"+QString::number(n):"")+"."+ext);
        if (!QFileInfo::exists(file) && !QFileInfo::exists(file+".harbinger-hive-part") &&
            !QFileInfo::exists(file+".harbinger-hive-part.mkv")) return file;
    }
    return {};
}
bool HiveWindow::prepareSplit() {
    if (sources_.size()!=1 || !sources_.first().info.hasVideo || sources_.first().info.duration<=0) {
        QMessageBox::warning(this,"Cannot split","Split mode requires exactly one probed video with a known duration.");return false;
    }
    if (options_.videoAction != "transcode") {
        QMessageBox::warning(this,"Cannot split stream-copy video",
            "Single-video distributed splitting requires video transcoding. For passthrough, use the normal Hive queue.");
        return false;
    }
    if (!(options_.videoCodec=="h264" || options_.videoCodec=="hevc")) {
        QMessageBox::warning(this,"Codec not supported for splitting","This early compatibility-mode implementation supports H.264/libx264 and HEVC/libx265 only.");return false;
    }
    const QString encoder=options_.videoCodec=="h264"?"libx264":"libx265";
    if (!hardware_.encoders.contains(encoder)) {
        QMessageBox::warning(this,"Encoder unavailable","Host FFmpeg must have "+encoder+" installed for matching segments.");return false;
    }
    scratch_=std::make_unique<QTemporaryDir>(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("HarbingerHiveParts-XXXXXX"));
    if (!scratch_->isValid()) { QMessageBox::warning(this,"Cannot split","Could not create temporary segment workspace."); return false; }
    splitOutput_=reserveOutput(sources_.first().info.path,"mkv");
    if (splitOutput_.isEmpty()) return false;
    const auto &input=sources_.first();
    const int count=int(std::ceil(input.info.duration/60.0));
    if (count>1000) { QMessageBox::warning(this,"Too many pieces","Limit is 1000 60-second segments per job.");return false; }
    tasks_.clear();
    for (int i=0;i<count;++i) {
        Item entry;
        entry.task.source=input.info.path;
        entry.task.output=QDir(scratch_->path()).filePath(QString("part_%1.mkv").arg(i,4,10,QChar('0')));
        entry.task.media=input.info;
        entry.task.options=options_;
        entry.task.options.container="mkv";
        entry.task.options.audioEncoder="none";
        entry.task.options.videoAction="transcode";
        entry.task.options.audioAction="remove";
        entry.task.options.decodeMode="cpu";
        entry.task.options.encodeMode="cpu";
        entry.task.options.videoEncoder=encoder;
        entry.task.segment=true;
        entry.task.exactEncoder=encoder;
        entry.task.start=i*60.0;
        entry.task.length=qMin(60.0,input.info.duration-i*60.0);
        tasks_.append(entry);
    }
    appendLog(QString("Experimental split: %1 chunks; use identical %2 encoder on every machine. Source transfers may dominate on slower LANs.")
        .arg(count).arg(encoder));
    return true;
}
void HiveWindow::run() {
    if (active_ || sources_.isEmpty()) return;
    if (!QDir(folder_).exists()) { QMessageBox::warning(this,"Output folder missing","Choose an existing output folder in Harbinger.");return; }
    stopping_=false;splitMode_=split_->isChecked();assembled_=false;
    if (options_.smart && splitMode_) {
        QMessageBox::warning(this,"Hive Smart Encoding","Smart Encoding uses whole-file distribution, not experimental segment splitting.");
        return;
    }
    if (splitMode_) { if (!prepareSplit()) return; }
    else {
        tasks_.clear();
        for (const auto &src:sources_) {
            Item entry;entry.task.source=src.info.path;entry.task.media=src.info;
            entry.task.options=options_;
            if (options_.smart) {
                const auto plan=SmartEncoding::makePlan(options_,src.info);
                if (!plan.ok) {
                    QMessageBox::warning(this,"Smart Encoding plan failed",
                        QFileInfo(src.info.path).fileName()+": "+plan.error);
                    tasks_.clear();return;
                }
                entry.task.options=plan.options;
                // The plan has resolved codec choices, per-file bitrate and
                // stream-preservation rules. The flag asks the remote worker
                // to perform the same final MKV stream-copy remux as Engine.
                entry.task.options.smart=true;
                appendLog("Smart plan for "+QFileInfo(src.info.path).fileName()+": "+plan.explanation);
            } else {
                if (!src.decodeOverride.isEmpty()) entry.task.options.decodeMode=src.decodeOverride;
                if (!src.encodeOverride.isEmpty()) entry.task.options.encodeMode=src.encodeOverride;
                if (src.gpuOverride=="default") entry.task.options.gpuId.clear();
                else if (!src.gpuOverride.isEmpty()) entry.task.options.gpuId=src.gpuOverride;
            }
            entry.task.output=reserveOutput(src.info.path,entry.task.options.container);
            // Avoid duplicate output reservations within the same batch.
            while (std::any_of(tasks_.cbegin(),tasks_.cend(),[&](const Item &i){return i.task.output==entry.task.output;})) {
                entry.task.output=entry.task.output.left(entry.task.output.lastIndexOf('.'))+"_extra."+entry.task.options.container;
            }
            tasks_.append(entry);
        }
    }
    active_=true;assembling_=false;runButton_->setEnabled(false);cancelButton_->setEnabled(true);split_->setEnabled(false);
    state_->setText("Connecting to selected workers…");refreshTasks();connectNodes();
    if (clients_.isEmpty() && !local_->isChecked()) {
        QMessageBox::warning(this,"No compute nodes","Select a worker or include this computer.");finishBatch();return;
    }
    schedule();
}
void HiveWindow::connectNodes() {
    for (int i=0;i<nodes_->topLevelItemCount();++i) {
        auto *row=nodes_->topLevelItem(i);
        if (row->checkState(0)!=Qt::Checked) continue;
        const QString id=row->data(0,Qt::UserRole).toString();
        if (clients_.contains(id)) continue;
        const auto addr=row->text(2);
        auto *client=new HiveClient(id,row->text(1),addr,HiveProtocol::ServicePort,this);
        clients_[id]=client;
        connect(client,&HiveClient::fingerprintNeeded,this,[this,client](const QString &,const QString &fp) {
            // Never print the expected code in the prompt: user must read it on the worker.
            const QString pin=QInputDialog::getText(this,"Verify Harbinger Hive worker",
                "On the WORKER PC, open Harbinger Hive and enter the first 16 characters of its TLS verification code here:").trimmed();
            if (pin.compare(fp.left(16),Qt::CaseInsensitive)==0 && pin.size()==16) client->acceptFingerprint(true);
            else { QMessageBox::warning(this,"Worker verification failed","The verification code did not match. Connection refused.");client->acceptFingerprint(false); }
        });
        connect(client,&HiveClient::ready,this,[this,client,row] {
            row->setText(3,client->gpuSummary());
            schedule();
        });
        connect(client,&HiveClient::logMessage,this,&HiveWindow::appendLog);
        connect(client,&HiveClient::transferStatus,this,[this,client](const QString &status) {
            if (inFlight_.contains(client)) { tasks_[inFlight_.value(client)].status=client->label()+" — "+status;refreshTasks(); }
        });
        connect(client,&HiveClient::taskProgress,this,[this,client](int p) {
            if (inFlight_.contains(client)) {tasks_[inFlight_.value(client)].progress=p;refreshTasks();}
        });
        connect(client,&HiveClient::taskDone,this,[this,client](bool success,const QString &error,const QString &out) {
            if (!inFlight_.contains(client)) return;
            const int index=inFlight_.take(client);
            if (index>=0 && index<tasks_.size()) taskFinished(index,success,error,out);
            schedule();
        });
        connect(client,&HiveClient::disconnected,this,[this,client](const QString &reason) {
            appendLog(client->label()+": "+reason);
            const int index=inFlight_.contains(client)?inFlight_.take(client):-1;
            if (index>=0) taskFinished(index,false,"Worker disconnected",{});
            schedule();
        });
        client->connectToWorker();
        QTimer::singleShot(60000,client,[this,client] {
            if (active_ && !client->isReady() && !client->isBusy()) {
                appendLog(client->label()+" did not become ready within 60 seconds.");
                client->disconnectWorker();schedule();
            }
        });
    }
}
int HiveWindow::takeNext(bool forLocal,const QSet<QString> &encoders) {
    for (int i=0;i<tasks_.size();++i) {
        if (tasks_[i].state!=0) continue;
        if (!forLocal) {
            const auto &task=tasks_[i].task;
            if (splitMode_ && !encoders.contains(task.exactEncoder)) continue;
            if (!splitMode_) {
                const auto &o=task.options;
                if (o.audioAction=="transcode" && !encoders.contains(o.audioEncoder)) continue;
                if (o.videoAction=="transcode") {
                    bool videoAvailable=false;
                    for (const QString &encoder : encoders)
                        if (Hardware::codecForEncoder(encoder)==o.videoCodec) {
                            videoAvailable=true;break;
                        }
                    if (!videoAvailable) continue;
                }
            }
        }
        return i;
    }
    return -1;
}
void HiveWindow::schedule() {
    if (!active_) return;
    if (stopping_) {
        if (localIndex_<0 && !localEngine_.running() && inFlight_.isEmpty() &&
            localSegment_.state()==QProcess::NotRunning &&
            assembler_.state()==QProcess::NotRunning &&
            validator_.state()==QProcess::NotRunning) finishBatch();
        return;
    }
    if (assembling_) return;
    if (local_->isChecked() && localIndex_<0 && !localEngine_.running() && localSegment_.state()==QProcess::NotRunning) {
        const int index=takeNext(true);
        if (index>=0) startLocal(index);
    }
    for (auto *client:clients_) {
        if (!client->isReady() || client->isBusy() || inFlight_.contains(client)) continue;
        const int index=takeNext(false,client->encoders());
        if (index<0) continue;
        if (client->startTask(tasks_[index].task)) {
            tasks_[index].state=1;tasks_[index].status=client->label();inFlight_[client]=index;
        }
    }
    refreshTasks();
    bool nonePending=true;
    for (const auto &t:tasks_) if (t.state<2) nonePending=false;
    if (nonePending && localIndex_<0 && inFlight_.isEmpty() && assembler_.state()==QProcess::NotRunning) {
        if (splitMode_ && !assembled_) finaliseSplit();
        else finishBatch();
    } else if (localIndex_<0 && inFlight_.isEmpty() && !local_->isChecked()) {
        bool possible=false;
        for (auto *c:clients_) if (c->isConnectedOrConnecting() &&
            (!c->isReady() || takeNext(false,c->encoders())>=0)) possible=true;
        if (!possible) {
            for (auto &t:tasks_) if (t.state==0) {t.state=3;t.status="No connected worker with required FFmpeg encoders";}
            finishBatch();
        }
    }
}
void HiveWindow::startLocal(int index) {
    localIndex_=index;tasks_[index].state=1;tasks_[index].status="This computer";
    if (!splitMode_) {
        const auto &t=tasks_[index].task;
        EncodeTask input;input.index=index;input.info=t.media;
        // Smart local jobs must pass the original request so Engine plans
        // against the source and applies its own MKV finalisation step.
        localEngine_.start({input},options_.smart ? options_ : t.options,folder_,hardware_);
    } else {
        const auto &t=tasks_[index].task;
        QStringList args=Codec::buildArgs(t.options,t.media,t.output);
        const int i=args.indexOf("-i");
        if (i>=0) {args.insert(i+2,"-ss");args.insert(i+3,QString::number(t.start,'f',3));
                   args.insert(i+4,"-t");args.insert(i+5,QString::number(t.length,'f',3));}
        localSegment_.start(ffmpeg_,args);
    }
    refreshTasks();
}
void HiveWindow::localFinished(int code,QProcess::ExitStatus status) {
    const int index=localIndex_;localIndex_=-1;
    if (index<0) return;
    const bool okay=code==0 && status==QProcess::NormalExit && QFileInfo::exists(tasks_[index].task.output);
    taskFinished(index,okay,okay?QString{}:QString::fromUtf8(localSegment_.readAllStandardError()).right(1200),
                 okay?tasks_[index].task.output:QString{});
    schedule();
}
void HiveWindow::taskFinished(int index,bool success,const QString &reason,const QString &output) {
    if (index<0 || index>=tasks_.size()) return;
    auto &item=tasks_[index];
    if (success) {item.state=2;item.progress=100;item.status="Complete";appendLog("Completed "+QFileInfo(output).fileName());}
    else if (!stopping_ && ++item.tries<2) {
        item.state=0;item.status="Retrying: "+reason.left(90);item.progress=0;
        appendLog("Requeue after failure: "+reason);
    } else {item.state=3;item.status="Failed: "+reason.left(100);appendLog(item.status);}
    refreshTasks();
}
void HiveWindow::refreshTasks() {
    taskTree_->clear();int finished=0;
    for (int i=0;i<tasks_.size();++i) {
        const auto &t=tasks_[i];
        const QString name=splitMode_?QString("Part %1 — %2s–%3s").arg(i+1).arg(t.task.start,0,'f',0).arg(t.task.start+t.task.length,0,'f',0)
                                     : QFileInfo(t.task.source).fileName();
        auto *r=new QTreeWidgetItem(taskTree_,{name,t.status.isEmpty()?"Waiting":t.status,QString::number(t.progress)+"%"});
        Q_UNUSED(r);
        if (t.state>=2) ++finished;
    }
    state_->setText(QString("Hive progress: %1 of %2 tasks finished").arg(finished).arg(tasks_.size()));
}
void HiveWindow::finaliseSplit() {
    if (assembling_) return;
    for (const auto &t:tasks_) if (t.state!=2) {
        appendLog("Cannot assemble: at least one segment failed.");finishBatch();return;
    }
    assembling_=true;validationIndex_=0;combinedSegmentDuration_=0;
    firstCodec_.clear();firstWidth_.clear();firstHeight_.clear();firstFps_.clear();firstExtra_.clear();
    appendLog("Checking each video section for matching codec parameters, timing and resolution…");
    validateNextSegment();
}
void HiveWindow::validateNextSegment() {
    if (stopping_) {finishBatch();return;}
    if (validationIndex_>=tasks_.size()) {assembleValidatedSegments();return;}
    validator_.start(ffprobe_,{"-v","error","-select_streams","v:0",
        "-show_entries","stream=codec_name,width,height,avg_frame_rate,pix_fmt,profile,level,extradata_hash",
        "-show_entries","format=duration","-show_data_hash","sha256", "-of","json",
        tasks_.at(validationIndex_).task.output});
}
void HiveWindow::validationFinished(int code,QProcess::ExitStatus status) {
    if (!active_ || !assembling_ || stopping_) {schedule();return;}
    if (code!=0 || status!=QProcess::NormalExit) {
        appendLog("A video section failed FFprobe validation: "+QString::fromUtf8(validator_.readAllStandardError()).left(500));
        finishBatch();return;
    }
    const auto probeData=QJsonDocument::fromJson(validator_.readAllStandardOutput()).object();
    const auto streams=probeData.value("streams").toArray();
    if (streams.isEmpty()) {appendLog("Missing video track in encoded section");finishBatch();return;}
    const auto v=streams.first().toObject();
    const QString codec=v.value("codec_name").toString();
    const QString width=QString::number(v.value("width").toInt());
    const QString height=QString::number(v.value("height").toInt());
    const QString fps=v.value("avg_frame_rate").toString();
    const QString extra=v.value("extradata_hash").toString();
    if (codec.isEmpty() || width=="0" || height=="0" || fps.isEmpty()) {
        appendLog("Incomplete segment video metadata; refusing to merge");finishBatch();return;
    }
    if (firstCodec_.isEmpty()) {
        firstCodec_=codec;firstWidth_=width;firstHeight_=height;firstFps_=fps;firstExtra_=extra;
    } else if (firstCodec_!=codec || firstWidth_!=width || firstHeight_!=height || firstFps_!=fps ||
               (!firstExtra_.isEmpty() && !extra.isEmpty() && firstExtra_!=extra)) {
        appendLog("Encoded sections have incompatible codec headers/frame rates/resolutions; refusing merge.");
        finishBatch();return;
    }
    combinedSegmentDuration_+=probeData.value("format").toObject().value("duration").toString().toDouble();
    ++validationIndex_;
    state_->setText(QString("Validating %1 / %2 encoded sections…").arg(validationIndex_).arg(tasks_.size()));
    validateNextSegment();
}
void HiveWindow::assembleValidatedSegments() {
    const double original=sources_.first().info.duration;
    if (std::abs(combinedSegmentDuration_-original)>qMax(2.0,tasks_.size()*0.08)) {
        appendLog("Segment duration mismatch; refusing concatenation");finishBatch();return;
    }
    const QString manifest=QDir(scratch_->path()).filePath("concat.ffcat");
    QFile list(manifest);
    if (!list.open(QIODevice::WriteOnly)) {appendLog("Cannot write concat manifest");finishBatch();return;}
    for (const auto &t:tasks_) list.write("file '"+t.task.output.toUtf8()+"'\n");
    list.close();
    const QString tmp=splitOutput_+".harbinger-hive-part.mkv";
    if (QFileInfo::exists(tmp) || QFileInfo::exists(splitOutput_)) {
        appendLog("Output collision; refusing overwrite");finishBatch();return;
    }
    QStringList args={"-hide_banner","-nostdin","-n","-loglevel","warning",
        "-f","concat","-safe","0","-i",manifest,"-i",sources_.first().info.path,
        "-map","0:v:0","-c:v","copy","-map_metadata","1","-map_chapters","1"};
    // The host handles audio once, after video sections are reassembled.
    if (options_.audioAction != "remove" && sources_.first().info.hasAudio) {
        args << "-map" << (sources_.first().info.audioStreamIndex >= 0
            ? "1:" + QString::number(sources_.first().info.audioStreamIndex) : QString("1:a:0"));
        if (options_.audioAction == "copy") args << "-c:a" << "copy";
        else {
            args << Codec::audioEncodeArgs(options_, sources_.first().info);
        }
    }
    args << "-map" << "1:s?" << "-c:s" << "copy";
    for (const auto &s:sources_.first().info.streams) if (s.type=="subtitle" && s.codec=="mov_text") {
        args << "-c:s" << "srt";break;
    }
    args << tmp;
    appendLog("Joining video segments and processing original audio/subtitles…");
    state_->setText("Assembling final MKV…");
    assembler_.start(ffmpeg_,args);
}
void HiveWindow::finishBatch() {
    if (!active_) return;
    active_=false;assembling_=false;
    cancelButton_->setEnabled(false);runButton_->setEnabled(true);split_->setEnabled(!options_.smart);
    int succeeded=0,failed=0;
    for (const auto &t:tasks_) {if (t.state==2) ++succeeded;else ++failed;}
    state_->setText(QString("Hive finished: %1 succeeded, %2 failed%3").arg(succeeded).arg(failed)
        .arg(assembled_?" — final MKV assembled":""));
    appendLog(state_->text());
    for (auto *client:clients_) {client->disconnectWorker();client->deleteLater();}
    clients_.clear();inFlight_.clear();
    if (!stopping_ && splitMode_ && !assembled_) appendLog("Experimental split did NOT produce a final file. Inspect validation messages.");
}
void HiveWindow::appendLog(const QString &line) { log_->appendPlainText(line.left(1500)); }

void HiveWindow::closeEvent(QCloseEvent *event) {
    if (active_) {
        if (QMessageBox::question(this,"Cancel Hive encoding?",
            "Closing this window stops the distributed jobs. Cancel and close?",
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) {
            event->ignore();return;
        }
        stopping_=true;
        for (auto *c:clients_) c->disconnectWorker();
        localEngine_.cancel();localSegment_.kill();assembler_.kill();validator_.kill();
    }
    QDialog::closeEvent(event);
}
