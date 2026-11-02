#include "MainWindow.h"
#include "HiveWorker.h"
#include "HiveProtocol.h"
#include "Hardware.h"
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QApplication>
#include <QIcon>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace {
int runHeadlessWorker(int argc, char *argv[]) {
    // Do not create QApplication: openSUSE servers running through SSH need
    // neither DISPLAY nor a Qt GUI platform plugin.
    QCoreApplication app(argc,argv);
    app.setApplicationName("Harbinger");
    app.setOrganizationName("Harbinger");
    app.setApplicationVersion("0.6.3");
    QString allowHost;
    QString ffmpeg="ffmpeg";
    bool cpuOnly=false;
    int threads=0;
    for (int i=1;i<argc;++i) {
        const QString arg=QString::fromLocal8Bit(argv[i]);
        if (arg=="--hive-worker") continue;
        if ((arg=="--allow-host" || arg=="--ffmpeg" || arg=="--threads") && i+1<argc) {
            const QString value=QString::fromLocal8Bit(argv[++i]);
            if (arg=="--allow-host") allowHost=value;
            else if (arg=="--ffmpeg") ffmpeg=value;
            else {
                bool ok=false;threads=value.toInt(&ok);
                if (!ok || threads<0 || threads>64) {
                    qCritical().noquote()<<"--threads must be 0..64 (0=auto)";return 2;
                }
            }
        } else if (arg=="--cpu-only") cpuOnly=true;
        else {
            qCritical().noquote()<<"Unknown or incomplete headless option:"<<arg;return 2;
        }
    }
    HiveWorker worker;
    if (!worker.setHeadlessAllowedHost(allowHost)) {
        qCritical().noquote()<<"Usage: Harbinger --hive-worker --allow-host <trusted-Windows-private-IPv4> [--cpu-only] [--threads N] [--ffmpeg PATH]";
        qCritical().noquote()<<"A private LAN host IP is mandatory; no wildcard or unattended open sharing.";
        return 2;
    }
    worker.setFFmpeg(ffmpeg);
    worker.setThreadLimit(threads);
    worker.setAllowGpu(!cpuOnly);
    QObject::connect(&worker,&HiveWorker::logMessage,&app,[](const QString &line) {
        qInfo().noquote()<<line;
    });
    if (!cpuOnly) {
        qInfo().noquote()<<"Scanning FFmpeg/GPU capabilities (this may take a little time)...";
        worker.setHardwareReport(Hardware::scan(ffmpeg));
    }
    QString error;
    if (!worker.enable(&error)) {
        qCritical().noquote()<<"Hive worker failed to start:"<<error;
        return 1;
    }
    qInfo().noquote()<<"Harbinger Hive v"<<HiveProtocol::Version<<"headless worker ready on TCP 36840";
    qInfo().noquote()<<"TLS verification code:"<<worker.fingerprint().left(16);
    qInfo().noquote()<<"Full TLS certificate SHA-256:"<<worker.fingerprint();
    qInfo().noquote()<<"Only the configured host IP is accepted. Keep this port on your trusted LAN only.";
    return app.exec();
}
}
int main(int argc, char *argv[]) {
    for (int i=1;i<argc;++i)
        if (QString::fromLocal8Bit(argv[i])=="--hive-worker") return runHeadlessWorker(argc,argv);
    QApplication app(argc, argv);
    app.setApplicationName("Harbinger");
    app.setWindowIcon(QIcon(":/harbinger/icon.png"));
    app.setOrganizationName("Harbinger");
    app.setApplicationVersion("0.6.3");
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#151A24"));
    palette.setColor(QPalette::WindowText, QColor("#EDF1F7"));
    palette.setColor(QPalette::Base, QColor("#10151E"));
    palette.setColor(QPalette::AlternateBase, QColor("#1A2230"));
    palette.setColor(QPalette::Text, QColor("#EDF1F7"));
    palette.setColor(QPalette::Button, QColor("#273244"));
    palette.setColor(QPalette::ButtonText, QColor("#EDF1F7"));
    palette.setColor(QPalette::Highlight, QColor("#3897DD"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor("#8894A8"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#748092"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#748092"));
    app.setPalette(palette);

    app.setStyleSheet(R"(
        QMainWindow, QWidget { background: #151A24; color: #EDF1F7; font-size: 12px; }
        QLabel#brand { color: #EAF6FF; font-size: 26px; font-weight: 800; }
        QLabel#muted { color: #95A3B8; font-size: 11px; }
        QLabel#backend { background: #20364A; border: 1px solid #34546A; border-radius: 10px;
                         padding: 8px 12px; color: #B8E3FF; }
        QLabel#sectionLabel { color: #A8B9CF; font-weight: 700; letter-spacing: 1px; }
        QLabel#summary { background: #1A2A38; border: 1px solid #294355; border-radius: 8px; padding: 12px; }
        QGroupBox { border: 1px solid #334055; border-radius: 10px; margin-top: 17px; padding: 12px 9px 9px 9px; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; color: #D4E7F8; padding: 0 6px; }
        QPushButton { background: #273244; border: 1px solid #405169; border-radius: 8px; padding: 9px 13px; }
        QPushButton:hover { background: #32435A; }
        QPushButton:pressed { background: #203247; }
        QPushButton:disabled { background: #242B38; border-color: #343D4C; color: #748092; }
        QPushButton#primary { background: #1979CC; border-color: #3995E3; color: white; font-weight: 700; }
        QPushButton#primary:hover { background: #288FE6; }
        QPushButton#primary:disabled { background: #31445B; border-color: #3B5067; color: #8D9FB4; }
        QPushButton#primarySubtle { background: #203E61; border-color: #3A6895; color: #D6EAFF; font-weight: 700; }
        QComboBox, QLineEdit, QPlainTextEdit { border: 1px solid #3A4960; border-radius: 7px;
                 background: #101721; padding: 7px; selection-background-color: #277FC3; }
        QComboBox QAbstractItemView { background: #1B2533; color: #EDF1F7; selection-background-color: #27679D; }
        QTreeWidget { border: 1px solid #334055; border-radius: 8px; background: #111924;
                      alternate-background-color: #17212E; }
        QHeaderView::section { background: #273244; color: #DCE9F7; border: none; border-right: 1px solid #3A485B;
                               padding: 9px; font-weight: 600; }
        QTreeWidget::item:selected { background: #214A70; }
        QProgressBar { background: #1B2B3A; border: 1px solid #385069; border-radius: 6px;
                       text-align: center; color: white; min-height: 14px; }
        QProgressBar::chunk { background: #36A0EA; border-radius: 5px; }
        QMenuBar { background: #151A24; }
        QMenuBar::item:selected, QMenu::item:selected { background: #284866; }
        QMenu { background: #1C2634; border: 1px solid #3B4C61; }
        QScrollArea { background: transparent; }
        QStatusBar { color: #9EBCD7; }
    )");
    MainWindow window;
    window.show();
    return app.exec();
}
