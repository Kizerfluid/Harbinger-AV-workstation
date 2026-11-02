#pragma once
#include "Engine.h"
#include "Probe.h"
#include "Hardware.h"
#include "HiveWorker.h"
#include <QMainWindow>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QByteArray>

class QComboBox;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QCheckBox;
class QProgressBar;
class QPlainTextEdit;
class QProcess;
class QAction;
class QGroupBox;
class HardwareDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void buildMenus();
    void addFiles(const QStringList &paths);
    void selectFiles();
    void selectOutputFolder();
    void removeSelected();
    void clearCompleted();
    void refreshRow(int index);
    void refreshFileInfo();
    void startBitrateScan(const Job &job);
    void parseBitratePacketOutput(const QByteArray &chunk);
    void finishBitrateScan(bool success);
    void fillCodecCombos();
    void applyPreset();
    void fillSmartCodecs();
    void updateSmartUi();
    void refreshSettings();
    void updateActions();
    void detectEncoders();
    void chooseFFmpeg();
    void chooseFFprobe();
    void startEncoding();
    void openMuxWindow();
    void openHardwareDialog();
    void openHiveWindow();
    void scanHardware();
    void rebuildGpuCombos();
    void loadJobOverrides();
    void updateJobOverrides();
    void saveSettings();
    void restoreSettings();
    QString ffmpegPath() const;
    QString ffprobePath() const;
    EncodeOptions currentOptions() const;
    void setBusy(bool busy);
    void log(const QString &message);
    // ffprobe -show_packets is streamed, not buffered in memory.  A scan runs
    // only for the selected file and is cached by path/size/modification time.
    struct PacketBitrateStats {
        qint64 bytes = 0;
        double first = 0.0;
        double last = 0.0;
        bool hasTimes = false;
    };
    QProcess *bitrateScanProcess_ = nullptr;
    QString activeBitrateScanKey_;
    QByteArray bitrateScanBuffer_;
    QHash<int, PacketBitrateStats> bitrateScanStats_;
    MediaInfo bitrateScanInfo_;
    QHash<QString, QHash<int, qint64>> measuredBitrates_;
    QSet<QString> unsuccessfulBitrateScans_;
    QVector<Job> jobs_;
    QSet<QString> encoders_;
    Probe probe_;
    Engine engine_;
    HiveWorker hiveWorker_;
    Hardware::Report hardwareReport_;
    HardwareScanner *hardwareScanner_ = nullptr;
    HardwareDialog *hardwareDialog_ = nullptr;
    bool hardwareScanning_ = false;
    bool rescanAfterCurrent_ = false;
    QProcess *detection_ = nullptr;
    QString savedVideo_;
    QString lastVideoChoice_; // Restore codec/passthrough when switching back from audio-only output.
    QString savedGpu_;
    QString savedAudio_;
    QString savedSmartVideo_;
    QString savedSmartAudio_;
    QString customFFmpeg_;
    QString customFFprobe_;
    bool closeAfterStop_ = false;
    bool refreshing_ = false;
    bool applyingPreset_ = false;
    QString lastValidFrameRate_ = "0"; // Restore prior choice if Custom is cancelled.

    QTreeWidget *queue_ = nullptr;
    QTreeWidget *fileInfo_ = nullptr;
    QGroupBox *fileInfoGroup_ = nullptr;
    QComboBox *mode_ = nullptr;
    QGroupBox *smartGroup_ = nullptr;
    QDoubleSpinBox *smartTargetMB_ = nullptr;
    QComboBox *smartPriority_ = nullptr;
    QComboBox *smartSpeed_ = nullptr;
    QComboBox *smartVideo_ = nullptr;
    QComboBox *smartAudio_ = nullptr;
    QLabel *smartPreview_ = nullptr;
    QLabel *advancedPresetLabel_ = nullptr;
    QComboBox *preset_ = nullptr;
    QComboBox *container_ = nullptr;
    QComboBox *video_ = nullptr;
    QComboBox *audio_ = nullptr;
    QComboBox *quality_ = nullptr;
    QComboBox *speed_ = nullptr;
    QComboBox *resolution_ = nullptr;
    QComboBox *frameRate_ = nullptr;
    QComboBox *videoBitDepth_ = nullptr;
    QSpinBox *audioBitrate_ = nullptr;
    QSlider *audioBitrateSlider_ = nullptr;
    QWidget *audioBitrateRow_ = nullptr;
    QString configuredAudioBitrateCodec_;
    QComboBox *sampleRate_ = nullptr;
    QComboBox *channels_ = nullptr;
    QComboBox *encodeMode_ = nullptr;
    QComboBox *decodeMode_ = nullptr;
    QComboBox *gpu_ = nullptr;
    QCheckBox *cpuFallback_ = nullptr;
    QComboBox *jobEncode_ = nullptr;
    QComboBox *jobDecode_ = nullptr;
    QComboBox *jobGpu_ = nullptr;
    QGroupBox *jobOverridesGroup_ = nullptr;
    QGroupBox *hardwareGroup_ = nullptr;
    QCheckBox *metadata_ = nullptr;
    QLineEdit *outputFolder_ = nullptr;
    QLabel *backendLabel_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QLabel *queueCount_ = nullptr;
    QProgressBar *overallProgress_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QGroupBox *settingsGroup_ = nullptr;
    QPushButton *addButton_ = nullptr;
    QPushButton *removeButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QAction *ffmpegAction_ = nullptr;
    QAction *ffprobeAction_ = nullptr;
    QAction *hardwareAction_ = nullptr;
};
