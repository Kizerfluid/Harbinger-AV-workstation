#include "HardwareDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

HardwareDialog::HardwareDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Harbinger — Hardware Capabilities");
    resize(820, 530);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("GPU video capabilities · detection is specific to your current FFmpeg build and drivers");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    device_ = new QComboBox;
    layout->addWidget(device_);
    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({"Codec", "Hardware decoding", "Hardware encoding"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    info_ = new QLabel;
    info_->setWordWrap(true);
    layout->addWidget(info_);
    notice_ = new QLabel("'Test passed' means FFmpeg completed a short codec probe requesting GPU operation. "
        "It does not guarantee every resolution, bit depth, profile, or driver combination. "
        "'Not available' means no encoder is listed or its runtime probe failed. "
        "'Reported / unverified' means FFmpeg lists the API but the probe could not complete. "
        "Hover over a cell to see diagnostic details.");
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    scan_ = buttons->addButton("Rescan hardware", QDialogButtonBox::ActionRole);
    connect(scan_, &QPushButton::clicked, this, &HardwareDialog::rescanRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(device_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &HardwareDialog::populate);
}
void HardwareDialog::setScanning(bool scanning) {
    scan_->setEnabled(!scanning);
    if (scanning) info_->setText("Scanning GPUs and performing short FFmpeg encode/decode probes…");
}
void HardwareDialog::showReport(const Hardware::Report &report) {
    report_ = report;
    const QString selected = device_->currentData().toString();
    device_->blockSignals(true);
    device_->clear();
    for (const auto &gpu : report_.devices)
        device_->addItem(gpu.name + " · " + gpu.backend.toUpper(), gpu.id);
    int at = device_->findData(selected);
    device_->setCurrentIndex(at >= 0 ? at : 0);
    device_->blockSignals(false);
    populate();
}
void HardwareDialog::populate() {
    table_->setRowCount(0);
    const int index = device_->currentIndex();
    if (index < 0 || index >= report_.devices.size()) {
        info_->setText("No supported GPU adapters detected.\n" + report_.warnings.join("\n")); return;
    }
    const auto gpu = report_.devices.at(index);
    const auto codes = Hardware::codecIds();
    table_->setRowCount(codes.size());
    for (int i = 0; i < codes.size(); ++i) {
        const auto &cap = gpu.codecs.value(codes.at(i));
        auto *name = new QTableWidgetItem(Hardware::codecName(codes.at(i)));
        auto *dec = new QTableWidgetItem(Hardware::hardwareName(cap.decode));
        auto *enc = new QTableWidgetItem(Hardware::hardwareName(cap.encode));
        dec->setToolTip("API: " + cap.decodeBackend + "\n" + cap.decodeNote);
        enc->setToolTip("Encoder: " + cap.encoder + "\n" + cap.encodeNote);
        table_->setItem(i, 0, name);
        table_->setItem(i, 1, dec);
        table_->setItem(i, 2, enc);
    }
    const QString note = gpu.selectable ? "This GPU can be individually selected for supported encoding backends."
        : "This backend uses FFmpeg's default adapter. Specific-adapter selection is not verified.";
    info_->setText(note + (report_.warnings.isEmpty() ? QString{} : "\n" + report_.warnings.join("\n")));
}
