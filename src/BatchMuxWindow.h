#pragma once
#include "BatchMuxPlan.h"
#include "MuxEngine.h"
#include <QDialog>
#include <QVector>
#include <memory>
#include <atomic>
class QTableWidget;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QThread;

class BatchMuxWindow final : public QDialog {
    Q_OBJECT
public:
    BatchMuxWindow(const QString &ffmpeg, const QString &ffprobe,
                   const MuxPlan &sample, QWidget *parent=nullptr);
    ~BatchMuxWindow() override;
protected:
    void reject() override;
private:
    void chooseFiles();
    void chooseFolder();
    void chooseOutput();
    void refreshSources();
    void invalidate();
    void prepare();
    void onPrepared(const QVector<BatchEpisode> &rows, const QString &error);
    void startBatch();
    void startNext();
    void finishedJob(bool success, bool cancelled, const QString &message, const QString &output);
    void cancelBatch();
    void setBusy();
    QString ffmpeg_,ffprobe_;
    MuxPlan sample_;
    QVector<BatchSourceGroup> groups_;
    QVector<BatchEpisode> rows_;
    std::shared_ptr<std::atomic_bool> stop_;
    MuxEngine engine_;
    int runningIndex_=-1;
    int finishedCount_=0, failedCount_=0;
    bool preparing_=false, running_=false, ready_=false;
    QThread *prepareThread_=nullptr;
    QTableWidget *sources_=nullptr,*preview_=nullptr;
    QLineEdit *output_=nullptr;
    QComboBox *reference_=nullptr;
    QCheckBox *waveform_=nullptr,*sorted_=nullptr;
    QPushButton *prepareButton_=nullptr,*startButton_=nullptr,*cancelButton_=nullptr;
    QLabel *status_=nullptr;
    QPlainTextEdit *log_=nullptr;
    QProgressBar *progress_=nullptr;
};
