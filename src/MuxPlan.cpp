#include "MuxPlan.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <cmath>

namespace {
QString canonical(const QString &p) {
    const QFileInfo fi(p);
    // Canonical when possible (handles aliases/symlinks), absolute otherwise.
    const QString found = fi.canonicalFilePath();
    return QDir::cleanPath(found.isEmpty() ? fi.absoluteFilePath() : found);
}
QString key(const QString &source, int streamIndex) {
    return canonical(source) + QChar(0x1F) + QString::number(streamIndex);
}
}

namespace Mux {
QString validate(const MuxPlan &plan) {
    if (plan.tracks.isEmpty()) return "Select at least one video, audio or subtitle track.";
    if (plan.output.trimmed().isEmpty() || QFileInfo(plan.output).suffix().compare("mkv", Qt::CaseInsensitive) != 0)
        return "Choose an output filename ending in .mkv.";
    const QFileInfo out(plan.output);
    if (!QDir(out.absolutePath()).exists()) return "The output folder does not exist.";
    if (!QFileInfo(out.absolutePath()).isWritable()) return "The output folder is not writable.";
    if (out.exists()) return "The output file already exists. Choose a different filename; files are never overwritten.";
    QSet<QString> used;
    const QString outputPath = canonical(plan.output);
    int videos = 0;
    QHash<QString, int> defaultTypes;
    for (const auto &t : plan.tracks) {
        if (!QStringList{"video", "audio", "subtitle"}.contains(t.type)) return "Invalid track type.";
        if (t.streamIndex < 0 || !QFileInfo(t.source).isFile()) return "A selected source file is missing.";
        if (canonical(t.source) == outputPath) return "Output may not replace an input file.";
        if (used.contains(key(t.source, t.streamIndex))) return "A track was selected twice.";
        used.insert(key(t.source, t.streamIndex));
        if (t.type == "video" && ++videos > 1) return "Choose only one video track for this MKV.";
        if (t.isDefault && ++defaultTypes[t.type] > 1) return "Only one default track is allowed per media type.";
        if (!QRegularExpression("^[A-Za-z]{2,3}$").match(t.language).hasMatch())
            return "Use a 2- or 3-letter language code, such as eng, jpn or und.";
        if (t.codec.isEmpty()) return "A track is missing its detected codec.";
        if (t.delayMs != 0 && t.type != "audio") return "Only audio tracks support timestamp delays.";
        if (t.delayMs < -43200000 || t.delayMs > 43200000)
            return "An audio track delay is outside the supported range of ±12 hours.";
    }
    if (!plan.chapterSource.isEmpty() && !QFileInfo(plan.chapterSource).isFile())
        return "The selected chapter source no longer exists.";
    return {};
}

QStringList arguments(const MuxPlan &plan, const QString &temporaryOutput) {
    // Call validate() first. Paths and titles are individual QProcess arguments,
    // never interpolated into a shell command.
    QStringList args {"-hide_banner", "-nostdin", "-n", "-loglevel", "warning", "-stats_period", "0.25", "-progress", "pipe:1"};
    // -itsoffset is an INPUT option. A separate input is necessary for every
    // (file, audio delay) combination so tracks from the same file can have
    // independent delays without any re-encoding.
    struct SourceInput { QString path; qint64 delayMs; };
    QVector<SourceInput> sources;
    auto includeSource = [&](const QString &path, qint64 delayMs) -> int {
        for (int i = 0; i < sources.size(); ++i)
            if (sources.at(i).path == path && sources.at(i).delayMs == delayMs) return i;
        sources.append({path, delayMs});
        return static_cast<int>(sources.size()) - 1;
    };
    QVector<int> streamInputs;
    for (const auto &t : plan.tracks)
        streamInputs.append(includeSource(t.source, t.type == "audio" ? t.delayMs : 0));
    const int chapterInput = plan.chapterSource.isEmpty() ? -1 : includeSource(plan.chapterSource, 0);
    for (const auto &s : sources) {
        if (s.delayMs != 0)
            args << "-itsoffset" << QString::number(static_cast<double>(s.delayMs) / 1000.0, 'f', 3);
        args << "-i" << s.path;
    }
    for (int i = 0; i < plan.tracks.size(); ++i)
        args << "-map" << QString("%1:%2").arg(streamInputs.at(i)).arg(plan.tracks.at(i).streamIndex);
    args << "-c" << "copy";
    for (int i = 0; i < plan.tracks.size(); ++i) {
        const MuxTrack &t = plan.tracks.at(i);
        // mov_text (MP4 timed text) can't be copied as-is to Matroska.
        // Only this subtitle stream is converted; video and audio are stream-copied.
        if (t.type == "subtitle" && t.codec == "mov_text") args << QString("-c:%1").arg(i) << "srt";
        args << QString("-metadata:s:%1").arg(i) << "language=" + t.language.toLower();
        args << QString("-metadata:s:%1").arg(i) << "title=" + t.title;
        QString disposition;
        if (t.isDefault) disposition = "default";
        if (t.isForced) disposition += disposition.isEmpty() ? "forced" : "+forced";
        args << QString("-disposition:%1").arg(i) << (disposition.isEmpty() ? QString("0") : disposition);
    }
    args << "-map_chapters" << QString::number(chapterInput);
    if (plan.preserveGlobalMetadata) {
        int primary = 0;
        for (int i = 0; i < plan.tracks.size(); ++i)
            if (plan.tracks.at(i).type == "video") { primary = streamInputs.at(i); break; }
        args << "-map_metadata" << QString::number(primary);
    } else args << "-map_metadata" << "-1";
    args << "-f" << "matroska" << temporaryOutput;
    return args;
}
QString suggestedOutput(const QString &source) {
    const QFileInfo info(source);
    const QString base = info.completeBaseName() + "_muxed";
    const QString folder = info.absolutePath();
    for (int i = 0; i < 10000; ++i) {
        const QString suffix = i == 0 ? "" : "_" + QString::number(i);
        const QString path = QDir(folder).filePath(base + suffix + ".mkv");
        const QString temp = QDir(folder).filePath(base + suffix + ".harbinger-part.mkv");
        if (!QFileInfo::exists(path) && !QFileInfo::exists(temp)) return path;
    }
    return QDir(folder).filePath(base + "_new.mkv");
}
QString descriptiveTrack(const MuxTrack &t) {
    QString s = t.codec.toUpper();
    if (!t.language.isEmpty() && t.language != "und") s += " · " + t.language.toLower();
    if (!t.title.isEmpty()) s += " · " + t.title;
    return s;
}
} // namespace Mux
