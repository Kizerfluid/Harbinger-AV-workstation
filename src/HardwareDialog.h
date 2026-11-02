#pragma once
#include "Hardware.h"
#include <QDialog>
class QComboBox;
class QLabel;
class QTableWidget;

class HardwareDialog final : public QDialog {
    Q_OBJECT
public:
    explicit HardwareDialog(QWidget *parent = nullptr);
    void showReport(const Hardware::Report &report);
    void setScanning(bool scanning);
Q_SIGNALS:
    void rescanRequested();
private:
    void populate();
    Hardware::Report report_;
    QComboBox *device_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *info_ = nullptr;
    QLabel *notice_ = nullptr;
    class QPushButton *scan_ = nullptr;
};
