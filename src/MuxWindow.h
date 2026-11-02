#pragma once
#include "Probe.h"
#include "MuxEngine.h"
#include "MuxPlan.h"
#include <QDialog>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;

class MuxWindow final : public QDialog {
    Q_OBJECT
public:
    explicit MuxWindow(const QString &ffmpeg, const QString &ffprobe, QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    void buildUi();
    void importFiles(const QStringList &paths);
    void pickFiles();
    void removeFile();
    void onProbed(const QString &path, const MediaInfo &info);
    void onSourceToggle(QTreeWidgetItem *item, int column);
    void onSelectedEdited(QTreeWidgetItem *item, int column);
    void syncOutputList(int preferredRow = -1);
    void shiftSelected(int direction);
    void autoAlignAudio();
    void autoSyncWaveforms();
    void openBatchMux();
    void resetAudioDelays();
    void removeTrack();
    void updateChapterChoices();
    void updateSuggestion();
    void updateSummary();
    MuxPlan currentPlan() const;
    void pickOutput();
    void startMux();
    void cancelMux();
    void setRunning(bool busy);
    QTreeWidgetItem *sourceRow(const QString &source, int index) const;
    QTreeWidgetItem *fileRow(const QString &source) const;
    QString ffmpeg_;
    QString ffprobe_;
    Probe probe_;
    MuxEngine engine_;
    QMap<QString, MediaInfo> sources_;
    QVector<MuxTrack> selected_;
    QStringList sourceOrder_;
    bool autoOutput_ = true;
    bool closeAfterCancel_ = false;
    QTreeWidget *sourcesTree_ = nullptr;
    QTreeWidget *selectedTree_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QPushButton *browseButton_ = nullptr;
    QPushButton *removeFileButton_ = nullptr;
    QPushButton *removeTrackButton_ = nullptr;
    QPushButton *upButton_ = nullptr;
    QPushButton *downButton_ = nullptr;
    QPushButton *alignAudioButton_ = nullptr;
    QPushButton *waveSyncButton_ = nullptr;
    QPushButton *batchButton_ = nullptr;
    bool analysingAudio_ = false;
    QPushButton *resetDelayButton_ = nullptr;
    QPushButton *muxButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLineEdit *outputField_ = nullptr;
    QComboBox *chapterSource_ = nullptr;
    QCheckBox *metadataCheck_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *status_ = nullptr;
    QPlainTextEdit *logs_ = nullptr;
};
