#pragma once
#include "Types.h"
#include <QMap>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QMetaType>

namespace Hardware {
// "verified" means a real short FFmpeg probe completed. Decoder tests explicitly
// request hardware frames, but driver / FFmpeg behaviour can differ by media profile.
enum class State { Unavailable, Listed, Verified };
struct Capability {
    State encode = State::Unavailable;
    State decode = State::Unavailable;
    QString encoder;
    QString decodeBackend;
    QString encodeNote;
    QString decodeNote;
};
struct Device {
    QString id;        // stable identifier: nvenc:<index>, vaapi:<renderNode>, or automatic
    QString name;
    QString vendor;    // nvidia / intel / amd
    QString backend;   // nvenc / qsv / amf / vaapi
    QString locator;   // NVENC ordinal, VAAPI render node, or empty (default adapter)
    bool selectable = false; // distinct physical GPU can be selected in ffmpeg
    QMap<QString, Capability> codecs;
};
struct Report {
    QSet<QString> encoders;
    QSet<QString> hwaccels;
    QList<Device> devices;
    QStringList warnings;
};
struct Choice {
    bool ok = true;
    QString error;
    QString encoder;
    QString encoderBackend;
    QString gpuId;
    QString locator;
    QString decodeBackend;
    QString decodeLocator;
    QString softwareEncoder;
    bool hardwareEncode = false;
    bool hardwareDecode = false;
};
QStringList codecIds();
QString codecName(const QString &id);
QString codecForEncoder(const QString &encoder);
QString softwareEncoder(const QString &codec, const QSet<QString> &encoders);
QString hardwareEncoder(const QString &codec, const QString &backend);
QString hardwareName(State state);
Report scan(const QString &ffmpegPath);
Choice select(const EncodeOptions &options, const MediaInfo &media, const Report &report);
QStringList inputArgs(const Choice &choice);
QStringList outputArgs(const Choice &choice);
}

class HardwareScanner final : public QThread {
    Q_OBJECT
public:
    explicit HardwareScanner(QString ffmpegExecutable, QObject *parent = nullptr);
    void cancel();
Q_SIGNALS:
    void reportReady(const Hardware::Report &report);
protected:
    void run() override;
private:
    QString executable_;
};

Q_DECLARE_METATYPE(Hardware::Report)
