#pragma once
#include "HiveClient.h"
#include "HiveWorker.h"
#include "Engine.h"
#include <QDialog>
#include <QHash>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

class QUdpSocket;
class QTreeWidget;
class QCheckBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QComboBox;
class QLineEdit;
class QProgressBar;

class HiveWindow final : public QDialog {
    Q_OBJECT
public:
    explicit HiveWindow(HiveWorker *worker,const QString &ffmpeg,const QString &ffprobe,const QString &folder,
                        const QVector<EncodeTask> &sources,const EncodeOptions &options,
                        const Hardware::Report &hardware,QWidget *parent=nullptr);
    ~HiveWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    struct Item {
        HiveRemoteTask task;
        int state=0; // 0 waiting, 1 running, 2 success, 3 failed
        int tries=0;
        int progress=0;
        QString status;
    };
    void announceReceived();
    void connectNodes();
    void run();
    void schedule();
    int takeNext(bool forLocal,const QSet<QString> &encoders={});
    void startLocal(int index);
    void localFinished(int code,QProcess::ExitStatus status);
    void taskFinished(int index,bool success,const QString &reason,const QString &output);
    void refreshTasks();
    void finaliseSplit();
    void validateNextSegment();
    void validationFinished(int code,QProcess::ExitStatus status);
    void assembleValidatedSegments();
    void finishBatch();
    void appendLog(const QString &line);
    QString reserveOutput(const QString &source,const QString &ext="mkv") const;
    void updateSharing();
    bool prepareSplit();
    HiveWorker *worker_;
    QString ffmpeg_,ffprobe_,folder_;
    QVector<EncodeTask> sources_;
    EncodeOptions options_;
    Hardware::Report hardware_;
    Engine localEngine_;
    QProcess localSegment_;
    QProcess assembler_;
    QProcess validator_;
    std::unique_ptr<QTemporaryDir> scratch_;
    QList<Item> tasks_;
    QHash<QString,HiveClient*> clients_;
    QHash<HiveClient*,int> inFlight_;
    QHash<QString,QString> seenIp_;
    QUdpSocket *discovery_ = nullptr;
    QTreeWidget *nodes_=nullptr,*taskTree_=nullptr;
    QCheckBox *sharing_=nullptr,*local_=nullptr,*split_=nullptr,*shareGpu_=nullptr;
    QSpinBox *workerThreads_=nullptr;
    QLabel *identity_=nullptr,*state_=nullptr;
    QLineEdit *manualIp_=nullptr;
    QPlainTextEdit *log_=nullptr;
    QPushButton *runButton_=nullptr,*cancelButton_=nullptr;
    int localIndex_=-1;
    bool active_=false,stopping_=false,splitMode_=false,assembled_=false,assembling_=false;
    int validationIndex_=0;
    QString firstCodec_,firstWidth_,firstHeight_,firstFps_,firstExtra_;
    double combinedSegmentDuration_=0;
    QString splitOutput_;
};
