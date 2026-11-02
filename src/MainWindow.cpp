#include "MainWindow.h"
#include "Codec.h"
#include "SmartEncoding.h"
#include "AudioInfo.h"
#include "VideoInfo.h"
#include "MuxWindow.h"
#include "HardwareDialog.h"
#include "HiveWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QDragMoveEvent>
#include <QKeySequence>
#include <QHash>
#include <QLocale>
#include <QSize>
#include <QStandardItemModel>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

namespace {
class FileTree final : public QTreeWidget {
public:
    std::function<void(const QStringList &)> onFiles;
    explicit FileTree(QWidget *parent = nullptr) : QTreeWidget(parent) {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
    }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
        else QTreeWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
        else QTreeWidget::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        if (!event->mimeData()->hasUrls()) { QTreeWidget::dropEvent(event); return; }
        QStringList files;
        for (const QUrl &url : event->mimeData()->urls()) if (url.isLocalFile()) files << url.toLocalFile();
        if (onFiles) onFiles(files);
        event->acceptProposedAction();
    }
};

void setDataValue(QComboBox *box, const QString &value, const QString &fallback = {}) {
    int i = -1;
    for (int k = 0; k < box->count(); ++k) if (box->itemData(k).toString() == value) { i = k; break; }
    if (i < 0 && !fallback.isEmpty())
        for (int k = 0; k < box->count(); ++k) if (box->itemData(k).toString() == fallback) { i = k; break; }
    if (i >= 0) box->setCurrentIndex(i);
}
// Display-only track metadata. Never infer a language from the file name or
// audio content; the source stream's FFprobe language and title tags are used.
QString friendlyStreamLanguage(const QString &raw) {
    const QString tag = raw.trimmed().toLower();
    if (tag.isEmpty() || tag == "und" || tag == "unknown") return {};
    // Matroska frequently uses ISO 639-2 three-letter tags, whereas QLocale
    // recognises BCP 47 / two-letter language tags reliably.
    const QString base = tag.section('-', 0, 0).section('_', 0, 0);
    static const QHash<QString, QString> iso639_2 = {
        {"eng", "en"}, {"jpn", "ja"}, {"fra", "fr"}, {"fre", "fr"},
        {"deu", "de"}, {"ger", "de"}, {"spa", "es"}, {"ita", "it"},
        {"por", "pt"}, {"rus", "ru"}, {"kor", "ko"}, {"zho", "zh"},
        {"chi", "zh"}, {"ara", "ar"}, {"hin", "hi"}, {"nld", "nl"},
        {"dut", "nl"}, {"pol", "pl"}, {"swe", "sv"}, {"dan", "da"},
        {"nor", "no"}, {"fin", "fi"}, {"tur", "tr"}, {"ukr", "uk"},
        {"vie", "vi"}, {"tha", "th"}, {"ind", "id"}, {"ces", "cs"},
        {"cze", "cs"}, {"hun", "hu"}, {"ron", "ro"}, {"rum", "ro"},
        {"heb", "he"}, {"ell", "el"}, {"gre", "el"}, {"bul", "bg"},
        {"hrv", "hr"}, {"srp", "sr"}, {"slv", "sl"}, {"cat", "ca"},
        {"ben", "bn"}, {"fas", "fa"}, {"per", "fa"}, {"tam", "ta"},
        {"tel", "te"}, {"msa", "ms"}, {"may", "ms"}, {"fil", "fil"},
        {"afr", "af"}, {"est", "et"}, {"lit", "lt"}, {"lav", "lv"}
    };
    const QString normalized = iso639_2.value(base, base);
    const auto language = QLocale(normalized).language();
    if (language != QLocale::C && language != QLocale::AnyLanguage)
        return QLocale::languageToString(language);
    return base.toUpper();  // Unknown but explicitly tagged: show the tag.
}
QString streamName(const MediaStream &track) {
    const QString language = friendlyStreamLanguage(track.language);
    QString title = track.title.trimmed();
    title.replace('\n', ' ');
    title.replace('\r', ' ');
    if (language.isEmpty()) return title.isEmpty() ? (track.type == "video" ? "—" : "Not tagged") : title;
    if (title.isEmpty() || title.compare(language, Qt::CaseInsensitive) == 0)
        return language;
    if (title.startsWith(language + " ", Qt::CaseInsensitive) ||
        title.startsWith(language + " -", Qt::CaseInsensitive)) return title;
    return language + " — " + title;
}
QString subtitleFormat(const QString &name) {
    const QString codec = name.toLower();
    if (codec == "subrip") return "SubRip (SRT)";
    if (codec == "ass") return "ASS";
    if (codec == "ssa") return "SSA";
    if (codec == "webvtt") return "WebVTT";
    if (codec == "mov_text") return "Timed Text (MP4)";
    if (codec == "hdmv_pgs_subtitle") return "PGS (Blu-ray)";
    if (codec == "dvd_subtitle") return "VobSub (DVD)";
    if (codec == "dvb_subtitle") return "DVB subtitles";
    if (codec == "dvb_teletext") return "DVB teletext";
    if (codec == "eia_608") return "CEA-608 captions";
    if (codec == "arib_caption") return "ARIB captions";
    return name.isEmpty() ? "Not reported" : name.toUpper();
}
QString subtitleKind(const QString &name) {
    const QString codec = name.toLower();
    if (codec == "hdmv_pgs_subtitle" || codec == "dvd_subtitle" || codec == "dvb_subtitle" || codec == "xsub")
        return "Image subtitles";
    if (codec == "dvb_teletext" || codec == "eia_608" || codec == "arib_caption")
        return "Captions";
    if (codec == "subrip" || codec == "ass" || codec == "ssa" || codec == "webvtt" ||
        codec == "mov_text" || codec == "text" || codec == "ttml" || codec == "sami")
        return "Text subtitles";
    return "Subtitle track"; // Unknown format: do not guess text vs bitmap.
}
QString streamFlags(const MediaStream &track) {
    QStringList flags;
    if (track.isDefault) flags << "Default";
    if (track.isForced) flags << "Forced";
    return flags.join(" · ");
}
// A stable fingerprint prevents old measured results from being shown when an
// output path is overwritten by a subsequent encode.
QString bitrateCacheKey(const QString &path) {
    const QFileInfo file(path);
    return file.absoluteFilePath() + "|" + QString::number(file.size()) + "|" +
        QString::number(file.lastModified().toMSecsSinceEpoch());
}
QString streamInfoTip(const MediaStream &track, const QString &details) {
    QString tip = details + "\nLanguage: " +
        (friendlyStreamLanguage(track.language).isEmpty() ? "Not reported" : friendlyStreamLanguage(track.language));
    if (!track.language.isEmpty() && track.language != "und") tip += " (" + track.language + ")";
    tip += "\nTrack title: " + (track.title.isEmpty() ? "Not reported" : track.title);
    if (!streamFlags(track).isEmpty()) tip += "\nFlags: " + streamFlags(track);
    return tip;
}

// The two codec dropdowns are also the only place to choose stream operations.
// Real codecs imply transcoding; these two sentinels reuse the existing engine paths.
QString actionForCodecSelection(const QString &selection) {
    if (selection == "copy") return "copy";
    if (selection == "remove" || selection == "none") return "remove";
    return "transcode";
}
QString displayStreamChoice(const QString &selection, const QString &name) {
    const QString action = actionForCodecSelection(selection);
    if (action == "copy") return "Passthrough (original, no quality loss)";
    if (action == "remove") return "Removed";
    return "Transcode → " + name;
}
QString defaultAudioCodec(const QString &container) {
    if (container == "webm" || container == "ogg") return "libopus";
    if (container == "mp3") return "libmp3lame";
    if (container == "flac") return "flac";
    if (container == "wav") return "pcm_s16le";
    if (container == "ac3") return "ac3";
    if (container == "eac3") return "eac3";
    if (container == "dts") return "dca";
    return "aac";
}
QString niceStatus(JobState s) {
    switch (s) {
    case JobState::Probing: return "Inspecting…";
    case JobState::Ready: return "Ready";
    case JobState::Queued: return "Queued";
    case JobState::Encoding: return "Encoding";
    case JobState::Completed: return "Done";
    case JobState::Failed: return "Failed";
    case JobState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), probe_(this), engine_(this), hiveWorker_(this) {
    setWindowTitle("Harbinger — Video & Audio Encoder");
    setMinimumSize(980, 660);
    resize(1250, 820);
    setAcceptDrops(true);
    buildUi();
    buildMenus();

    // Run packet-size analysis in a separate ffprobe process. The stream of
    // packet records is consumed incrementally to avoid holding huge outputs.
    bitrateScanProcess_ = new QProcess(this);
    connect(bitrateScanProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        parseBitratePacketOutput(bitrateScanProcess_->readAllStandardOutput());
    });
    connect(bitrateScanProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            parseBitratePacketOutput(bitrateScanProcess_->readAllStandardOutput());
            parseBitratePacketOutput("\n"); // flush final unterminated record
            finishBitrateScan(code == 0 && status == QProcess::NormalExit);
        });
    connect(bitrateScanProcess_, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finishBitrateScan(false);
        });

    connect(&probe_, &Probe::probed, this, [this](const QString &path, const MediaInfo &info) {
        for (int i = 0; i < jobs_.size(); ++i) {
            if (jobs_[i].input == path && jobs_[i].state == JobState::Probing) {
                jobs_[i].info = info;
                jobs_[i].state = info.error.isEmpty() ? JobState::Ready : JobState::Failed;
                jobs_[i].error = info.error;
                refreshRow(i);
            }
        }
        refreshFileInfo();
        refreshSettings();
        updateActions();
    });
    connect(&engine_, &Engine::jobStarted, this, [this](int i, const QString &output) {
        jobs_[i].state = JobState::Encoding;
        jobs_[i].output = output;
        refreshRow(i);
        statusBar()->showMessage("Encoding " + QFileInfo(jobs_[i].input).fileName());
    });
    connect(&engine_, &Engine::jobProgress, this, [this](int i, int progress) {
        jobs_[i].progress = progress;
        refreshRow(i);
    });
    connect(&engine_, &Engine::jobFinished, this,
        [this](int i, bool success, bool cancelled, const QString &error, const QString &output) {
            jobs_[i].state = success ? JobState::Completed : cancelled ? JobState::Cancelled : JobState::Failed;
            jobs_[i].progress = success ? 100 : jobs_[i].progress;
            jobs_[i].error = error;
            jobs_[i].output = output;
            refreshRow(i);
            if (success) log("Completed: " + output);
            else log((cancelled ? "Cancelled: " : "Failed: ") + QFileInfo(jobs_[i].input).fileName() + " — " + error);
        });
    connect(&engine_, &Engine::queueFinished, this, [this] {
        for (int i = 0; i < jobs_.size(); ++i) {
            if (jobs_[i].state == JobState::Queued) { jobs_[i].state = JobState::Ready; refreshRow(i); }
        }
        setBusy(false);
        refreshFileInfo(); // Start bitrate scan if it was deferred during encoding.
        statusBar()->showMessage("Queue finished. Ready for more files.");
        saveSettings();
        if (closeAfterStop_) QTimer::singleShot(0, this, [this] { close(); });
    });
    connect(&engine_, &Engine::logLine, this, &MainWindow::log);
    restoreSettings();
    hiveWorker_.setFFmpeg(ffmpegPath());
    detectEncoders();
}
MainWindow::~MainWindow() {
    if (bitrateScanProcess_ && bitrateScanProcess_->state() != QProcess::NotRunning) {
        bitrateScanProcess_->kill();
        bitrateScanProcess_->waitForFinished(3000);
    }
    if (hardwareScanner_) {
        hardwareScanner_->cancel();
        hardwareScanner_->wait(); // never destroy a running QThread
    }
}

void MainWindow::buildUi() {
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(20, 14, 20, 14);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *headings = new QVBoxLayout;
    auto *appName = new QLabel("Harbinger");
    appName->setObjectName("brand");
    headings->addWidget(appName);
    auto *tagline = new QLabel("Encode video and audio without the command line");
    tagline->setObjectName("muted");
    headings->addWidget(tagline);
    header->addLayout(headings);
    header->addStretch();
    backendLabel_ = new QLabel("Checking FFmpeg…");
    backendLabel_->setObjectName("backend");
    backendLabel_->setToolTip("Click Tools → Select FFmpeg / FFprobe to configure the backend.");
    header->addWidget(backendLabel_);
    layout->addLayout(header);

    auto *tools = new QHBoxLayout;
    addButton_ = new QPushButton("+ Add files");
    addButton_->setObjectName("primarySubtle");
    auto *muxLauncher = new QPushButton("Mux / combine MKV tracks");
    auto *hiveLauncher = new QPushButton("Harbinger Hive");
    hiveLauncher->setObjectName("primarySubtle");
    muxLauncher->setObjectName("primarySubtle");
    removeButton_ = new QPushButton("Remove selected");
    clearButton_ = new QPushButton("Clear finished");
    tools->addWidget(addButton_);
    tools->addWidget(muxLauncher);
    tools->addWidget(hiveLauncher);
    tools->addWidget(removeButton_);
    tools->addWidget(clearButton_);
    tools->addStretch();
    queueCount_ = new QLabel("0 files");
    queueCount_->setObjectName("muted");
    tools->addWidget(queueCount_);
    layout->addLayout(tools);
    connect(addButton_, &QPushButton::clicked, this, &MainWindow::selectFiles);
    connect(muxLauncher, &QPushButton::clicked, this, &MainWindow::openMuxWindow);
    connect(hiveLauncher, &QPushButton::clicked, this, &MainWindow::openHiveWindow);
    connect(removeButton_, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(clearButton_, &QPushButton::clicked, this, &MainWindow::clearCompleted);
    // The remove button follows the selection immediately, not just when a file is added.
    // The QTreeWidget is created below, so the connection is made after its construction.

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *section = new QLabel("ENCODING QUEUE");
    section->setObjectName("sectionLabel");
    leftLayout->addWidget(section);
    auto *fileTree = new FileTree;
    fileTree->onFiles = [this](const QStringList &paths) { if (!engine_.running()) addFiles(paths); };
    queue_ = fileTree;
    // Keep the queue focused on actions/progress. Detailed stream metadata lives
    // in the File Information panel below and no longer duplicates columns here.
    queue_->setColumnCount(4);
    queue_->setHeaderLabels({"File", "Duration", "Status", "Progress"});
    queue_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    queue_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    queue_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    queue_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    queue_->header()->resizeSection(3, 110);
    queue_->setAlternatingRowColors(true);
    queue_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    queue_->setRootIsDecorated(false);
    queue_->setUniformRowHeights(false);
    leftLayout->addWidget(queue_);
    connect(queue_, &QTreeWidget::itemSelectionChanged, this, [this] {
        loadJobOverrides();
        refreshFileInfo();
        updateActions();
    });
    fileInfoGroup_ = new QGroupBox("FILE INFORMATION — SELECT A FILE");
    auto *infoLayout = new QVBoxLayout(fileInfoGroup_);
    fileInfo_ = new QTreeWidget;
    fileInfo_->setColumnCount(6);
    fileInfo_->setHeaderLabels({"Track", "Language / name", "Codec / profile", "Bitrate", "Bit depth", "Details"});
    fileInfo_->setRootIsDecorated(false);
    fileInfo_->setSelectionMode(QAbstractItemView::NoSelection);
    fileInfo_->setAlternatingRowColors(true);
    fileInfo_->setMinimumHeight(106);
    fileInfo_->setMaximumHeight(205);
    fileInfo_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fileInfo_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    fileInfo_->header()->resizeSection(1, 172);
    fileInfo_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    fileInfo_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    fileInfo_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    fileInfo_->header()->setSectionResizeMode(5, QHeaderView::Interactive);
    fileInfo_->header()->resizeSection(5, 188);
    infoLayout->setContentsMargins(6, 8, 6, 6);
    infoLayout->addWidget(fileInfo_);
    leftLayout->addWidget(fileInfoGroup_);
    refreshFileInfo();
    auto *dropHint = new QLabel("Drop video or audio files here, or use Add files");
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setObjectName("muted");
    leftLayout->addWidget(dropHint);
    splitter->addWidget(left);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(335);
    auto *settingsWidget = new QWidget;
    auto *side = new QVBoxLayout(settingsWidget);
    side->setContentsMargins(6, 0, 2, 0);
    auto *rightLabel = new QLabel("OUTPUT SETTINGS");
    rightLabel->setObjectName("sectionLabel");
    side->addWidget(rightLabel);
    auto *modeLabel = new QLabel("ENCODING MODE");
    modeLabel->setObjectName("sectionLabel");
    side->addWidget(modeLabel);
    mode_ = new QComboBox;
    mode_->addItem("Smart Encoding · let Harbinger decide", "smart");
    mode_->addItem("Advanced Encoding · full manual control", "advanced");
    side->addWidget(mode_);
    advancedPresetLabel_ = new QLabel("QUICK PRESET");
    advancedPresetLabel_->setObjectName("sectionLabel");
    side->addWidget(advancedPresetLabel_);
    preset_ = new QComboBox;
    preset_->addItem("Custom settings", "custom");
    preset_->addItem("Everyday MP4 — H.264 + AAC", "mp4_standard");
    preset_->addItem("Smaller MKV — H.265 + AAC", "mkv_hevc");
    preset_->addItem("Web video — VP9 + Opus", "webm_vp9");
    preset_->addItem("Modern AV1 — AV1 + Opus", "mkv_av1");
    preset_->addItem("Audio — MP3", "audio_mp3");
    preset_->addItem("Audio — Opus", "audio_opus");
    preset_->addItem("Lossless audio — FLAC", "audio_flac");
    side->addWidget(preset_);
    settingsGroup_ = new QGroupBox("Format & codecs");
    auto *form = new QFormLayout(settingsGroup_);
    form->setSpacing(10);
    container_ = new QComboBox;
    for (const QString &c : Codec::containers()) container_->addItem(c.toUpper(), c);
    video_ = new QComboBox;
    audio_ = new QComboBox;
    video_->setToolTip("Choose a video codec to transcode, or Passthrough / Remove to copy or omit the source video.");
    audio_->setToolTip("Choose an audio codec to transcode, or Passthrough / Remove to copy or omit the source audio.");
    quality_ = new QComboBox;
    quality_->addItems({"Maximum quality", "High quality", "Balanced (recommended)", "Smaller files"});
    quality_->setCurrentIndex(2);
    speed_ = new QComboBox;
    speed_->addItems({"Fast", "Balanced", "Slow (better compression)"});
    speed_->setCurrentIndex(1);
    resolution_ = new QComboBox;
    resolution_->addItem("Keep original", 0);
    for (int h : {2160, 1440, 1080, 720, 480, 360}) resolution_->addItem(QString::number(h) + "p", h);
    resolution_->setToolTip("Resize to the selected height, preserving aspect ratio. Smaller files may be upscaled.");
    frameRate_ = new QComboBox;
    frameRate_->addItem("Keep original", "0");
    frameRate_->addItem("23.976 fps (film)", "24000/1001");
    frameRate_->addItem("24 fps", "24");
    frameRate_->addItem("25 fps", "25");
    frameRate_->addItem("29.970 fps (NTSC)", "30000/1001");
    frameRate_->addItem("30 fps", "30");
    frameRate_->addItem("50 fps", "50");
    frameRate_->addItem("59.940 fps (NTSC)", "60000/1001");
    frameRate_->addItem("60 fps", "60");
    frameRate_->addItem("120 fps", "120");
    frameRate_->addItem("Custom frame rate…", "custom");
    frameRate_->setToolTip("Choose output frame rate. Keep original avoids frame duplication/drop. Custom accepts 1–240 fps.");
    videoBitDepth_ = new QComboBox;
    videoBitDepth_->addItem("Automatic (legacy: 8-bit)", 0);
    videoBitDepth_->addItem("8-bit (yuv420p / NV12)", 8);
    videoBitDepth_->addItem("10-bit (yuv420p10le / P010)", 10);
    videoBitDepth_->setToolTip("10-bit output is available for HEVC, AV1 and VP9 when supported by the selected FFmpeg encoder. Does not affect passthrough or Smart Encoding.");
    audioBitrateRow_ = new QWidget;
    auto *bitrateLayout = new QHBoxLayout(audioBitrateRow_);
    bitrateLayout->setContentsMargins(0, 0, 0, 0);
    bitrateLayout->setSpacing(8);
    audioBitrateSlider_ = new QSlider(Qt::Horizontal);
    audioBitrateSlider_->setToolTip("Adjust the audio encoder's target bitrate.");
    audioBitrate_ = new QSpinBox;
    audioBitrate_->setRange(6, 1536);
    audioBitrate_->setValue(192);
    audioBitrate_->setSuffix(" kbps");
    audioBitrate_->setKeyboardTracking(false); // Commit a typed number rather than each intermediate digit.
    audioBitrate_->setMinimumWidth(105);
    bitrateLayout->addWidget(audioBitrateSlider_, 1);
    bitrateLayout->addWidget(audioBitrate_);
    sampleRate_ = new QComboBox;
    sampleRate_->addItem("Automatic", 0);
    sampleRate_->addItem("44.1 kHz", 44100);
    sampleRate_->addItem("48 kHz", 48000);
    channels_ = new QComboBox;
    channels_->addItem("Original", 0);
    channels_->addItem("Mono", 1);
    channels_->addItem("Stereo", 2);
    channels_->addItem("5.1 surround", 6);
    channels_->addItem("7.1 surround", 8);
    channels_->setToolTip("Original preserves source channels when supported; otherwise downmixes to the selected codec’s limit. 5.1 and 7.1 use standard channel layouts.");
    metadata_ = new QCheckBox("Preserve source metadata");
    metadata_->setChecked(true);
    form->addRow("Container", container_);
    form->addRow("Video codec", video_);
    form->addRow("Audio codec", audio_);
    form->addRow("Video quality", quality_);
    form->addRow("Encoding speed", speed_);
    form->addRow("Resolution", resolution_);
    form->addRow("Frame rate", frameRate_);
    form->addRow("Video bit depth", videoBitDepth_);
    form->addRow("Audio target bitrate", audioBitrateRow_);
    form->addRow("Sample rate", sampleRate_);
    form->addRow("Channels", channels_);
    form->addRow(metadata_);
    side->addWidget(settingsGroup_);

    hardwareGroup_ = new QGroupBox("Hardware acceleration");
    auto *hardwareForm = new QFormLayout(hardwareGroup_);
    encodeMode_ = new QComboBox;
    encodeMode_->addItem("Automatic · quality-aware", "auto");
    encodeMode_->addItem("GPU · hardware encoding", "gpu");
    encodeMode_->addItem("CPU · software encoding", "cpu");
    decodeMode_ = new QComboBox;
    decodeMode_->addItem("Automatic · tested GPU when available", "auto");
    decodeMode_->addItem("GPU · hardware decoding", "gpu");
    decodeMode_->addItem("CPU · software decoding", "cpu");
    gpu_ = new QComboBox;
    gpu_->addItem("Automatic · any compatible GPU", "");
    cpuFallback_ = new QCheckBox("Retry on CPU if hardware fails");
    cpuFallback_->setChecked(true);
    cpuFallback_->setToolTip("Retry with CPU decoding and/or an equivalent software encoder. The output codec family stays the same.");
    auto *hardwareButton = new QPushButton("View GPU codec capabilities…");
    hardwareForm->addRow("Encoding", encodeMode_);
    hardwareForm->addRow("Decoding", decodeMode_);
    hardwareForm->addRow("GPU", gpu_);
    hardwareForm->addRow(cpuFallback_);
    hardwareForm->addRow(hardwareButton);
    side->addWidget(hardwareGroup_);
    connect(hardwareButton, &QPushButton::clicked, this, &MainWindow::openHardwareDialog);
    for (QComboBox *c : {encodeMode_, decodeMode_, gpu_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refreshSettings);
    connect(cpuFallback_, &QCheckBox::toggled, this, &MainWindow::refreshSettings);

    jobOverridesGroup_ = new QGroupBox("Per-file acceleration override");
    auto *jobForm = new QFormLayout(jobOverridesGroup_);
    jobEncode_ = new QComboBox;
    jobEncode_->addItem("Use global setting", "");
    jobEncode_->addItem("Automatic", "auto");
    jobEncode_->addItem("GPU", "gpu");
    jobEncode_->addItem("CPU", "cpu");
    jobDecode_ = new QComboBox;
    jobDecode_->addItem("Use global setting", "");
    jobDecode_->addItem("Automatic", "auto");
    jobDecode_->addItem("GPU", "gpu");
    jobDecode_->addItem("CPU", "cpu");
    jobGpu_ = new QComboBox;
    jobGpu_->addItem("Use global setting", "");
    jobGpu_->addItem("Any GPU (override)", "default");
    jobForm->addRow("Encode", jobEncode_);
    jobForm->addRow("Decode", jobDecode_);
    jobForm->addRow("GPU", jobGpu_);
    side->addWidget(jobOverridesGroup_);
    for (QComboBox *c : {jobEncode_, jobDecode_, jobGpu_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateJobOverrides);
    jobOverridesGroup_->setEnabled(false);

    smartGroup_ = new QGroupBox("Smart Encoding");
    auto *smartForm = new QFormLayout(smartGroup_);
    smartForm->setSpacing(11);
    smartTargetMB_ = new QDoubleSpinBox;
    smartTargetMB_->setDecimals(1);
    smartTargetMB_->setRange(0.0, 100000.0);
    smartTargetMB_->setSingleStep(25.0);
    smartTargetMB_->setSuffix(" MB");
    smartTargetMB_->setSpecialValueText("No size target");
    smartTargetMB_->setToolTip("Approximate desired size of EACH output file, not the whole batch. "
        "No size target chooses quality-controlled encoding instead.");
    smartPriority_ = new QComboBox;
    smartPriority_->addItem("Balanced · video and audio", 1);
    smartPriority_->addItem("Visual Quality · favour picture", 0);
    smartPriority_->addItem("Audio Quality · favour sound", 2);
    smartSpeed_ = new QComboBox;
    smartSpeed_->addItem("Balanced · automatic acceleration", 1);
    smartSpeed_->addItem("Fast · favour available GPU", 0);
    smartSpeed_->addItem("Slow · favour CPU compression", 2);
    smartVideo_ = new QComboBox;
    smartAudio_ = new QComboBox;
    smartPreview_ = new QLabel("Add a file to see how Harbinger would encode it.");
    smartPreview_->setWordWrap(true);
    smartPreview_->setObjectName("muted");
    smartForm->addRow("Target file size", smartTargetMB_);
    smartForm->addRow("Priority", smartPriority_);
    smartForm->addRow("Encoding speed", smartSpeed_);
    smartForm->addRow("Video codec", smartVideo_);
    smartForm->addRow("Audio codec", smartAudio_);
    smartForm->addRow(smartPreview_);
    side->addWidget(smartGroup_);

    auto *outputGroup = new QGroupBox("Destination");
    auto *destLayout = new QVBoxLayout(outputGroup);
    auto *destinationRow = new QHBoxLayout;
    outputFolder_ = new QLineEdit;
    outputFolder_->setPlaceholderText("Output directory");
    auto *browse = new QPushButton("Browse…");
    destinationRow->addWidget(outputFolder_, 1);
    destinationRow->addWidget(browse);
    destLayout->addLayout(destinationRow);
    auto *outputHint = new QLabel("Files are named *_encoded.*. Existing files are never overwritten.");
    outputHint->setWordWrap(true);
    outputHint->setObjectName("muted");
    destLayout->addWidget(outputHint);
    side->addWidget(outputGroup);
    connect(browse, &QPushButton::clicked, this, &MainWindow::selectOutputFolder);
    connect(outputFolder_, &QLineEdit::textChanged, this, &MainWindow::updateActions);

    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setObjectName("summary");
    side->addWidget(summaryLabel_);
    hintLabel_ = new QLabel;
    hintLabel_->setWordWrap(true);
    hintLabel_->setObjectName("muted");
    side->addWidget(hintLabel_);
    side->addStretch(1);
    scroll->setWidget(settingsWidget);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({760, 400});
    layout->addWidget(splitter, 1);

    auto *actions = new QHBoxLayout;
    overallProgress_ = new QProgressBar;
    overallProgress_->setRange(0, 100);
    overallProgress_->setValue(0);
    overallProgress_->setTextVisible(true);
    actions->addWidget(overallProgress_, 1);
    startButton_ = new QPushButton("Start processing");
    startButton_->setObjectName("primary");
    stopButton_ = new QPushButton("Cancel");
    stopButton_->setEnabled(false);
    actions->addWidget(startButton_);
    actions->addWidget(stopButton_);
    layout->addLayout(actions);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startEncoding);
    connect(stopButton_, &QPushButton::clicked, this, [this] { log("Cancellation requested…"); stopButton_->setEnabled(false); engine_.cancel(); });

    auto *logRow = new QHBoxLayout;
    auto *logTitle = new QLabel("ACTIVITY LOG");
    logTitle->setObjectName("sectionLabel");
    logRow->addWidget(logTitle);
    logRow->addStretch();
    auto *showLog = new QCheckBox("Show details");
    logRow->addWidget(showLog);
    layout->addLayout(logRow);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    logView_->setFixedHeight(130);
    logView_->setVisible(false);
    layout->addWidget(logView_);
    connect(showLog, &QCheckBox::toggled, logView_, &QWidget::setVisible);
    setCentralWidget(root);
    statusBar()->showMessage("Add files to get started.");

    connect(preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applyPreset);
    connect(container_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        fillCodecCombos();
        if (!applyingPreset_) preset_->setCurrentIndex(0);
    });
    connect(audio_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (!refreshing_ && !applyingPreset_) preset_->setCurrentIndex(0);
        refreshSettings();
    });
    connect(video_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (!refreshing_ && !applyingPreset_) preset_->setCurrentIndex(0);
        refreshSettings();
    });
    for (QComboBox *c : {quality_, speed_, resolution_, sampleRate_, channels_, videoBitDepth_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            if (!applyingPreset_ && !refreshing_) preset_->setCurrentIndex(0);
            refreshSettings();
            updateActions();
        });
    connect(frameRate_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && frameRate_->itemData(index).toString() == "custom") {
            bool ok = false;
            const double requested = QInputDialog::getDouble(this, "Custom output frame rate",
                "Frames per second (1–240):", 23.976, 1.0, 240.0, 3, &ok);
            if (ok) {
                const QString value = QString::number(requested, 'f', 3);
                const QSignalBlocker blocker(frameRate_);
                int customIndex = frameRate_->findData(value);
                if (customIndex < 0) {
                    customIndex = frameRate_->count() - 1;
                    frameRate_->insertItem(customIndex, value + " fps (custom)", value);
                }
                frameRate_->setCurrentIndex(customIndex);
            } else {
                const QSignalBlocker blocker(frameRate_);
                const int previous = frameRate_->findData(lastValidFrameRate_);
                frameRate_->setCurrentIndex(previous >= 0 ? previous : 0);
            }
        }
        lastValidFrameRate_ = frameRate_->currentData().toString();
        if (!applyingPreset_ && !refreshing_) preset_->setCurrentIndex(0);
        refreshSettings();
        updateActions();
    });
    connect(audioBitrateSlider_, &QSlider::valueChanged, this, [this](int position) {
        const auto spec = Codec::audioBitrateSpec(audio_->currentData().toString());
        if (!spec.enabled()) return;
        const int target = spec.discrete.isEmpty() ? position : spec.discrete.value(position, spec.minimum);
        const QSignalBlocker blocked(audioBitrate_);
        audioBitrate_->setValue(target);
        if (!applyingPreset_ && !refreshing_) preset_->setCurrentIndex(0);
        refreshSettings();
    });
    connect(audioBitrate_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int typed) {
        const QString encoder = audio_->currentData().toString();
        const auto spec = Codec::audioBitrateSpec(encoder);
        if (!spec.enabled()) return;
        const int target = Codec::normaliseAudioBitrate(encoder, typed);
        const QSignalBlocker blockedSpin(audioBitrate_);
        const QSignalBlocker blockedSlider(audioBitrateSlider_);
        audioBitrate_->setValue(target);
        audioBitrateSlider_->setValue(spec.discrete.isEmpty() ? target : spec.discrete.indexOf(target));
        if (!applyingPreset_ && !refreshing_) preset_->setCurrentIndex(0);
        refreshSettings();
    });
    connect(metadata_, &QCheckBox::toggled, this, [this] {
        if (!applyingPreset_ && !refreshing_) preset_->setCurrentIndex(0);
        updateActions();
    });
    connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSmartUi(); });
    connect(smartTargetMB_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { refreshSettings(); });
    for (QComboBox *c : {smartPriority_, smartSpeed_, smartVideo_, smartAudio_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSettings(); });
    updateSmartUi();
    refreshSettings();
}

void MainWindow::buildMenus() {
    auto *fileMenu = menuBar()->addMenu("&File");
    auto *add = fileMenu->addAction("Add media files…");
    add->setShortcut(QKeySequence::Open);
    connect(add, &QAction::triggered, this, &MainWindow::selectFiles);
    auto *openOutput = fileMenu->addAction("Open output folder");
    connect(openOutput, &QAction::triggered, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder_->text()));
    });
    fileMenu->addSeparator();
    connect(fileMenu->addAction("Exit"), &QAction::triggered, this, &QWidget::close);
    auto *toolMenu = menuBar()->addMenu("&Tools");
    connect(toolMenu->addAction("MKV Track Muxer…"), &QAction::triggered, this, &MainWindow::openMuxWindow);
    connect(toolMenu->addAction("Harbinger Hive…"), &QAction::triggered, this, &MainWindow::openHiveWindow);
    hardwareAction_ = toolMenu->addAction("Hardware Capabilities…");
    connect(hardwareAction_, &QAction::triggered, this, &MainWindow::openHardwareDialog);
    toolMenu->addSeparator();
    ffmpegAction_ = toolMenu->addAction("Select FFmpeg executable…");
    ffprobeAction_ = toolMenu->addAction("Select FFprobe executable…");
    connect(ffmpegAction_, &QAction::triggered, this, &MainWindow::chooseFFmpeg);
    connect(ffprobeAction_, &QAction::triggered, this, &MainWindow::chooseFFprobe);
    connect(menuBar()->addMenu("&Help")->addAction("About Harbinger"), &QAction::triggered, this, [this] {
        QMessageBox::about(this, "About Harbinger",
            "Harbinger 0.6.0\n\nA cross-platform FFmpeg frontend built with C++17 and Qt 6.\n\n"
            "Supports video/audio encoding and multi-source Matroska track muxing using FFmpeg and FFprobe.\n"
            "Encoders vary by FFmpeg build and available hardware.\n\n"
            "This is an early application build, not a production release.");
    });
}

QString MainWindow::ffmpegPath() const {
    if (!customFFmpeg_.isEmpty()) return customFFmpeg_;
    const QString local = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        "ffmpeg.exe"
#else
        "ffmpeg"
#endif
    );
    return QFileInfo::exists(local) ? local : "ffmpeg";
}
QString MainWindow::ffprobePath() const {
    if (!customFFprobe_.isEmpty()) return customFFprobe_;
    const QString local = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        "ffprobe.exe"
#else
        "ffprobe"
#endif
    );
    return QFileInfo::exists(local) ? local : "ffprobe";
}
void MainWindow::restoreSettings() {
    QSettings prefs("Harbinger", "Harbinger");
    // One-time preference migration for users upgrading from StreamForge v0.2.
    // Preserve any preferences already written under the new application name.
    if (prefs.allKeys().isEmpty()) {
        QSettings legacy("StreamForge", "StreamForge");
        for (const QString &key : legacy.allKeys())
            prefs.setValue(key, legacy.value(key));
        prefs.sync();
    }
    customFFmpeg_ = prefs.value("backend/ffmpeg").toString();
    customFFprobe_ = prefs.value("backend/ffprobe").toString();
    savedVideo_ = prefs.value("encoding/videoCodec").toString();
    const QString migratedCodec = Hardware::codecForEncoder(savedVideo_);
    if (!migratedCodec.isEmpty()) savedVideo_ = migratedCodec;
    savedAudio_ = prefs.value("encoding/audioCodec").toString();
    if (savedAudio_ == "none") savedAudio_ = "remove";
    // Migration from v0.4.1's separate Stream action + Codec dropdowns.
    // Newly saved v0.4.2 settings no longer contain these legacy action keys.
    const QString oldVideoAction = prefs.value("encoding/videoAction").toString();
    const QString oldAudioAction = prefs.value("encoding/audioAction").toString();
    if (oldVideoAction == "copy" || oldVideoAction == "remove") savedVideo_ = oldVideoAction;
    if (oldAudioAction == "copy" || oldAudioAction == "remove") savedAudio_ = oldAudioAction;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty() || !QDir(defaultDir).exists()) defaultDir = QDir::homePath();
    outputFolder_->setText(prefs.value("output/folder", defaultDir).toString());
    setDataValue(container_, prefs.value("encoding/container", "mp4").toString(), "mp4");
    quality_->setCurrentIndex(qBound(0, prefs.value("encoding/quality", 2).toInt(), 3));
    speed_->setCurrentIndex(qBound(0, prefs.value("encoding/speed", 1).toInt(), 2));
    setDataValue(resolution_, prefs.value("encoding/height", 0).toString(), "0");
    const QString savedFps = prefs.value("encoding/frameRate", "0").toString();
    if (savedFps != "custom" && frameRate_->findData(savedFps) < 0) {
        bool validFps = false;
        const double value = savedFps.toDouble(&validFps);
        if (validFps && value >= 1.0 && value <= 240.0)
            frameRate_->insertItem(frameRate_->count() - 1, savedFps + " fps (custom)", savedFps);
    }
    setDataValue(frameRate_, savedFps, "0");
    setDataValue(videoBitDepth_, prefs.value("encoding/videoBitDepth", 0).toString(), "0");
    // Retain existing v0.5.1 integer preference; the slider will be configured
    // for the chosen codec after FFmpeg encoder discovery completes.
    {
        const QSignalBlocker blocked(audioBitrate_);
        audioBitrate_->setValue(prefs.value("encoding/audioRate", 192).toInt());
    }
    setDataValue(sampleRate_, prefs.value("encoding/sampleRate", 0).toString(), "0");
    setDataValue(channels_, prefs.value("encoding/channels", 0).toString(), "0");
    metadata_->setChecked(prefs.value("encoding/metadata", true).toBool());
    setDataValue(encodeMode_, prefs.value("acceleration/encode", "auto").toString(), "auto");
    setDataValue(decodeMode_, prefs.value("acceleration/decode", "auto").toString(), "auto");
    savedGpu_ = prefs.value("acceleration/gpu").toString();
    cpuFallback_->setChecked(prefs.value("acceleration/fallback", true).toBool());
    const bool hasPreviousSettings = prefs.contains("encoding/container") || prefs.contains("encoding/audioCodec");
    setDataValue(mode_, prefs.value("smart/mode", hasPreviousSettings ? "advanced" : "smart").toString(), "smart");
    smartTargetMB_->setValue(prefs.value("smart/targetMB", 0.0).toDouble());
    setDataValue(smartPriority_, prefs.value("smart/priority", "1").toString(), "1");
    setDataValue(smartSpeed_, prefs.value("smart/speed", "1").toString(), "1");
    savedSmartVideo_ = prefs.value("smart/video", "auto").toString();
    savedSmartAudio_ = prefs.value("smart/audio", "auto").toString();
    fillSmartCodecs();
    updateSmartUi();
    probe_.setExecutable(ffprobePath());
}
void MainWindow::saveSettings() {
    QSettings prefs("Harbinger", "Harbinger");
    prefs.setValue("backend/ffmpeg", customFFmpeg_);
    prefs.setValue("backend/ffprobe", customFFprobe_);
    prefs.setValue("output/folder", outputFolder_->text());
    prefs.setValue("encoding/container", container_->currentData());
    prefs.setValue("encoding/videoCodec", video_->currentData());
    prefs.setValue("encoding/audioCodec", audio_->currentData());
    // Action is derived from codec selection; remove obsolete v0.4.1 keys after migration.
    prefs.remove("encoding/videoAction");
    prefs.remove("encoding/audioAction");
    prefs.setValue("encoding/quality", quality_->currentIndex());
    prefs.setValue("encoding/speed", speed_->currentIndex());
    prefs.setValue("encoding/height", resolution_->currentData());
    prefs.setValue("encoding/frameRate", frameRate_->currentData());
    prefs.setValue("encoding/videoBitDepth", videoBitDepth_->currentData());
    prefs.setValue("encoding/audioRate", audioBitrate_->value());
    prefs.setValue("encoding/sampleRate", sampleRate_->currentData());
    prefs.setValue("encoding/channels", channels_->currentData());
    prefs.setValue("encoding/metadata", metadata_->isChecked());
    prefs.setValue("acceleration/encode", encodeMode_->currentData());
    prefs.setValue("acceleration/decode", decodeMode_->currentData());
    prefs.setValue("acceleration/gpu", gpu_->currentData());
    prefs.setValue("acceleration/fallback", cpuFallback_->isChecked());
    prefs.setValue("smart/mode", mode_->currentData());
    prefs.setValue("smart/targetMB", smartTargetMB_->value());
    prefs.setValue("smart/priority", smartPriority_->currentData());
    prefs.setValue("smart/speed", smartSpeed_->currentData());
    prefs.setValue("smart/video", smartVideo_->currentData());
    prefs.setValue("smart/audio", smartAudio_->currentData());
}
void MainWindow::chooseFFmpeg() {
    const QString file = QFileDialog::getOpenFileName(this, "Select FFmpeg executable", {}, "Executable (*)");
    if (file.isEmpty()) return;
    customFFmpeg_ = file;
    hiveWorker_.setFFmpeg(ffmpegPath());
    saveSettings();
    detectEncoders();
}
void MainWindow::chooseFFprobe() {
    const QString file = QFileDialog::getOpenFileName(this, "Select FFprobe executable", {}, "Executable (*)");
    if (file.isEmpty()) return;
    customFFprobe_ = file;
    probe_.setExecutable(ffprobePath());
    saveSettings();
    statusBar()->showMessage("FFprobe path updated. Future files use this binary.");
}
void MainWindow::detectEncoders() {
    if (detection_) {
        detection_->disconnect(this);
        detection_->kill();
        detection_->deleteLater();
    }
    encoders_.clear();
    hardwareReport_ = {};
    fillCodecCombos();
    backendLabel_->setText("Checking FFmpeg…");
    detection_ = new QProcess(this);
    QProcess *p = detection_;
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p](int code, QProcess::ExitStatus st) {
        if (p != detection_) return;
        if (code == 0 && st == QProcess::NormalExit) {
            encoders_ = Codec::parseEncoderList(QString::fromUtf8(p->readAllStandardOutput()));
            hardwareReport_.encoders = encoders_;
            hiveWorker_.setHardwareReport(hardwareReport_);
            backendLabel_->setText(encoders_.isEmpty() ? "FFmpeg: no encoders found" : "FFmpeg connected · " + QString::number(encoders_.size()) + " encoders");
        } else {
            backendLabel_->setText("FFmpeg not found");
            log("FFmpeg discovery failed: " + p->errorString());
        }
        fillCodecCombos();
        if (!encoders_.isEmpty()) scanHardware();
        p->deleteLater();
        if (detection_ == p) detection_ = nullptr;
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart && p == detection_) {
            backendLabel_->setText("FFmpeg not found — Tools → Select FFmpeg");
            log("Cannot start FFmpeg: " + p->errorString());
            p->deleteLater();
            detection_ = nullptr;
            updateActions();
        }
    });
    p->start(ffmpegPath(), {"-hide_banner", "-encoders"});
}

void MainWindow::applyPreset() {
    const QString name = preset_->currentData().toString();
    if (name == "custom" || applyingPreset_) return;
    applyingPreset_ = true;
    QString container, video, audio;
    if (name == "mp4_standard") { container = "mp4"; video = "h264"; audio = "aac"; }
    else if (name == "mkv_hevc") { container = "mkv"; video = "hevc"; audio = "aac"; }
    else if (name == "webm_vp9") { container = "webm"; video = "vp9"; audio = "libopus"; }
    else if (name == "mkv_av1") { container = "mkv"; video = "av1"; audio = "libopus"; }
    else if (name == "audio_mp3") { container = "mp3"; audio = "libmp3lame"; }
    else if (name == "audio_opus") { container = "ogg"; audio = "libopus"; }
    else if (name == "audio_flac") { container = "flac"; audio = "flac"; }
    setDataValue(container_, container);
    setDataValue(video_, video);
    setDataValue(audio_, audio);
    quality_->setCurrentIndex(2);
    speed_->setCurrentIndex(1);
    resolution_->setCurrentIndex(0);
    frameRate_->setCurrentIndex(0);
    videoBitDepth_->setCurrentIndex(0);
    applyingPreset_ = false;
    refreshSettings();
    if ((!video.isEmpty() && video_->currentData().toString() != video) || audio_->currentData().toString() != audio)
        hintLabel_->setText("Some codecs required by this preset are not in your FFmpeg build. Check the selected codecs before starting.");
}

void MainWindow::fillSmartCodecs() {
    if (!smartVideo_ || !smartAudio_) return;
    const QString videoSelection = smartVideo_->currentData().toString();
    const QString audioSelection = smartAudio_->currentData().toString();
    const QSignalBlocker blockV(smartVideo_);
    const QSignalBlocker blockA(smartAudio_);
    smartVideo_->clear();
    smartAudio_->clear();
    smartVideo_->addItem("Automatic · H.265 if available", "auto");
    for (const auto &entry : Codec::videoOptions("mkv", encoders_))
        smartVideo_->addItem(entry.name, entry.encoder);
    smartVideo_->addItem("Keep original · passthrough", "copy");
    smartAudio_->addItem("Automatic · Opus if available", "auto");
    for (const auto &entry : Codec::audioOptions("mkv", encoders_))
        smartAudio_->addItem(entry.name, entry.encoder);
    smartAudio_->addItem("Keep original · passthrough", "copy");
    setDataValue(smartVideo_, !savedSmartVideo_.isEmpty() ? savedSmartVideo_
        : videoSelection.isEmpty() ? "auto" : videoSelection, "auto");
    setDataValue(smartAudio_, !savedSmartAudio_.isEmpty() ? savedSmartAudio_
        : audioSelection.isEmpty() ? "auto" : audioSelection, "auto");
    if (smartVideo_->currentData().toString() == savedSmartVideo_) savedSmartVideo_.clear();
    if (smartAudio_->currentData().toString() == savedSmartAudio_) savedSmartAudio_.clear();
}

void MainWindow::updateSmartUi() {
    const bool smart = mode_->currentData().toString() == "smart";
    smartGroup_->setVisible(smart);
    advancedPresetLabel_->setVisible(!smart);
    preset_->setVisible(!smart);
    settingsGroup_->setVisible(!smart);
    hardwareGroup_->setVisible(!smart);
    jobOverridesGroup_->setVisible(!smart);
    refreshSettings();
}

void MainWindow::fillCodecCombos() {
    refreshing_ = true;
    const QString prevV = video_->currentData().toString();
    const QString prevA = audio_->currentData().toString();
    const QString container = container_->currentData().toString();
    const bool audioOnly = Codec::isAudioOnly(container);
    // Preserve the user's video choice when temporarily selecting an audio-only container.
    const bool wasAudioOnly = video_->count() == 1 &&
        video_->itemText(0).startsWith("Remove video (audio-only output)");
    if (audioOnly && !wasAudioOnly && !prevV.isEmpty()) lastVideoChoice_ = prevV;
    video_->clear();
    audio_->clear();

    if (audioOnly) {
        video_->addItem("Remove video (audio-only output)", "remove");
    } else {
        for (const auto &e : Codec::videoOptions(container, encoders_))
            video_->addItem(e.name, e.encoder);
        // Keep both actions available, even if the installed FFmpeg has no video encoder.
        video_->addItem("Passthrough · copy original video", "copy");
        video_->addItem("Remove · no video", "remove");
        const QString preferredVideo = wasAudioOnly ? lastVideoChoice_ :
            (prevV.isEmpty() ? savedVideo_ : prevV);
        setDataValue(video_, preferredVideo, Codec::defaultPreset(container));
    }

    for (const auto &e : Codec::audioOptions(container, encoders_))
        audio_->addItem(e.name, e.encoder);
    audio_->addItem("Passthrough · copy original audio", "copy");
    if (!audioOnly) audio_->addItem("Remove · no audio", "remove");
    setDataValue(audio_, prevA.isEmpty() ? savedAudio_ : prevA,
                 defaultAudioCodec(container));

    refreshing_ = false;
    fillSmartCodecs();
    refreshSettings();
}
void MainWindow::refreshSettings() {
    if (refreshing_) return;
    const bool audioOnly = Codec::isAudioOnly(container_->currentData().toString());
    const QString videoSelection = video_->currentData().toString();
    const QString audioSelection = audio_->currentData().toString();
    const QString vAction = audioOnly ? "remove" : actionForCodecSelection(videoSelection);
    const QString aAction = actionForCodecSelection(audioSelection);
    const bool videoTranscode = vAction == "transcode";
    const bool audioTranscode = aAction == "transcode";

    // The codec dropdown must remain editable when Passthrough / Remove is selected.
    // Only the dependent transcoding controls become disabled.
    video_->setEnabled(!audioOnly && !engine_.running());
    audio_->setEnabled(!engine_.running());
    quality_->setEnabled(!engine_.running() && videoTranscode);
    speed_->setEnabled(!engine_.running() && videoTranscode);
    resolution_->setEnabled(!engine_.running() && videoTranscode);
    frameRate_->setEnabled(!engine_.running() && videoTranscode);
    videoBitDepth_->setEnabled(!engine_.running() && videoTranscode);
    const bool can10Bit = !videoTranscode || Codec::supports10Bit(videoSelection);
    if (auto *model = qobject_cast<QStandardItemModel *>(videoBitDepth_->model())) {
        if (auto *item = model->item(videoBitDepth_->findData(10))) item->setEnabled(can10Bit);
    }
    // A change to H.264/MPEG-4 must not silently generate unsupported 10-bit FFmpeg flags.
    if (!can10Bit && videoBitDepth_->currentData().toInt() == 10) {
        const QSignalBlocker block(videoBitDepth_);
        videoBitDepth_->setCurrentIndex(videoBitDepth_->findData(0));
    }
    hardwareGroup_->setEnabled(!engine_.running() && videoTranscode);

    // Fine control for variable-rate encoders; indexed valid rates for codecs
    // with fixed bitrate tables (AC-3, E-AC-3, MP3 and DTS Core).
    const auto spec = Codec::audioBitrateSpec(audioSelection);
    if (configuredAudioBitrateCodec_ != audioSelection) {
        configuredAudioBitrateCodec_ = audioSelection;
        if (spec.enabled()) {
            const int target = Codec::normaliseAudioBitrate(audioSelection, audioBitrate_->value());
            const QSignalBlocker blockedSpin(audioBitrate_);
            const QSignalBlocker blockedSlider(audioBitrateSlider_);
            audioBitrate_->setRange(spec.minimum, spec.maximum);
            audioBitrate_->setSingleStep(spec.discrete.isEmpty() ? 1 : 32);
            audioBitrateSlider_->setRange(0, spec.discrete.isEmpty() ? 0 : spec.discrete.size() - 1);
            if (spec.discrete.isEmpty()) audioBitrateSlider_->setRange(spec.minimum, spec.maximum);
            audioBitrateSlider_->setSingleStep(1);
            audioBitrateSlider_->setPageStep(spec.discrete.isEmpty() ? 16 : 1);
            audioBitrate_->setValue(target);
            audioBitrateSlider_->setValue(spec.discrete.isEmpty() ? target : spec.discrete.indexOf(target));
        }
    }
    audioBitrateRow_->setEnabled(!engine_.running() && audioTranscode && spec.enabled());
    audioBitrate_->setToolTip(spec.discrete.isEmpty()
        ? "Type an exact bitrate in kbps, or use the slider (1 kbps increments)."
        : "Only codec-supported bitrates are used. Typed values snap to the closest valid rate.");
    sampleRate_->setEnabled(!engine_.running() && audioTranscode);
    channels_->setEnabled(!engine_.running() && audioTranscode);
    const int maxChannels = Codec::audioMaxChannels(audioSelection);
    if (audioTranscode && channels_->currentData().toInt() > maxChannels) {
        const QSignalBlocker blocker(channels_);
        const int fallback = channels_->findData(maxChannels == 2 ? 2 : 6);
        if (fallback >= 0) channels_->setCurrentIndex(fallback);
    }
    if (auto *model = qobject_cast<QStandardItemModel *>(channels_->model())) {
        for (int i = 0; i < channels_->count(); ++i) {
            if (auto *item = model->item(i))
                item->setEnabled(!audioTranscode || channels_->itemData(i).toInt() <= maxChannels);
        }
    }
    channels_->setToolTip(audioTranscode && maxChannels < 8
        ? QString("%1 supports up to %2 channels. Original will automatically downmix higher-channel input.")
            .arg(audio_->currentText()).arg(maxChannels)
        : "Original keeps the input channel layout. Select 5.1 or 7.1 to mix to that layout.");
    const bool opus = audioTranscode && audioSelection == "libopus";
    const int idx = sampleRate_->findData(44100);
    if (idx >= 0) {
        auto *model = qobject_cast<QStandardItemModel *>(sampleRate_->model());
        if (model && model->item(idx)) model->item(idx)->setEnabled(!opus);
        if (opus && sampleRate_->currentData().toInt() == 44100) sampleRate_->setCurrentIndex(0);
    }

    if (mode_->currentData().toString() == "smart") {
        const EncodeOptions options = currentOptions();
        summaryLabel_->setText("Smart Encoding · MKV · preserves compatible audio, subtitle, chapter and font tracks.");
        const auto selected = queue_->selectedItems();
        const int index = selected.isEmpty() ? (jobs_.size() == 1 ? 0 : -1) : queue_->indexOfTopLevelItem(selected.first());
        if (index >= 0 && index < jobs_.size() && jobs_[index].state != JobState::Probing) {
            const auto plan = SmartEncoding::makePlan(options, jobs_[index].info);
            smartPreview_->setText(plan.ok ? plan.explanation : "Cannot plan this file: " + plan.error);
        } else smartPreview_->setText("Select a queued file to preview its planned settings. Each file is analysed independently.");
        hintLabel_->setText(options.smartTargetMB > 0
            ? "Target size is approximate, not guaranteed. Smart Encoding may lower resolution when the "
              "bitrate budget is small. All compatible MKV tracks are retained."
            : "Without a size target, Harbinger chooses quality-controlled encoding and preserves original resolution.");
        updateActions();
        return;
    }
    summaryLabel_->setText("Output: " + container_->currentText() + "\nVideo: " +
        displayStreamChoice(vAction == "remove" ? "remove" : videoSelection, video_->currentText()) +
        "\nAudio: " + displayStreamChoice(audioSelection, audio_->currentText()) +
        (audioTranscode && spec.enabled()
            ? QString("\nAudio target: %1 kbps per stream%2").arg(audioBitrate_->value())
                  .arg(audioSelection == "libopus" ? " (variable bitrate)" : "")
            : QString{}));
    hintLabel_->setText((vAction == "copy" || aAction == "copy")
        ? "Passthrough copies the original compressed stream without changing its quality. "
          "The output container must support the copied codec (MKV is recommended). "
          "To resize, change FPS, audio bitrate, sample rate or channels, choose an encoding codec."
        : "Selecting a codec transcodes that stream. 'Remove' omits it; 'Passthrough' copies it unchanged. "
          "Hardware acceleration applies only when video is transcoded.");
    updateActions();
}

void MainWindow::selectFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Add video or audio files", {},
        "Media files (*.mp4 *.mkv *.webm *.mov *.avi *.mpg *.mpeg *.m4v *.wmv *.flv *.ts *.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.wma *.aiff *.ac3 *.eac3 *.dts);;All files (*)");
    addFiles(files);
}
void MainWindow::addFiles(const QStringList &paths) {
    if (engine_.running()) return;
    for (const QString &p : paths) {
        const QFileInfo file(p);
        if (!file.isFile()) continue;
        const QString path = file.canonicalFilePath();
        bool exists = false;
        for (const auto &j : jobs_) if (j.input == path) { exists = true; break; }
        if (exists) continue;
        Job j;
        j.input = path;
        j.info.path = path;
        jobs_.append(j);
        auto *item = new QTreeWidgetItem(queue_);
        item->setSizeHint(0, QSize(0, 32));
        auto *progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(0);
        progress->setTextVisible(false);
        progress->setFixedHeight(13);
        queue_->setItemWidget(item, 3, progress);
        refreshRow(jobs_.size() - 1);
        probe_.enqueue(path);
    }
    updateActions();
}
void MainWindow::selectOutputFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, "Select output folder", outputFolder_->text());
    if (!path.isEmpty()) outputFolder_->setText(path);
}
void MainWindow::removeSelected() {
    if (engine_.running()) return;
    const auto selected = queue_->selectedItems();
    for (int i = jobs_.size() - 1; i >= 0; --i) {
        if (selected.contains(queue_->topLevelItem(i))) {
            delete queue_->takeTopLevelItem(i);
            jobs_.removeAt(i);
        }
    }
    refreshFileInfo();
    updateActions();
}
void MainWindow::clearCompleted() {
    if (engine_.running()) return;
    for (int i = jobs_.size() - 1; i >= 0; --i) {
        if (jobs_[i].state == JobState::Completed || jobs_[i].state == JobState::Cancelled || jobs_[i].state == JobState::Failed) {
            delete queue_->takeTopLevelItem(i);
            jobs_.removeAt(i);
        }
    }
    loadJobOverrides();
    refreshFileInfo();
    updateActions();
}
void MainWindow::refreshRow(int index) {
    if (index < 0 || index >= jobs_.size()) return;
    const Job &job = jobs_.at(index);
    auto *row = queue_->topLevelItem(index);
    if (!row) return;
    row->setText(0, QFileInfo(job.input).fileName());
    row->setToolTip(0, job.input);
    row->setText(1, job.state == JobState::Probing ? "…" : Codec::humanDuration(job.info.duration));
    row->setText(2, niceStatus(job.state));
    row->setToolTip(2, job.error.isEmpty() ? job.output : job.error);
    if (auto *bar = qobject_cast<QProgressBar *>(queue_->itemWidget(row, 3))) bar->setValue(job.progress);
    if (job.state == JobState::Failed) row->setForeground(2, QColor("#F28B82"));
    else if (job.state == JobState::Completed) row->setForeground(2, QColor("#6DD6AE"));
    else row->setForeground(2, QColor("#E6ECF5"));
    int total = 0;
    for (const auto &j : jobs_) total += j.progress;
    overallProgress_->setValue(jobs_.isEmpty() ? 0 : total / jobs_.size());
}
void MainWindow::parseBitratePacketOutput(const QByteArray &chunk) {
    bitrateScanBuffer_.append(chunk);
    int pos = -1;
    while ((pos = bitrateScanBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = bitrateScanBuffer_.left(pos).trimmed();
        bitrateScanBuffer_.remove(0, pos + 1);
        if (line.isEmpty()) continue;
        int streamIndex = -1;
        qint64 size = 0;
        double pts = 0.0, dts = 0.0, duration = 0.0;
        bool validPts = false, validDts = false;
        for (const QByteArray &field : line.split('|')) {
            const int eq = field.indexOf('=');
            if (eq < 1) continue;
            const QByteArray name = field.left(eq);
            const QByteArray value = field.mid(eq + 1);
            bool ok = false;
            if (name == "stream_index") { const int n = value.toInt(&ok); if (ok) streamIndex = n; }
            else if (name == "size") { const qint64 n = value.toLongLong(&ok); if (ok) size = n; }
            else if (name == "pts_time") { const double n = value.toDouble(&ok); if (ok && std::isfinite(n)) { pts = n; validPts = true; } }
            else if (name == "dts_time") { const double n = value.toDouble(&ok); if (ok && std::isfinite(n)) { dts = n; validDts = true; } }
            else if (name == "duration_time") { const double n = value.toDouble(&ok); if (ok && std::isfinite(n) && n >= 0.0 && n < 3600.0) duration = n; }
        }
        if (streamIndex < 0 || size <= 0) continue;
        PacketBitrateStats &stats = bitrateScanStats_[streamIndex];
        stats.bytes += size;
        if (validPts || validDts) {
            const double first = validPts ? pts : dts;
            const double last = first + duration;
            if (!stats.hasTimes) { stats.first = first; stats.last = last; stats.hasTimes = true; }
            else { stats.first = std::min(stats.first, first); stats.last = std::max(stats.last, last); }
        }
    }
}

void MainWindow::finishBitrateScan(bool success) {
    if (activeBitrateScanKey_.isEmpty()) return;
    const QString key = activeBitrateScanKey_;
    activeBitrateScanKey_.clear();
    QHash<int, qint64> results;
    if (success) {
        for (const MediaStream &track : bitrateScanInfo_.streams) {
            if (track.type != "video" && track.type != "audio") continue;
            const auto it = bitrateScanStats_.constFind(track.index);
            if (it == bitrateScanStats_.cend() || it->bytes <= 0) continue;
            // Use the span of this stream's actual packets rather than the
            // container's duration; dubbed audio may start late or end early.
            double seconds = it->hasTimes ? it->last - it->first : 0.0;
            if (!(seconds > 0.0) || !std::isfinite(seconds))
                seconds = track.durationKnown ? track.duration : bitrateScanInfo_.duration;
            if (!(seconds > 0.0) || !std::isfinite(seconds)) continue;
            const double bps = 8.0 * static_cast<double>(it->bytes) / seconds;
            if (bps > 0 && bps < 1e12 && std::isfinite(bps))
                results.insert(track.index, static_cast<qint64>(std::llround(bps)));
        }
    }
    if (!results.isEmpty()) measuredBitrates_.insert(key, results);
    else unsuccessfulBitrateScans_.insert(key);
    bitrateScanStats_.clear();
    bitrateScanBuffer_.clear();
    bitrateScanInfo_ = MediaInfo{};
    refreshFileInfo();
}

void MainWindow::startBitrateScan(const Job &job) {
    if (engine_.running() || job.info.streams.isEmpty() || !QFileInfo::exists(job.input)) return;
    const QString key = bitrateCacheKey(job.input);
    if (measuredBitrates_.contains(key) || unsuccessfulBitrateScans_.contains(key) ||
        !activeBitrateScanKey_.isEmpty()) return;
    bool shouldScan = false;
    for (const MediaStream &track : job.info.streams) {
        if ((track.type == "video" || track.type == "audio") &&
            (track.bitRate <= 0 || track.bitRateFromTag)) { shouldScan = true; break; }
    }
    if (!shouldScan) return;
    activeBitrateScanKey_ = key;
    bitrateScanInfo_ = job.info;
    bitrateScanStats_.clear();
    bitrateScanBuffer_.clear();
    bitrateScanProcess_->setProgram(ffprobePath());
    bitrateScanProcess_->setArguments({"-v", "error", "-show_packets", "-show_entries",
        "packet=stream_index,size,pts_time,dts_time,duration_time",
        "-of", "compact=p=0:nk=0", job.input});
    bitrateScanProcess_->start();
    // Repaint once so the empty bitrate cells show "Measuring…" immediately.
    QTimer::singleShot(0, this, [this] { refreshFileInfo(); });
    // A slow/network file shouldn't leave the analyser running forever.
    QTimer::singleShot(180000, this, [this, key] {
        if (activeBitrateScanKey_ == key &&
            bitrateScanProcess_->state() != QProcess::NotRunning) bitrateScanProcess_->kill();
    });
}

void MainWindow::refreshFileInfo() {
    if (!fileInfo_) return;
    fileInfo_->clear();
    // Extended selection is for queue actions; the inspector describes one file.
    QTreeWidgetItem *selected = queue_->currentItem();
    if (selected && !selected->isSelected()) selected = nullptr;
    if (!selected && !queue_->selectedItems().isEmpty()) selected = queue_->selectedItems().first();
    const int index = selected ? queue_->indexOfTopLevelItem(selected) : -1;
    auto addMessage = [this](const QString &message) {
        auto *item = new QTreeWidgetItem(fileInfo_);
        item->setText(0, message);
        item->setFirstColumnSpanned(true);
    };
    if (index < 0 || index >= jobs_.size()) {
        fileInfoGroup_->setTitle("FILE INFORMATION — SELECT A FILE");
        fileInfoGroup_->setToolTip({});
        addMessage("Select a queued file to inspect its video, audio and subtitle tracks.");
        return;
    }
    const Job &job = jobs_.at(index);
    fileInfoGroup_->setTitle("FILE INFORMATION — " + QFileInfo(job.input).fileName());
    fileInfoGroup_->setToolTip(job.input);
    if (job.state == JobState::Probing) { addMessage("Inspecting file streams…"); return; }
    if (!job.info.error.isEmpty()) { addMessage("Unable to inspect this file: " + job.info.error); return; }
    const QString bitKey = bitrateCacheKey(job.input);
    const auto measured = measuredBitrates_.value(bitKey);
    const bool measuring = (activeBitrateScanKey_ == bitKey);
    int displayed = 0;
    // Absolute FFprobe stream indexes are shown, even when audio and subtitle
    // streams are interleaved in the source container.
    for (const MediaStream &source : job.info.streams) {
        MediaStream track = source;
        if (measured.contains(track.index) && (track.type == "video" || track.type == "audio")) {
            track.bitRate = measured.value(track.index);
            track.bitRateFromTag = false;
            track.bitRateMeasured = true;
        }
        const bool isVideo = track.type == "video";
        const bool isAudio = track.type == "audio";
        const bool isSubtitle = track.type == "subtitle";
        if (!isVideo && !isAudio && !isSubtitle) continue;
        auto *item = new QTreeWidgetItem(fileInfo_);
        const QString type = isVideo ? "Video" : isAudio ? "Audio" : "Subtitle";
        item->setText(0, QString("%1 #%2").arg(type).arg(track.index));
        item->setText(1, streamName(track));
        item->setText(2, isVideo ? VideoInfo::codecName(track) :
                         isAudio ? AudioInfo::codecName(track) : subtitleFormat(track.codec));
        // Bitrate and bit depth do not apply to subtitle streams. Don't show
        // zero or misleading audio/video metadata for subtitles.
        item->setText(3, (isVideo || isAudio) && measuring &&
                         (track.bitRate <= 0 || track.bitRateFromTag) ? "Measuring…" :
                         isVideo ? VideoInfo::bitrate(track) :
                         isAudio ? AudioInfo::bitrate(track) : "—");
        item->setText(4, isVideo ? VideoInfo::bitDepth(track) :
                         isAudio ? AudioInfo::bitDepth(track) : "—");
        QString detail = isVideo ? VideoInfo::resolution(track) :
                         isAudio ? AudioInfo::channels(track) : subtitleKind(track.codec);
        const QString flags = streamFlags(track);
        if (!flags.isEmpty()) detail += " · " + flags;
        item->setText(5, detail);
        const QString description = isVideo ? VideoInfo::toolTip(track) :
                                    isAudio ? AudioInfo::toolTip(track) :
                                    QString("Source subtitle stream #%1\nCodec: %2")
                                        .arg(track.index).arg(subtitleFormat(track.codec));
        const QString tip = streamInfoTip(track, description);
        for (int col = 0; col < 6; ++col) item->setToolTip(col, tip);
        ++displayed;
    }
    if (displayed == 0) addMessage("This file contains no video, audio or subtitle streams.");
    // Start after painting the metadata; the QProcess runs asynchronously, so
    // analysing long MKVs never blocks the interface or changes the file.
    startBitrateScan(job);
}

EncodeOptions MainWindow::currentOptions() const {
    EncodeOptions o;
    o.container = container_->currentData().toString();
    const QString videoSelection = video_->currentData().toString();
    const QString audioSelection = audio_->currentData().toString();
    o.videoAction = Codec::isAudioOnly(o.container) ? "remove" : actionForCodecSelection(videoSelection);
    o.audioAction = actionForCodecSelection(audioSelection);
    // Retain valid codec family/encoder values even when the action is Copy/Remove;
    // Hive's protocol validates these fields independently of the chosen action.
    o.videoCodec = o.videoAction == "transcode" ? videoSelection : Codec::defaultPreset(o.container);
    o.videoEncoder = Hardware::softwareEncoder(o.videoCodec, encoders_);
    o.encodeMode = encodeMode_->currentData().toString();
    o.decodeMode = decodeMode_->currentData().toString();
    o.gpuId = gpu_->currentData().toString();
    o.cpuFallback = cpuFallback_->isChecked();
    o.audioEncoder = o.audioAction == "transcode" ? audioSelection : defaultAudioCodec(o.container);
    o.videoQuality = quality_->currentIndex();
    o.speed = speed_->currentIndex();
    o.height = resolution_->currentData().toInt();
    const QString rate = frameRate_->currentData().toString();
    if (rate != "0") {
        bool whole = false;
        const int fps = rate.toInt(&whole);
        if (whole) o.frameRate = fps;
        else o.frameRateOverride = rate;
    }
    o.videoBitDepth = videoBitDepth_->currentData().toInt();
    o.audioBitrate = audioBitrate_->value();
    o.sampleRate = sampleRate_->currentData().toInt();
    o.channels = channels_->currentData().toInt();
    o.preserveMetadata = metadata_->isChecked();
    if (mode_->currentData().toString() == "smart") {
        o.smart = true;
        o.smartTargetMB = smartTargetMB_->value();
        o.smartPriority = smartPriority_->currentData().toInt();
        o.speed = smartSpeed_->currentData().toInt();
        o.smartVideo = smartVideo_->currentData().toString();
        o.smartAudio = smartAudio_->currentData().toString();
        // Smart mode works on MKV so copied subtitles, audio tracks, fonts,
        // chapters and metadata are not discarded to meet a size goal.
        o.container = "mkv";
        const auto options = Codec::videoOptions("mkv", encoders_);
        const auto audioOptions = Codec::audioOptions("mkv", encoders_);
        const QString videoAuto = std::any_of(options.begin(), options.end(), [](const CodecEntry &e) { return e.encoder == "hevc"; }) ? "hevc"
            : std::any_of(options.begin(), options.end(), [](const CodecEntry &e) { return e.encoder == "h264"; }) ? "h264"
            : (options.isEmpty() ? QString{} : options.first().encoder);
        const QString audioAuto = std::any_of(audioOptions.begin(), audioOptions.end(), [](const CodecEntry &e) { return e.encoder == "libopus"; }) ? "libopus"
            : std::any_of(audioOptions.begin(), audioOptions.end(), [](const CodecEntry &e) { return e.encoder == "aac"; }) ? "aac"
            : (audioOptions.isEmpty() ? QString{} : audioOptions.first().encoder);
        o.videoCodec = videoAuto;
        o.audioEncoder = audioAuto;
    }
    return o;
}
void MainWindow::updateActions() {
    const bool running = engine_.running();
    int ready = 0;
    for (const auto &j : jobs_) if (j.state == JobState::Ready) ++ready;
    const bool audioOnly = Codec::isAudioOnly(container_->currentData().toString());
    const QString videoAction = audioOnly ? "remove" : actionForCodecSelection(video_->currentData().toString());
    const QString audioAction = actionForCodecSelection(audio_->currentData().toString());
    const bool smart = mode_->currentData().toString() == "smart";
    bool anySourceVideo = false;
    bool anySourceAudio = false;
    for (const auto &job : jobs_) if (job.state == JobState::Ready) {
        anySourceVideo |= job.info.hasVideo;
        anySourceAudio |= job.info.hasAudio;
    }
    const bool selectedCodec = smart ?
        (smartVideo_->currentIndex() >= 0 && smartAudio_->currentIndex() >= 0 &&
         (!anySourceVideo || smartVideo_->currentData().toString() != "auto" || !currentOptions().videoCodec.isEmpty()) &&
         (!anySourceAudio || smartAudio_->currentData().toString() != "auto" || !currentOptions().audioEncoder.isEmpty())) :
        (!(videoAction == "remove" && audioAction == "remove") &&
         video_->currentIndex() >= 0 && audio_->currentIndex() >= 0 &&
         (!audioOnly || audioAction != "remove"));
    const bool folderExists = QDir(outputFolder_->text()).exists();
    startButton_->setEnabled(!running && ready > 0 && selectedCodec && folderExists && !encoders_.isEmpty());
    removeButton_->setEnabled(!running && !queue_->selectedItems().isEmpty());
    clearButton_->setEnabled(!running && !jobs_.isEmpty());
    queueCount_->setText(QString("%1 files · %2 ready").arg(jobs_.size()).arg(ready));
}
void MainWindow::setBusy(bool busy) {
    mode_->setEnabled(!busy);
    smartGroup_->setEnabled(!busy);
    settingsGroup_->setEnabled(!busy);
    hardwareGroup_->setEnabled(!busy && !Codec::isAudioOnly(container_->currentData().toString()) &&
                               actionForCodecSelection(video_->currentData().toString()) == "transcode");
    jobOverridesGroup_->setEnabled(!busy && queue_->selectedItems().size() == 1);
    if (outputFolder_->parentWidget()) outputFolder_->parentWidget()->setEnabled(!busy);
    addButton_->setEnabled(!busy);
    removeButton_->setEnabled(!busy);
    clearButton_->setEnabled(!busy);
    stopButton_->setEnabled(busy);
    ffmpegAction_->setEnabled(!busy);
    ffprobeAction_->setEnabled(!busy);
    startButton_->setEnabled(false);
    updateActions();
}
void MainWindow::startEncoding() {
    if (engine_.running()) return;
    const QString directory = outputFolder_->text().trimmed();
    QFileInfo dir(directory);
    if (!dir.isDir() || !dir.isWritable()) {
        QMessageBox::warning(this, "Invalid destination", "Select an existing writable output folder.");
        return;
    }
    const EncodeOptions opts = currentOptions();
    QVector<EncodeTask> batch;
    for (int i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].state != JobState::Ready) continue;
        bool needVideo = !Codec::isAudioOnly(opts.container) && opts.videoAction != "remove";
        bool needAudio = opts.audioAction != "remove";
        if (opts.smart) {
            const auto plan = SmartEncoding::makePlan(opts, jobs_[i].info);
            if (!plan.ok) {
                jobs_[i].state = JobState::Failed;
                jobs_[i].error = "Smart Encoding: " + plan.error;
                log(QFileInfo(jobs_[i].input).fileName() + ": " + jobs_[i].error);
                refreshRow(i);
                continue;
            }
            needVideo = plan.options.videoAction != "remove";
            needAudio = plan.options.audioAction != "remove";
        }
        if ((needVideo && !jobs_[i].info.hasVideo) ||
            (needAudio && !jobs_[i].info.hasAudio && !needVideo)) {
            jobs_[i].state = JobState::Failed;
            jobs_[i].error = "This file does not contain the required media track.";
            refreshRow(i);
            continue;
        }
        EncodeTask task;
        task.index = i; task.info = jobs_[i].info;
        task.decodeOverride = jobs_[i].decodeOverride;
        task.encodeOverride = jobs_[i].encodeOverride;
        task.gpuOverride = jobs_[i].gpuOverride;
        batch.append(task);
        jobs_[i].state = JobState::Queued;
        refreshRow(i);
    }
    if (batch.isEmpty()) {
        QMessageBox::information(this, "Nothing to encode", "No ready files match the chosen output format.");
        updateActions();
        return;
    }
    saveSettings();
    engine_.setExecutable(ffmpegPath());
    engine_.start(batch, opts, QDir(directory).absolutePath(), hardwareReport_);
    setBusy(true);
}
void MainWindow::log(const QString &message) {
    logView_->appendPlainText(message.trimmed());
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!engine_.running() && event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *event) {
    if (engine_.running()) return;
    QStringList files;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) files << url.toLocalFile();
    addFiles(files);
    event->acceptProposedAction();
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (engine_.running()) {
        if (QMessageBox::question(this, "Encoding in progress", "Cancel the current encoding and exit?") != QMessageBox::Yes) {
            event->ignore(); return;
        }
        closeAfterStop_ = true;
        engine_.cancel();
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::openMuxWindow() {
    MuxWindow dialog(ffmpegPath(), ffprobePath(), this);
    dialog.exec();
}

void MainWindow::openHiveWindow() {
    if (engine_.running()) {
        QMessageBox::information(this, "Harbinger Hive", "Finish the current local encoding queue before opening the Hive coordinator.");
        return;
    }
    QVector<EncodeTask> sources;
    const auto options=currentOptions();
    for (int i=0;i<jobs_.size();++i) {
        const auto &j=jobs_.at(i);
        if (j.state!=JobState::Ready || !QFileInfo::exists(j.info.path)) continue;
        const bool needVideo = !Codec::isAudioOnly(options.container) && options.videoAction != "remove";
        if ((needVideo && !j.info.hasVideo) ||
            (options.audioAction != "remove" && !j.info.hasAudio && !needVideo)) continue;
        EncodeTask task;task.index=i;task.info=j.info;
        task.decodeOverride=j.decodeOverride;task.encodeOverride=j.encodeOverride;task.gpuOverride=j.gpuOverride;
        sources.append(task);
    }
    HiveWindow dialog(&hiveWorker_,ffmpegPath(),ffprobePath(),outputFolder_->text(),sources,options,hardwareReport_,this);
    dialog.exec();
}

void MainWindow::openHardwareDialog() {
    if (!hardwareDialog_) {
        hardwareDialog_ = new HardwareDialog(this);
        connect(hardwareDialog_, &HardwareDialog::rescanRequested, this, &MainWindow::scanHardware);
    }
    hardwareDialog_->showReport(hardwareReport_);
    hardwareDialog_->setScanning(hardwareScanning_);
    hardwareDialog_->show();
    hardwareDialog_->raise();
    hardwareDialog_->activateWindow();
}
void MainWindow::scanHardware() {
    if (engine_.running()) return;
    if (hardwareScanning_) { rescanAfterCurrent_ = true; return; }
    hardwareScanning_ = true;
    statusBar()->showMessage("Testing FFmpeg GPU encoding and decoding capabilities…");
    if (hardwareDialog_) hardwareDialog_->setScanning(true);
    auto *scanner = new HardwareScanner(ffmpegPath(), this);
    hardwareScanner_ = scanner;
    connect(scanner, &HardwareScanner::reportReady, this, [this, scanner](const Hardware::Report &result) {
        if (scanner != hardwareScanner_) return;
        hardwareReport_ = result;
        hiveWorker_.setHardwareReport(result);
        rebuildGpuCombos();
        if (hardwareDialog_) hardwareDialog_->showReport(result);
        int verified = 0;
        for (const auto &gpu : result.devices)
            for (const auto &cap : gpu.codecs)
                if (cap.encode == Hardware::State::Verified) ++verified;
        log(QString("Hardware scan: %1 GPUs, %2 working codec/encoder paths.")
            .arg(result.devices.size()).arg(verified));
        for (const QString &warning : result.warnings) log("GPU scan: " + warning);
        statusBar()->showMessage("Hardware scan complete. Open Tools → Hardware Capabilities to view details.");
    });
    connect(scanner, &QThread::finished, this, [this, scanner] {
        if (scanner == hardwareScanner_) hardwareScanner_ = nullptr;
        hardwareScanning_ = false;
        if (hardwareDialog_) hardwareDialog_->setScanning(false);
        scanner->deleteLater();
        if (rescanAfterCurrent_) {
            rescanAfterCurrent_ = false;
            scanHardware();
        }
    });
    scanner->start();
}
void MainWindow::rebuildGpuCombos() {
    const QString prev = gpu_->currentData().toString().isEmpty() ? savedGpu_ : gpu_->currentData().toString();
    const QString prevJob = jobGpu_->currentData().toString();
    gpu_->blockSignals(true);
    jobGpu_->blockSignals(true);
    gpu_->clear(); jobGpu_->clear();
    gpu_->addItem("Automatic · any compatible GPU", "");
    jobGpu_->addItem("Use global setting", "");
    jobGpu_->addItem("Any GPU (override)", "default");
    for (const auto &g : hardwareReport_.devices) {
        // Some FFmpeg backends do not provide stable per-adapter selection.
        if (!g.selectable) continue;
        const QString label = g.name + " · " + g.backend.toUpper();
        gpu_->addItem(label, g.id);
        jobGpu_->addItem(label, g.id);
    }
    setDataValue(gpu_, prev);
    setDataValue(jobGpu_, prevJob);
    gpu_->blockSignals(false);
    jobGpu_->blockSignals(false);
    savedGpu_.clear();
    refreshSettings();
}
void MainWindow::loadJobOverrides() {
    const auto selection = queue_->selectedItems();
    const bool one = selection.size() == 1 && !engine_.running();
    jobOverridesGroup_->setEnabled(one);
    if (!one) return;
    const int i = queue_->indexOfTopLevelItem(selection.first());
    if (i < 0 || i >= jobs_.size()) return;
    for (QComboBox *c : {jobDecode_, jobEncode_, jobGpu_}) c->blockSignals(true);
    setDataValue(jobDecode_, jobs_[i].decodeOverride, "");
    setDataValue(jobEncode_, jobs_[i].encodeOverride, "");
    setDataValue(jobGpu_, jobs_[i].gpuOverride, "");
    for (QComboBox *c : {jobDecode_, jobEncode_, jobGpu_}) c->blockSignals(false);
}
void MainWindow::updateJobOverrides() {
    if (engine_.running()) return;
    const auto selection = queue_->selectedItems();
    if (selection.size() != 1) return;
    const int i = queue_->indexOfTopLevelItem(selection.first());
    if (i < 0 || i >= jobs_.size()) return;
    jobs_[i].decodeOverride = jobDecode_->currentData().toString();
    jobs_[i].encodeOverride = jobEncode_->currentData().toString();
    jobs_[i].gpuOverride = jobGpu_->currentData().toString();
    queue_->topLevelItem(i)->setToolTip(0, jobs_[i].input + "\n" +
        "Per-job acceleration override: encoding=" + (jobs_[i].encodeOverride.isEmpty() ? "global" : jobs_[i].encodeOverride) +
        ", decoding=" + (jobs_[i].decodeOverride.isEmpty() ? "global" : jobs_[i].decodeOverride));
}
