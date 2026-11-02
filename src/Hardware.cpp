#include "Hardware.h"
#include "Codec.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QtGlobal>
#include <utility>

namespace {
struct RunResult { bool ok = false; QString output; QString error; bool completed = false; };
RunResult run(const QString &program, const QStringList &args, int timeoutMs = 6000) {
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args);
    if (!p.waitForStarted(2000)) return {false, {}, p.errorString()};
    if (!p.waitForFinished(timeoutMs)) {
        p.kill(); p.waitForFinished(1500);
        return {false, {}, "Timed out"};
    }
    const bool success = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return {success, QString::fromUtf8(p.readAllStandardOutput()),
        QString::fromUtf8(p.readAllStandardError()).right(2000), true};
}
QString readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}
QString vendorName(QString vendor) {
    vendor = vendor.trimmed().toLower();
    if (vendor == "0x10de") return "nvidia";
    if (vendor == "0x8086") return "intel";
    if (vendor == "0x1002") return "amd";
    return {};
}
QString backendForVendor(const QString &vendor) {
#ifdef Q_OS_WIN
    if (vendor == "intel") return "qsv";
    if (vendor == "amd") return "amf";
#else
    if (vendor == "intel" || vendor == "amd") return "vaapi";
#endif
    return {};
}
QList<Hardware::Device> discoverDevices(QStringList &warnings) {
    QList<Hardware::Device> devices;
    // NVENC ordinals are independently addressable by FFmpeg's -gpu option.
    const auto nv = run("nvidia-smi", {"--query-gpu=index,name", "--format=csv,noheader"}, 3000);
    if (nv.ok) {
        for (QString line : nv.output.split('\n', Qt::SkipEmptyParts)) {
            const int comma = line.indexOf(',');
            if (comma < 0) continue;
            const QString index = line.left(comma).trimmed();
            bool valid = false; index.toInt(&valid);
            if (!valid) continue;
            Hardware::Device gpu;
            gpu.id = "nvenc:" + index; gpu.locator = index;
            gpu.name = line.mid(comma + 1).trimmed(); gpu.vendor = "nvidia";
            gpu.backend = "nvenc"; gpu.selectable = true;
            devices.append(gpu);
        }
    }
#ifdef Q_OS_LINUX
    QDir sys("/sys/class/drm");
    bool foundNvidiaNode = false;
    for (const QString &entry : sys.entryList({"renderD*"}, QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString base = sys.filePath(entry) + "/device/";
        const QString vendor = vendorName(readFile(base + "vendor"));
        if (vendor == "nvidia") foundNvidiaNode = true;
        const QString backend = backendForVendor(vendor);
        if (backend.isEmpty()) continue;
        const QString node = "/dev/dri/" + entry;
        if (!QFileInfo::exists(node)) continue;
        Hardware::Device gpu;
        gpu.id = "vaapi:" + node; gpu.locator = node;
        gpu.name = vendor.toUpper() + " GPU (" + entry + ")";
        gpu.vendor = vendor; gpu.backend = backend; gpu.selectable = true;
        if (!QFileInfo(node).isReadable()) warnings << node + " is not readable; check render/video group permissions.";
        devices.append(gpu);
    }
    if (foundNvidiaNode && !nv.ok) {
        Hardware::Device gpu;
        gpu.id = "auto:nvenc"; gpu.name = "NVIDIA GPU (FFmpeg default adapter)";
        gpu.vendor = "nvidia"; gpu.backend = "nvenc"; gpu.selectable = false;
        devices.append(gpu);
        warnings << "nvidia-smi was not available; NVIDIA device selection uses the FFmpeg default adapter.";
    }
#elif defined(Q_OS_WIN)
    // CIM provides user-visible names but not a stable FFmpeg adapter index.
    // AMD AMF / Intel QSV probes therefore refer to the default adapter, not
    // necessarily to the physical device identified by CIM.
    const auto w = run("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
        "Get-CimInstance Win32_VideoController | Select-Object Name,PNPDeviceID | ConvertTo-Json -Compress"}, 7000);
    if (w.ok) {
        QJsonParseError e;
        const auto doc = QJsonDocument::fromJson(w.output.toUtf8(), &e);
        QList<QJsonObject> gpus;
        if (doc.isObject()) gpus << doc.object();
        if (doc.isArray()) for (const auto &v : doc.array()) if (v.isObject()) gpus << v.toObject();
        QSet<QString> seenDefaultBackends;
        for (const auto &obj : gpus) {
            const QString pnp = obj.value("PNPDeviceID").toString().toUpper();
            const QString vendor = pnp.contains("VEN_8086") ? "intel" : pnp.contains("VEN_1002") ? "amd" : pnp.contains("VEN_10DE") ? "nvidia" : QString{};
            if (vendor == "nvidia") {
                if (nv.ok || seenDefaultBackends.contains("nvenc")) continue;
                Hardware::Device gpu;
                gpu.id = "auto:nvenc"; gpu.name = obj.value("Name").toString() + " (FFmpeg default adapter)";
                gpu.vendor = vendor; gpu.backend = "nvenc"; gpu.selectable = false;
                devices.append(gpu);
                seenDefaultBackends.insert("nvenc");
                warnings << "nvidia-smi unavailable; specific NVIDIA GPU selection is not possible.";
                continue;
            }
            const QString backend = backendForVendor(vendor);
            if (backend.isEmpty() || seenDefaultBackends.contains(backend)) continue;
            Hardware::Device gpu;
            gpu.id = "auto:" + backend;
            gpu.name = obj.value("Name").toString() + " (FFmpeg default adapter)";
            gpu.vendor = vendor; gpu.backend = backend; gpu.selectable = false;
            devices.append(gpu);
            seenDefaultBackends.insert(backend);
        }
    }
#endif
    return devices;
}
QStringList deviceArgs(const Hardware::Device &device) {
    if (device.backend == "vaapi") {
        return {"-init_hw_device", "vaapi=harb:" + device.locator, "-filter_hw_device", "harb"};
    }
    return {};
}
QStringList encodeTestArgs(const Hardware::Device &device, const QString &encoder) {
    QStringList args = {"-hide_banner", "-nostdin", "-loglevel", "error", "-y"};
    args << deviceArgs(device);
    args << "-f" << "lavfi" << "-i" << "testsrc2=size=640x360:rate=5" << "-frames:v" << "2";
    if (device.backend == "vaapi") args << "-vf" << "format=nv12,hwupload";
    else args << "-pix_fmt" << "nv12";
    args << "-an" << "-c:v" << encoder;
    if (device.backend == "nvenc" && !device.locator.isEmpty()) args << "-gpu" << device.locator;
    args << "-f" << "null" << "-";
    return args;
}
QString sampleEncoder(const QString &codec, const QSet<QString> &encoders) {
    const QStringList candidates = codec == "h264" ? QStringList{"libx264"} :
        codec == "hevc" ? QStringList{"libx265"} :
        codec == "av1" ? QStringList{"libsvtav1", "libaom-av1"} :
        codec == "vp9" ? QStringList{"libvpx-vp9"} :
        codec == "mpeg4" ? QStringList{"mpeg4"} : QStringList{"mpeg2video"};
    for (const auto &v : candidates) if (encoders.contains(v)) return v;
    return {};
}
QString decodeAccel(const Hardware::Device &device, const QSet<QString> &accels) {
    if (device.backend == "nvenc") return accels.contains("cuda") ? "cuda" : QString{};
    if (device.backend == "vaapi") return accels.contains("vaapi") ? "vaapi" : QString{};
    if (device.backend == "qsv") return accels.contains("qsv") ? "qsv" : accels.contains("d3d11va") ? "d3d11va" : QString{};
    if (device.backend == "amf") return accels.contains("d3d11va") ? "d3d11va" : QString{};
    return {};
}
QString frameFormat(const QString &accel) {
    if (accel == "d3d11va") return "d3d11";
    return accel;
}
QStringList decodeTestArgs(const Hardware::Device &device, const QString &accel, const QString &sample) {
    QStringList args = {"-hide_banner", "-nostdin", "-loglevel", "verbose"};
    if (accel == "vaapi") args << "-init_hw_device" << "vaapi=harb:" + device.locator;
    args << "-hwaccel" << accel;
    if (accel == "vaapi") args << "-hwaccel_device" << "harb";
    if (accel == "cuda" && !device.locator.isEmpty()) args << "-hwaccel_device" << device.locator;
    args << "-hwaccel_output_format" << frameFormat(accel) << "-i" << sample
         << "-map" << "0:v:0" << "-frames:v" << "2" << "-an" << "-f" << "null" << "-";
    return args;
}
QString tempSample(const QString &ffmpeg, const QString &codec, const QSet<QString> &encoders, const QString &folder) {
    const QString encoder = sampleEncoder(codec, encoders);
    if (encoder.isEmpty()) return {};
    const QString path = QDir(folder).filePath(codec + ".mkv");
    QStringList args = {"-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "testsrc2=size=640x360:rate=5", "-frames:v", "3", "-an", "-c:v", encoder};
    if (codec == "h264" || codec == "hevc") args << "-preset" << "ultrafast";
    if (codec == "av1" && encoder == "libsvtav1") args << "-preset" << "12";
    if (codec == "vp9") args << "-deadline" << "realtime" << "-cpu-used" << "8";
    args << path;
    const auto result = run(ffmpeg, args, codec == "av1" ? 12000 : 7000);
    return result.ok && QFileInfo::exists(path) ? path : QString{};
}
}

namespace Hardware {
QStringList codecIds() { return {"h264", "hevc", "av1", "vp9", "mpeg4", "mpeg2"}; }
QString codecName(const QString &id) {
    if (id == "h264") return "H.264 / AVC";
    if (id == "hevc") return "H.265 / HEVC";
    if (id == "av1") return "AV1";
    if (id == "vp9") return "VP9";
    if (id == "mpeg2") return "MPEG-2";
    if (id == "mpeg4") return "MPEG-4 Part 2";
    return id.toUpper();
}
QString codecForEncoder(const QString &enc) {
    if (enc.startsWith("h264_") || enc == "libx264") return "h264";
    if (enc.startsWith("hevc_") || enc == "libx265") return "hevc";
    if (enc.startsWith("av1_") || enc == "libsvtav1" || enc == "libaom-av1") return "av1";
    if (enc.startsWith("vp9_") || enc == "libvpx-vp9") return "vp9";
    if (enc == "mpeg4") return "mpeg4";
    if (enc == "mpeg2video") return "mpeg2";
    return {};
}
QString softwareEncoder(const QString &codec, const QSet<QString> &available) {
    const QStringList options = codec == "h264" ? QStringList{"libx264"} :
        codec == "hevc" ? QStringList{"libx265"} :
        codec == "av1" ? QStringList{"libsvtav1", "libaom-av1"} :
        codec == "vp9" ? QStringList{"libvpx-vp9"} :
        codec == "mpeg4" ? QStringList{"mpeg4"} : QStringList{};
    for (const auto &e : options) if (available.contains(e)) return e;
    return {};
}
QString hardwareEncoder(const QString &codec, const QString &backend) {
    if (codec == "h264") {
        if (backend == "nvenc") return "h264_nvenc";
        if (backend == "amf") return "h264_amf";
        if (backend == "qsv") return "h264_qsv";
        if (backend == "vaapi") return "h264_vaapi";
    }
    if (codec == "hevc") {
        if (backend == "nvenc") return "hevc_nvenc";
        if (backend == "amf") return "hevc_amf";
        if (backend == "qsv") return "hevc_qsv";
        if (backend == "vaapi") return "hevc_vaapi";
    }
    if (codec == "av1") {
        if (backend == "nvenc") return "av1_nvenc";
        if (backend == "amf") return "av1_amf";
        if (backend == "qsv") return "av1_qsv";
        if (backend == "vaapi") return "av1_vaapi";
    }
    if (codec == "vp9") {
        if (backend == "qsv") return "vp9_qsv";
        if (backend == "vaapi") return "vp9_vaapi";
    }
    return {};
}
QString hardwareName(State s) {
    if (s == State::Verified) return "Test passed";
    if (s == State::Listed) return "Reported / unverified";
    return "Not available";
}
Report scan(const QString &ffmpegPath) {
    Report report;
    const auto enc = run(ffmpegPath, {"-hide_banner", "-encoders"}, 5000);
    if (!enc.ok) { report.warnings << "Cannot query FFmpeg encoders: " + enc.error; return report; }
    report.encoders = Codec::parseEncoderList(enc.output);
    const auto accel = run(ffmpegPath, {"-hide_banner", "-hwaccels"}, 5000);
    if (accel.ok) for (QString line : accel.output.split('\n')) {
        line = line.trimmed();
        if (QStringList{"cuda", "qsv", "vaapi", "d3d11va", "dxva2", "vdpau"}.contains(line)) report.hwaccels.insert(line);
    }
    report.devices = discoverDevices(report.warnings);
    if (report.devices.isEmpty()) report.warnings << "No supported GPU device could be enumerated. Check drivers and permissions.";
    QTemporaryDir temporary;
    QMap<QString, QString> samples;
    // Only generate tiny decode samples when there is a compatible FFmpeg hardware API.
    bool testDecode = false;
    for (const auto &gpu : report.devices) if (!decodeAccel(gpu, report.hwaccels).isEmpty()) testDecode = true;
    if (testDecode && temporary.isValid()) {
        for (const QString &codec : {"h264", "hevc", "av1", "vp9", "mpeg2", "mpeg4"})
            samples.insert(codec, tempSample(ffmpegPath, codec, report.encoders, temporary.path()));
    }
    for (auto &gpu : report.devices) {
        const QString accelName = decodeAccel(gpu, report.hwaccels);
        for (const QString &codec : codecIds()) {
            Capability cap;
            const QString encoder = hardwareEncoder(codec, gpu.backend);
            if (!encoder.isEmpty() && report.encoders.contains(encoder)) {
                cap.encode = State::Listed; cap.encoder = encoder;
                const auto r = run(ffmpegPath, encodeTestArgs(gpu, encoder), 6000);
                if (r.ok) cap.encode = State::Verified;
                else {
                    cap.encodeNote = r.error.trimmed().right(220);
                    // The process ran and rejected the encoder: unavailable for
                    // this GPU/driver/test configuration, despite FFmpeg listing it.
                    // A startup failure or timeout remains inconclusive.
                    if (r.completed) cap.encode = State::Unavailable;
                }
            }
            if (!accelName.isEmpty()) {
                cap.decode = State::Listed; cap.decodeBackend = accelName;
                const QString sample = samples.value(codec);
                if (!sample.isEmpty()) {
                    const auto r = run(ffmpegPath, decodeTestArgs(gpu, accelName, sample), 6500);
                    // Requiring a hardware-frame output format makes a software
                    // fallback far less likely than using -hwaccel alone.
                    if (r.ok) cap.decode = State::Verified;
                    else cap.decodeNote = r.error.trimmed().right(220);
                } else cap.decodeNote = "No software sample encoder available for testing.";
            }
            gpu.codecs.insert(codec, cap);
        }
    }
    return report;
}
Choice select(const EncodeOptions &o, const MediaInfo &media, const Report &report) {
    Choice c;
    const QString codec = o.videoCodec.isEmpty() ? codecForEncoder(o.videoEncoder) : o.videoCodec;
    c.softwareEncoder = softwareEncoder(codec, report.encoders);
    if (Codec::isAudioOnly(o.container) || o.videoAction != "transcode") {
        c.encoder = o.videoAction == "copy" ? "copy" : QString{};
        return c;
    }
    if (o.videoBitDepth == 10 && !Codec::supports10Bit(codec)) {
        c.ok = false;
        c.error = "10-bit output requires HEVC, AV1 or VP9. Select 8-bit or change the video codec.";
        return c;
    }
    QString encodeMode = o.encodeMode;
    QString decodeMode = o.decodeMode;
    const bool qualityFirst = o.videoQuality <= 1 || o.speed == 2;
    // Automatic means hardware for balanced/fast output when verified, but
    // prefer software for quality-first jobs.
    const bool preferHardware = encodeMode == "gpu" || (encodeMode == "auto" && (!qualityFirst || c.softwareEncoder.isEmpty()));
    QList<const Device *> candidates;
    for (const auto &gpu : report.devices) if (o.gpuId.isEmpty() || gpu.id == o.gpuId) candidates << &gpu;
    if (!o.gpuId.isEmpty() && candidates.isEmpty() && (encodeMode != "cpu" || decodeMode != "cpu")) {
        c.ok = false; c.error = "Selected GPU is no longer available. Refresh Hardware Capabilities."; return c;
    }
    const Device *chosen = nullptr;
    if (preferHardware) for (const Device *gpu : candidates) {
        if (gpu->codecs.value(codec).encode == State::Verified) { chosen = gpu; break; }
    }
    if (chosen) {
        c.hardwareEncode = true; c.encoderBackend = chosen->backend;
        c.encoder = chosen->codecs.value(codec).encoder;
        c.gpuId = chosen->id; c.locator = chosen->locator;
    } else if (!c.softwareEncoder.isEmpty()) {
        c.encoder = c.softwareEncoder;
        if (encodeMode == "gpu") {
            c.ok = false; c.error = "No successfully tested GPU encoder for " + codecName(codec) + ". Choose Software or another GPU.";
            return c;
        }
    } else {
        c.ok = false; c.error = "No supported encoder for " + codecName(codec) + " in this FFmpeg build."; return c;
    }
    if (decodeMode != "cpu") {
        // Use the output GPU first when available to avoid cross-GPU traffic.
        QList<const Device *> decodeCandidates;
        if (chosen) decodeCandidates << chosen;
        for (const Device *gpu : candidates) if (gpu != chosen) decodeCandidates << gpu;
        const QString srcCodec = media.videoCodec.toLower() == "mpeg2video" ? "mpeg2" : media.videoCodec.toLower();
        for (const Device *gpu : decodeCandidates) {
            const Capability cap = gpu->codecs.value(srcCodec);
            if (cap.decode == State::Verified) {
                c.hardwareDecode = true; c.decodeBackend = cap.decodeBackend;
                c.decodeLocator = gpu->locator; break;
            }
        }
        if (decodeMode == "gpu" && !c.hardwareDecode) {
            c.ok = false; c.error = "No successfully tested hardware decoder for the source codec (" + srcCodec + ").";
        }
    }
    return c;
}
QStringList inputArgs(const Choice &c) {
    if (!c.hardwareDecode) return {};
    QStringList a;
    if (c.decodeBackend == "vaapi") a << "-init_hw_device" << "vaapi=harbdec:" + c.decodeLocator;
    a << "-hwaccel" << c.decodeBackend;
    if (c.decodeBackend == "vaapi") a << "-hwaccel_device" << "harbdec";
    else if (c.decodeBackend == "cuda" && !c.decodeLocator.isEmpty()) a << "-hwaccel_device" << c.decodeLocator;
    // Do not pin hardware frames here: FFmpeg can transparently download them
    // for software resize/filter/audio combinations.
    return a;
}
QStringList outputArgs(const Choice &c) {
    if (!c.hardwareEncode) return {};
    if (c.encoderBackend == "nvenc" && !c.locator.isEmpty()) return {"-gpu", c.locator};
    return {};
}
}
HardwareScanner::HardwareScanner(QString executable, QObject *parent) : QThread(parent), executable_(std::move(executable)) {
    qRegisterMetaType<Hardware::Report>("Hardware::Report");
}
void HardwareScanner::cancel() { requestInterruption(); }
void HardwareScanner::run() {
    const auto report = Hardware::scan(executable_);
    if (!isInterruptionRequested()) Q_EMIT reportReady(report);
}
