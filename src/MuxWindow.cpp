#include "MuxWindow.h"
#include "Codec.h"
#include "AudioInfo.h"
#include "WaveSync.h"
#include "BatchMuxWindow.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QProgressDialog>
#include <QThread>
#include <QPointer>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <memory>
#include <functional>

namespace {
const QString mediaFilter = "Media and subtitle files (*.mkv *.mp4 *.mov *.avi *.webm *.ts *.m2ts *.mpg *.flv *.m4v *.mp3 *.m4a *.aac *.flac *.opus *.ogg *.wav *.srt *.ass *.ssa *.vtt *.sup);;All files (*)";
class SourceList final : public QTreeWidget {
public:
    std::function<void(const QStringList &)> onDropFiles;
    explicit SourceList(QWidget *parent = nullptr) : QTreeWidget(parent) {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
    }
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction(); else QTreeWidget::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction(); else QTreeWidget::dragMoveEvent(e);
    }
    void dropEvent(QDropEvent *e) override {
        if (!e->mimeData()->hasUrls()) { QTreeWidget::dropEvent(e); return; }
        QStringList paths;
        for (const QUrl &url : e->mimeData()->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
        if (onDropFiles) onDropFiles(paths);
        e->acceptProposedAction();
    }
};
QString typeLabel(const QString &kind) {
    if (kind == "video") return "Video";
    if (kind == "audio") return "Audio";
    if (kind == "subtitle") return "Subtitle";
    return kind;
}
QString trackDetail(const MediaStream &stream) {
    if (stream.type == "video") return QString("%1 × %2").arg(stream.width).arg(stream.height);
    if (stream.type == "audio") return QString("%1 · %2 · %3")
        .arg(stream.channelLayout.isEmpty() ? QString::number(stream.channels) + " ch" : stream.channelLayout)
        .arg(AudioInfo::bitrate(stream)).arg(AudioInfo::bitDepth(stream));
    return stream.title.isEmpty() ? "Text / image subtitles" : stream.title;
}
QString itemKey(const QString &source, int streamIndex) {
    return source + QChar(0x1F) + QString::number(streamIndex);
}
}

MuxWindow::MuxWindow(const QString &ffmpeg, const QString &ffprobe, QWidget *parent)
    : QDialog(parent), ffmpeg_(ffmpeg), ffprobe_(ffprobe), probe_(this), engine_(this) {
    setWindowTitle("Harbinger — MKV Track Muxer");
    setMinimumSize(1080, 680);
    resize(1350, 870);
    setAcceptDrops(true);
    probe_.setExecutable(ffprobe);
    engine_.setExecutable(ffmpeg);
    buildUi();
    connect(&probe_, &Probe::probed, this, &MuxWindow::onProbed);
    connect(&engine_, &MuxEngine::progressChanged, progressBar_, &QProgressBar::setValue);
    connect(&engine_, &MuxEngine::logLine, this, [this](const QString &line) { logs_->appendPlainText(line); });
    connect(&engine_, &MuxEngine::finished, this, [this](bool ok, bool cancelled, const QString &message, const QString &output) {
        setRunning(false);
        status_->setText(ok ? "MKV completed successfully" : cancelled ? "Mux cancelled" : "Mux failed — check log");
        if (ok) {
            logs_->appendPlainText("Created: " + output);
            updateSuggestion();
            if (QMessageBox::question(this, "MKV created", "Finished muxing. Open the output folder?",
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(output).absolutePath()));
        } else if (!cancelled) {
            QMessageBox::warning(this, "Could not create MKV", message.left(1500));
        }
        if (closeAfterCancel_) close();
    });
}
void MuxWindow::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 14, 18, 16);
    outer->setSpacing(10);
    auto *heading = new QLabel("MKV Track Muxer");
    heading->setObjectName("brand");
    outer->addWidget(heading);
    auto *subtitle = new QLabel("Import multiple files, tick the tracks you want, and combine them into a new MKV. Video and audio are copied without quality loss.");
    subtitle->setWordWrap(true);
    subtitle->setObjectName("muted");
    outer->addWidget(subtitle);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    auto *sourcePanel = new QWidget;
    auto *sourceLayout = new QVBoxLayout(sourcePanel);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    auto *sourceBar = new QHBoxLayout;
    sourceBar->addWidget(new QLabel("SOURCE FILES AND AVAILABLE TRACKS"));
    sourceBar->addStretch();
    addButton_ = new QPushButton("+ Add files");
    addButton_->setObjectName("primarySubtle");
    removeFileButton_ = new QPushButton("Remove file");
    sourceBar->addWidget(addButton_);
    sourceBar->addWidget(removeFileButton_);
    sourceLayout->addLayout(sourceBar);
    sourcesTree_ = new SourceList;
    static_cast<SourceList *>(sourcesTree_)->onDropFiles = [this](const QStringList &paths) { importFiles(paths); };
    sourcesTree_->setColumnCount(4);
    sourcesTree_->setHeaderLabels({"Choose tracks", "Codec", "Details", "Language"});
    sourcesTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    sourcesTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sourcesTree_->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    sourcesTree_->header()->resizeSection(2, 220);
    sourcesTree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    sourceLayout->addWidget(sourcesTree_, 1);
    auto *sourceHelp = new QLabel("Tick any tracks to include. Choosing a second video replaces the first. Drag files here to import.");
    sourceHelp->setWordWrap(true);
    sourceHelp->setObjectName("muted");
    sourceLayout->addWidget(sourceHelp);
    split->addWidget(sourcePanel);

    auto *outputPanel = new QWidget;
    auto *outputLayout = new QVBoxLayout(outputPanel);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    auto *trackBar = new QHBoxLayout;
    trackBar->addWidget(new QLabel("OUTPUT MKV — TRACK ORDER"));
    trackBar->addStretch();
    upButton_ = new QPushButton("↑ Up");
    downButton_ = new QPushButton("↓ Down");
    removeTrackButton_ = new QPushButton("Remove track");
    alignAudioButton_ = new QPushButton("Align audio endings");
    alignAudioButton_->setToolTip("Optional: delay shorter audio so it ends near the reference video/audio track. Duration differences don't prove correct sync.");
    resetDelayButton_ = new QPushButton("Clear delays");
    waveSyncButton_ = new QPushButton("Auto-sync by waveform");
    waveSyncButton_->setToolTip("Compare audio energy patterns at multiple positions. Suggestions require review; uncertain matches are never applied.");
    trackBar->addWidget(upButton_);
    trackBar->addWidget(downButton_);
    trackBar->addWidget(removeTrackButton_);
    batchButton_ = new QPushButton("Batch / whole season…");
    batchButton_->setToolTip("Use these selected tracks as a template for matching episodes from multiple source folders.");
    trackBar->addWidget(batchButton_);
    outputLayout->addLayout(trackBar);
    selectedTree_ = new QTreeWidget;
    selectedTree_->setColumnCount(8);
    selectedTree_->setHeaderLabels({"Type", "Source file", "Codec", "Language", "Track name", "Delay (ms)", "Default", "Forced"});
    selectedTree_->setRootIsDecorated(false);
    selectedTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    selectedTree_->setAlternatingRowColors(true);
    selectedTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    selectedTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    selectedTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    selectedTree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    selectedTree_->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    selectedTree_->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    selectedTree_->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    selectedTree_->header()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    selectedTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outputLayout->addWidget(selectedTree_, 1);
    auto *audioSyncBar = new QHBoxLayout;
    audioSyncBar->addWidget(new QLabel("AUDIO TIMING"));
    audioSyncBar->addStretch();
    audioSyncBar->addWidget(waveSyncButton_);
    audioSyncBar->addWidget(alignAudioButton_);
    audioSyncBar->addWidget(resetDelayButton_);
    outputLayout->addLayout(audioSyncBar);
    auto *trackHelp = new QLabel("Double-click Language, Track name or Audio delay (ms) to edit. Waveform sync compares decoded audio samples and offers reviewed suggestions; align endings only compares durations.");
    trackHelp->setWordWrap(true);
    trackHelp->setObjectName("muted");
    outputLayout->addWidget(trackHelp);
    split->addWidget(outputPanel);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 6);
    outer->addWidget(split, 1);

    auto *settings = new QGroupBox("Output MKV");
    auto *settingLayout = new QVBoxLayout(settings);
    auto *outRow = new QHBoxLayout;
    outRow->addWidget(new QLabel("Save to"));
    outputField_ = new QLineEdit;
    outputField_->setPlaceholderText("Select output filename (.mkv)");
    outRow->addWidget(outputField_, 1);
    browseButton_ = new QPushButton("Browse…");
    outRow->addWidget(browseButton_);
    settingLayout->addLayout(outRow);
    auto *extraRow = new QHBoxLayout;
    extraRow->addWidget(new QLabel("Chapters"));
    chapterSource_ = new QComboBox;
    chapterSource_->addItem("Don't include chapters", "");
    extraRow->addWidget(chapterSource_, 1);
    metadataCheck_ = new QCheckBox("Keep main file metadata");
    metadataCheck_->setChecked(true);
    extraRow->addWidget(metadataCheck_);
    settingLayout->addLayout(extraRow);
    summary_ = new QLabel("Choose tracks above to build your MKV.");
    summary_->setObjectName("summary");
    summary_->setWordWrap(true);
    settingLayout->addWidget(summary_);
    outer->addWidget(settings);

    auto *runRow = new QHBoxLayout;
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    runRow->addWidget(progressBar_, 1);
    muxButton_ = new QPushButton("Mux selected tracks");
    muxButton_->setObjectName("primary");
    cancelButton_ = new QPushButton("Cancel mux");
    cancelButton_->setEnabled(false);
    runRow->addWidget(muxButton_);
    runRow->addWidget(cancelButton_);
    outer->addLayout(runRow);
    status_ = new QLabel("Ready. Import source files to get started.");
    status_->setObjectName("muted");
    outer->addWidget(status_);
    logs_ = new QPlainTextEdit;
    logs_->setReadOnly(true);
    logs_->setMaximumBlockCount(1500);
    logs_->setFixedHeight(94);
    logs_->setPlaceholderText("FFmpeg activity and errors appear here.");
    outer->addWidget(logs_);

    connect(addButton_, &QPushButton::clicked, this, &MuxWindow::pickFiles);
    connect(removeFileButton_, &QPushButton::clicked, this, &MuxWindow::removeFile);
    connect(removeTrackButton_, &QPushButton::clicked, this, &MuxWindow::removeTrack);
    connect(upButton_, &QPushButton::clicked, this, [this] { shiftSelected(-1); });
    connect(downButton_, &QPushButton::clicked, this, [this] { shiftSelected(1); });
    connect(alignAudioButton_, &QPushButton::clicked, this, &MuxWindow::autoAlignAudio);
    connect(waveSyncButton_, &QPushButton::clicked, this, &MuxWindow::autoSyncWaveforms);
    connect(batchButton_, &QPushButton::clicked, this, &MuxWindow::openBatchMux);
    connect(resetDelayButton_, &QPushButton::clicked, this, &MuxWindow::resetAudioDelays);
    connect(sourcesTree_, &QTreeWidget::itemChanged, this, &MuxWindow::onSourceToggle);
    connect(sourcesTree_, &QTreeWidget::itemSelectionChanged, this, &MuxWindow::updateSummary);
    connect(selectedTree_, &QTreeWidget::itemChanged, this, &MuxWindow::onSelectedEdited);
    connect(selectedTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int col) {
        if (!engine_.running() && (col == 3 || col == 4 || (col == 5 && item->text(0) == "Audio")))
            selectedTree_->editItem(item, col);
    });
    connect(selectedTree_, &QTreeWidget::itemSelectionChanged, this, &MuxWindow::updateSummary);
    connect(outputField_, &QLineEdit::textEdited, this, [this] { autoOutput_ = false; updateSummary(); });
    connect(outputField_, &QLineEdit::textChanged, this, &MuxWindow::updateSummary);
    connect(browseButton_, &QPushButton::clicked, this, &MuxWindow::pickOutput);
    connect(chapterSource_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MuxWindow::updateSummary);
    connect(muxButton_, &QPushButton::clicked, this, &MuxWindow::startMux);
    connect(cancelButton_, &QPushButton::clicked, this, &MuxWindow::cancelMux);
    updateSummary();
}
void MuxWindow::pickFiles() {
    importFiles(QFileDialog::getOpenFileNames(this, "Import files for MKV muxing", {}, mediaFilter));
}
void MuxWindow::importFiles(const QStringList &paths) {
    if (engine_.running() || analysingAudio_) return;
    for (const auto &path : paths) {
        const QFileInfo fi(path);
        if (!fi.isFile()) continue;
        const QString canonicalPath = fi.canonicalFilePath();
        if (canonicalPath.isEmpty() || sources_.contains(canonicalPath)) continue;
        MediaInfo info;
        info.path = canonicalPath;
        sources_.insert(canonicalPath, info);
        sourceOrder_.append(canonicalPath);
        auto *row = new QTreeWidgetItem(sourcesTree_);
        row->setText(0, fi.fileName());
        row->setToolTip(0, canonicalPath);
        row->setData(0, Qt::UserRole, canonicalPath);
        row->setText(1, "Inspecting…");
        probe_.enqueue(canonicalPath);
    }
    updateSuggestion();
    updateSummary();
}
QTreeWidgetItem *MuxWindow::fileRow(const QString &source) const {
    for (int i = 0; i < sourcesTree_->topLevelItemCount(); ++i) {
        auto *row = sourcesTree_->topLevelItem(i);
        if (row->data(0, Qt::UserRole).toString() == source) return row;
    }
    return nullptr;
}
QTreeWidgetItem *MuxWindow::sourceRow(const QString &source, int index) const {
    auto *file = fileRow(source);
    if (!file) return nullptr;
    for (int i = 0; i < file->childCount(); ++i) {
        auto *child = file->child(i);
        if (child->data(0, Qt::UserRole + 1).toInt() == index) return child;
    }
    return nullptr;
}
void MuxWindow::onProbed(const QString &path, const MediaInfo &info) {
    if (!sources_.contains(path)) return; // Source was removed while the probe ran.
    sources_[path] = info;
    auto *parent = fileRow(path);
    if (!parent) return;
    if (!info.error.isEmpty()) {
        parent->setText(1, "Cannot inspect");
        parent->setToolTip(1, info.error);
        status_->setText("Could not inspect " + QFileInfo(path).fileName() + ": " + info.error.left(150));
        return;
    }
    const QSignalBlocker blocked(sourcesTree_);
    parent->setText(1, QString("%1 tracks").arg(info.streams.size()));
    parent->setText(2, Codec::humanDuration(info.duration));
    for (const MediaStream &stream : info.streams) {
        auto *row = new QTreeWidgetItem(parent);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setData(0, Qt::UserRole, path);
        row->setData(0, Qt::UserRole + 1, stream.index);
        row->setText(0, QString("%1 #%2").arg(typeLabel(stream.type)).arg(stream.index));
        row->setText(1, stream.type == "audio" ? AudioInfo::codecName(stream) : stream.codec.toUpper());
        row->setText(2, trackDetail(stream));
        row->setText(3, stream.language);
        row->setCheckState(0, Qt::Unchecked);
        if (stream.type == "audio") {
            const QString details = AudioInfo::toolTip(stream);
            for (int col = 0; col < 4; ++col) row->setToolTip(col, details);
        } else row->setToolTip(0, stream.title);
    }
    parent->setExpanded(true);
    // For easy first use, select first file's first video + audio only.
    // Extra files can then contribute any number of audio and subtitle tracks.
    if (!sourceOrder_.isEmpty() && sourceOrder_.first() == path && selected_.isEmpty()) {
        bool videoChosen = false;
        bool audioChosen = false;
        for (const auto &s : info.streams) {
            if ((s.type == "video" && !videoChosen) || (s.type == "audio" && !audioChosen)) {
                auto *item = sourceRow(path, s.index);
                if (item) item->setCheckState(0, Qt::Checked);
                MuxTrack t {path, s.index, s.type, s.codec, s.language, s.title, s.type != "subtitle", s.isForced, s.duration > 0 ? s.duration : info.duration};
                t.durationEstimated = !s.durationKnown;
                t.startTime = s.startTime;
                selected_.append(t);
                if (s.type == "video") videoChosen = true;
                if (s.type == "audio") audioChosen = true;
            }
        }
        syncOutputList();
    }
    updateChapterChoices();
    updateSuggestion();
    updateSummary();
}
void MuxWindow::onSourceToggle(QTreeWidgetItem *item, int column) {
    if (column != 0 || engine_.running() || analysingAudio_) return;
    const QVariant idx = item->data(0, Qt::UserRole + 1);
    if (!idx.isValid()) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    const int index = idx.toInt();
    const bool wanted = item->checkState(0) == Qt::Checked;
    int match = -1;
    for (int i = 0; i < selected_.size(); ++i)
        if (selected_[i].source == path && selected_[i].streamIndex == index) { match = i; break; }
    if (!wanted) {
        if (match >= 0) selected_.removeAt(match);
        syncOutputList();
        return;
    }
    if (match >= 0 || !sources_.contains(path)) return;
    const MediaInfo &info = sources_.value(path);
    for (const auto &stream : info.streams) {
        if (stream.index != index) continue;
        if (stream.type == "video") {
            // The simple muxing mode outputs a single video track.
            for (int i = selected_.size() - 1; i >= 0; --i) {
                if (selected_[i].type == "video") {
                    auto *old = sourceRow(selected_[i].source, selected_[i].streamIndex);
                    if (old) { const QSignalBlocker block(sourcesTree_); old->setCheckState(0, Qt::Unchecked); }
                    selected_.removeAt(i);
                }
            }
        }
        bool alreadyDefault = false;
        for (const auto &t : selected_) if (t.type == stream.type && t.isDefault) alreadyDefault = true;
        MuxTrack t {path, stream.index, stream.type, stream.codec, stream.language, stream.title,
                    !alreadyDefault && (stream.type != "subtitle" || stream.isDefault), stream.isForced,
                    stream.duration > 0 ? stream.duration : info.duration};
        t.durationEstimated = !stream.durationKnown;
        t.startTime = stream.startTime;
        if (stream.type == "video") selected_.prepend(t);
        else selected_.append(t);
        syncOutputList();
        return;
    }
}
void MuxWindow::onSelectedEdited(QTreeWidgetItem *item, int column) {
    if (engine_.running() || analysingAudio_) return;
    const int row = selectedTree_->indexOfTopLevelItem(item);
    if (row < 0 || row >= selected_.size()) return;
    MuxTrack &t = selected_[row];
    if (column == 3) t.language = item->text(3).trimmed().toLower();
    else if (column == 4) t.title = item->text(4).trimmed();
    else if (column == 5) {
        bool ok = false;
        const qint64 delay = item->text(5).trimmed().toLongLong(&ok);
        if (t.type != "audio" || !ok || delay < -43200000 || delay > 43200000) {
            // Do not call syncOutputList() here: it clears the very tree item
            // whose itemChanged signal is currently being delivered (use-after-free).
            const QSignalBlocker block(selectedTree_);
            item->setText(5, t.type == "audio" ? QString::number(t.delayMs) : QStringLiteral("—"));
            QMessageBox::warning(this, "Invalid audio delay", "Enter a whole number of milliseconds from -43200000 to 43200000 (negative starts earlier; positive starts later).");
            return;
        }
        t.delayMs = delay;
        updateSummary();
        return;
    } else if (column == 6) {
        const bool checked = item->checkState(6) == Qt::Checked;
        t.isDefault = checked;
        if (checked) {
            // Update other defaults in place; destroying/recreating a QTreeWidgetItem
            // synchronously from its own itemChanged callback can crash Qt.
            const QString type = t.type;
            const QSignalBlocker block(selectedTree_);
            for (int i = 0; i < selected_.size(); ++i) {
                if (i == row || selected_[i].type != type) continue;
                selected_[i].isDefault = false;
                if (auto *peer = selectedTree_->topLevelItem(i))
                    peer->setCheckState(6, Qt::Unchecked);
            }
        }
    } else if (column == 7) t.isForced = item->checkState(7) == Qt::Checked;
    updateSummary();
}
void MuxWindow::syncOutputList(int preferredRow) {
    if (preferredRow < 0) preferredRow = selectedTree_->currentIndex().row();
    const QSignalBlocker block(selectedTree_);
    selectedTree_->clear();
    for (const auto &t : selected_) {
        auto *row = new QTreeWidgetItem(selectedTree_);
        row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        row->setText(0, typeLabel(t.type));
        row->setText(1, QFileInfo(t.source).fileName());
        if (t.type != "audio") row->setToolTip(1, t.source);
        QString codecLabel = t.codec.toUpper() + (t.type == "subtitle" && t.codec == "mov_text" ? " → SRT" : "");
        if (t.type == "audio") {
            auto it = sources_.constFind(t.source);
            if (it != sources_.cend()) {
                for (const auto &stream : it->streams) {
                    if (stream.type == "audio" && stream.index == t.streamIndex) {
                        codecLabel = AudioInfo::codecName(stream);
                        const QString details = AudioInfo::toolTip(stream);
                        for (int col = 0; col < 8; ++col) row->setToolTip(col, details);
                        break;
                    }
                }
            }
        }
        row->setText(2, codecLabel);
        row->setText(3, t.language);
        row->setText(4, t.title);
        row->setText(5, t.type == "audio" ? QString::number(t.delayMs) : QStringLiteral("—"));
        if (t.type == "audio")
            row->setToolTip(5, QString("Audio begins %1 ms relative to the original timeline. Positive is later; negative is earlier. No re-encoding.").arg(t.delayMs));
        row->setCheckState(6, t.isDefault ? Qt::Checked : Qt::Unchecked);
        row->setCheckState(7, t.isForced ? Qt::Checked : Qt::Unchecked);
    }
    if (preferredRow >= 0 && preferredRow < selectedTree_->topLevelItemCount())
        selectedTree_->setCurrentItem(selectedTree_->topLevelItem(preferredRow));
    updateSuggestion();
    updateSummary();
}
void MuxWindow::removeTrack() {
    if (engine_.running() || analysingAudio_) return;
    const int row = selectedTree_->indexOfTopLevelItem(selectedTree_->currentItem());
    if (row < 0 || row >= selected_.size()) return;
    auto *source = sourceRow(selected_[row].source, selected_[row].streamIndex);
    if (source) { source->setCheckState(0, Qt::Unchecked); }
    else { selected_.removeAt(row); syncOutputList(row); }
}
void MuxWindow::shiftSelected(int direction) {
    if (engine_.running() || analysingAudio_) return;
    const int row = selectedTree_->indexOfTopLevelItem(selectedTree_->currentItem());
    const int to = row + direction;
    if (row < 0 || to < 0 || to >= selected_.size()) return;
    selected_.swapItemsAt(row, to);
    syncOutputList(to);
}
void MuxWindow::removeFile() {
    if (engine_.running() || analysingAudio_) return;
    auto *item = sourcesTree_->currentItem();
    if (!item) return;
    while (item->parent()) item = item->parent();
    const QString path = item->data(0, Qt::UserRole).toString();
    for (int i = selected_.size() - 1; i >= 0; --i) if (selected_[i].source == path) selected_.removeAt(i);
    delete sourcesTree_->takeTopLevelItem(sourcesTree_->indexOfTopLevelItem(item));
    sources_.remove(path);
    sourceOrder_.removeAll(path);
    updateChapterChoices();
    syncOutputList();
}
void MuxWindow::updateChapterChoices() {
    const QString old = chapterSource_->currentData().toString();
    const QSignalBlocker blocked(chapterSource_);
    chapterSource_->clear();
    chapterSource_->addItem("Don't include chapters", "");
    for (const auto &p : sourceOrder_) {
        if (sources_.value(p).hasChapters) chapterSource_->addItem(QFileInfo(p).fileName(), p);
    }
    const int matching = chapterSource_->findData(old);
    if (matching >= 0) chapterSource_->setCurrentIndex(matching);
    // Otherwise leave chapters disabled. Users must choose the source explicitly.
}
void MuxWindow::updateSuggestion() {
    if (!autoOutput_) return;
    QString path;
    for (const auto &t : selected_) if (t.type == "video") { path = t.source; break; }
    if (path.isEmpty() && !sourceOrder_.isEmpty()) path = sourceOrder_.first();
    if (!path.isEmpty()) outputField_->setText(Mux::suggestedOutput(path));
}
void MuxWindow::autoAlignAudio() {
    if (engine_.running() || analysingAudio_) return;
    const MuxTrack *reference = nullptr;
    for (const auto &t : selected_) {
        if (t.type == "video" && std::isfinite(t.duration) && t.duration > 0.0) {
            reference = &t;
            break;
        }
    }
    if (!reference) {
        for (const auto &t : selected_) {
            if (t.type == "audio" && std::isfinite(t.duration) && t.duration > 0.0 &&
                (!reference || t.duration > reference->duration)) reference = &t;
        }
    }
    if (!reference) {
        QMessageBox::information(this, "Duration unavailable", "Import a source with a detectable video or audio duration first.");
        return;
    }
    int changes = 0;
    bool approximate = reference->durationEstimated;
    for (const auto &t : selected_) {
        if (t.type != "audio" || &t == reference) continue;
        if (!(std::isfinite(t.duration) && t.duration > 0.0)) continue;
        if ((reference->startTime + reference->duration) - (t.startTime + t.duration) > 0.05) ++changes;
        approximate = approximate || t.durationEstimated;
    }
    if (changes == 0) {
        QMessageBox::information(this, "Nothing to align", "No audio track is clearly shorter than the selected video (or longest audio track). Existing manual delays were not changed.");
        return;
    }
    const QString referenceName = QFileInfo(reference->source).fileName();
    QString message = QString("Harbinger can end-align %1 shorter audio track(s) with %2 (%3 s).\n\n"
        "This only compares durations; it does NOT detect where dialogue starts. "
        "If the missing material is at the END, the resulting audio will be out of sync.\n\n"
        "Proceed and replace all existing audio delays with the calculated values?")
        .arg(changes).arg(referenceName).arg(reference->duration, 0, 'f', 3);
    if (approximate) message += "\n\nWarning: some track lengths come from container-level estimates, not confirmed stream durations.";
    if (QMessageBox::question(this, "Align audio endings (estimated)", message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const double referenceEnd = reference->startTime + reference->duration;
    for (auto &t : selected_) {
        if (t.type != "audio") continue;
        const double difference = referenceEnd - (t.startTime + t.duration);
        if (std::isfinite(t.duration) && t.duration > 0.0 && difference > 0.05)
            t.delayMs = std::clamp<qint64>(qRound64(difference * 1000.0), qint64{0}, qint64{43200000});
        else t.delayMs = 0;
    }
    syncOutputList();
}
void MuxWindow::autoSyncWaveforms() {
    if (engine_.running() || analysingAudio_) return;
    QVector<MuxTrack> audio;
    for (const auto &t : selected_) if (t.type == "audio") audio.append(t);
    if (audio.size()<2) {
        QMessageBox::information(this,"Waveform sync", "Select at least two audio tracks to compare.");
        return;
    }
    QStringList labels;
    for (const auto &t : audio) labels.append(QString("%1 · #%2 · %3")
        .arg(QFileInfo(t.source).fileName()).arg(t.streamIndex).arg(t.language));
    bool accepted=false;
    const QString choice=QInputDialog::getItem(this,"Waveform sync — reference track",
        "Choose the track whose timing should remain unchanged:", labels,0,false,&accepted);
    if (!accepted) return;
    const int referenceIndex=labels.indexOf(choice);
    if (referenceIndex<0) return;
    const MuxTrack reference=audio.at(referenceIndex);
    audio.removeAt(referenceIndex);
    if (QMessageBox::question(this,"Analyse audio waveforms",
        QString("Compare %1 selected audio track(s) against:\n%2\n\n"
                "Harbinger will decode short samples for matching only. It will not change "
                "or encode your original audio. Results are estimates and must be reviewed. "
                "Some dubbed languages and alternate cuts cannot be matched reliably.\n\nProceed?")
          .arg(audio.size()).arg(choice), QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes) return;
    analysingAudio_=true;
    // Keep source and selected-track lists immutable during analysis.
    sourcesTree_->setEnabled(false);selectedTree_->setEnabled(false);
    addButton_->setEnabled(false);removeFileButton_->setEnabled(false);
    status_->setText("Analysing audio waveforms…");updateSummary();
    auto *progress = new QProgressDialog("Analysing selected audio tracks…","Cancel",0,audio.size(),this);
    progress->setWindowTitle("Harbinger — Waveform sync");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    auto cancelled=std::make_shared<std::atomic_bool>(false);
    connect(progress,&QProgressDialog::canceled,this,[cancelled] {cancelled->store(true);});
    auto *thread=QThread::create([this,reference,audio,cancelled,progress]() {
        QVector<WaveSync::Suggestion> proposals;
        for (const auto &track : audio) {
            if (cancelled->load()) break;
            proposals.append(WaveSync::analyse(ffmpeg_,reference,track,*cancelled));
            const int finished=proposals.size();
            QMetaObject::invokeMethod(progress,[progress,finished] {progress->setValue(finished);},Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this,[this,proposals,cancelled,progress]() {
            analysingAudio_=false;
            progress->hide();progress->deleteLater();
            sourcesTree_->setEnabled(true);selectedTree_->setEnabled(true);
            addButton_->setEnabled(true);
            status_->setText(cancelled->load()?"Waveform analysis cancelled":"Waveform analysis completed");
            updateSummary();
            if (cancelled->load()) return;
            QString report;
            int confident=0;
            for (const auto &r : proposals) {
                const QString filename=QFileInfo(r.source).fileName();
                if (r.reliable) {
                    ++confident;
                    report += QString("%1 (audio #%2): proposed %3 ms · score %4%5\n")
                        .arg(filename).arg(r.streamIndex).arg(r.delayMs)
                        .arg(r.correlation,0,'f',2)
                        .arg(r.secondCorrelation>0?QString(" / %1").arg(r.secondCorrelation,0,'f',2):QString());
                } else report += QString("%1 (audio #%2): not applied — %3\n")
                    .arg(filename).arg(r.streamIndex).arg(r.explanation);
            }
            if (!confident) {
                QMessageBox::information(this,"Waveform sync — no reliable match",
                    report + "\nTry the manual delay control, or align with an external audio editor.");
                return;
            }
            const bool apply=QMessageBox::question(this,"Waveform sync — review suggestions",
                report+"\nApply only the confidently matched offsets? Existing delays for unmatched tracks are left unchanged. "
                       "Please verify actual dialogue and subtitle sync after muxing.",
                       QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
            if (!apply) return;
            for (const auto &r : proposals) if (r.reliable)
                for (auto &t : selected_) if (t.type=="audio" && t.source==r.source && t.streamIndex==r.streamIndex)
                    t.delayMs=r.delayMs;
            syncOutputList();
        },Qt::QueuedConnection);
    });
    connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    thread->start();
}

void MuxWindow::resetAudioDelays() {
    if (engine_.running() || analysingAudio_) return;
    for (auto &t : selected_) if (t.type == "audio") t.delayMs = 0;
    syncOutputList();
}
void MuxWindow::updateSummary() {
    int v = 0, a = 0, s = 0;
    int movText = 0;
    for (const auto &t : selected_) {
        if (t.type == "video") ++v;
        if (t.type == "audio") ++a;
        if (t.type == "subtitle") ++s;
        if (t.codec == "mov_text") ++movText;
    }
    QString summary = QString("Selected: %1 video · %2 audio · %3 subtitle tracks. Stream copy (no video/audio re-encoding).").arg(v).arg(a).arg(s);
    if (movText) summary += QString(" %1 MP4 timed-text subtitle track(s) will be converted to SRT.").arg(movText);
    int delayed = 0;
    for (const auto &t : selected_) if (t.type == "audio" && t.delayMs != 0) ++delayed;
    if (delayed) summary += QString(" %1 audio delay(s) will shift timestamps without re-encoding.").arg(delayed);
    summary_->setText(summary);
    const bool idle = !engine_.running() && !analysingAudio_;
    const int row = selectedTree_->indexOfTopLevelItem(selectedTree_->currentItem());
    upButton_->setEnabled(idle && row > 0);
    downButton_->setEnabled(idle && row >= 0 && row < selected_.size() - 1);
    removeTrackButton_->setEnabled(idle && row >= 0);
    int audioCount = 0;
    for (const auto &t : selected_) if (t.type == "audio") ++audioCount;
    alignAudioButton_->setEnabled(idle && audioCount > 0);
    waveSyncButton_->setEnabled(idle && audioCount > 1);
    resetDelayButton_->setEnabled(idle && audioCount > 0);
    batchButton_->setEnabled(idle && !selected_.isEmpty() && !probe_.isBusy());
    removeFileButton_->setEnabled(idle && sourcesTree_->currentItem());
    muxButton_->setEnabled(idle && !selected_.isEmpty() && !outputField_->text().trimmed().isEmpty());
}
MuxPlan MuxWindow::currentPlan() const {
    MuxPlan plan;
    plan.tracks = selected_;
    plan.output = outputField_->text().trimmed();
    plan.chapterSource = chapterSource_->currentData().toString();
    plan.preserveGlobalMetadata = metadataCheck_->isChecked();
    return plan;
}
void MuxWindow::pickOutput() {
    const QString file = QFileDialog::getSaveFileName(this, "Save combined MKV", outputField_->text(), "Matroska video (*.mkv)");
    if (file.isEmpty()) return;
    autoOutput_ = false;
    outputField_->setText(file.endsWith(".mkv", Qt::CaseInsensitive) ? file : file + ".mkv");
}
void MuxWindow::setRunning(bool busy) {
    addButton_->setEnabled(!busy);
    removeFileButton_->setEnabled(!busy);
    sourcesTree_->setEnabled(!busy);
    selectedTree_->setEnabled(!busy);
    outputField_->setEnabled(!busy);
    browseButton_->setEnabled(!busy);
    chapterSource_->setEnabled(!busy);
    metadataCheck_->setEnabled(!busy);
    alignAudioButton_->setEnabled(!busy);
    waveSyncButton_->setEnabled(!busy);
    resetDelayButton_->setEnabled(!busy);
    batchButton_->setEnabled(!busy);
    cancelButton_->setEnabled(busy);
    updateSummary();
}
void MuxWindow::startMux() {
    if (engine_.running() || analysingAudio_) return;
    const MuxPlan plan = currentPlan();
    const QString issue = Mux::validate(plan);
    if (!issue.isEmpty()) {
        QMessageBox::warning(this, "Check mux settings", issue);
        return;
    }
    progressBar_->setValue(0);
    status_->setText("Combining tracks — copying streams into Matroska…");
    if (engine_.start(plan) && engine_.running()) setRunning(true);
}
void MuxWindow::cancelMux() {
    if (!engine_.running()) return;
    cancelButton_->setEnabled(false);
    status_->setText("Cancelling mux…");
    engine_.cancel();
}
void MuxWindow::closeEvent(QCloseEvent *event) {
    if (analysingAudio_) {
        QMessageBox::information(this, "Audio analysis", "Cancel the waveform analysis before closing the muxer.");
        event->ignore();
        return;
    }
    if (engine_.running()) {
        if (QMessageBox::question(this, "Muxing in progress", "Cancel muxing and close this window?") == QMessageBox::Yes) {
            closeAfterCancel_ = true;
            cancelMux();
        }
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}
void MuxWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (!engine_.running() && !analysingAudio_ && event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MuxWindow::dropEvent(QDropEvent *event) {
    if (engine_.running() || analysingAudio_) return;
    QStringList files;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) files << url.toLocalFile();
    importFiles(files);
    event->acceptProposedAction();
}

void MuxWindow::openBatchMux() {
    if (engine_.running() || analysingAudio_ || probe_.isBusy() || selected_.isEmpty()) return;
    auto *window = new BatchMuxWindow(ffmpeg_, ffprobe_, currentPlan(), this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}
