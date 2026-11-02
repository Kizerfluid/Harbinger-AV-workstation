#pragma once
#include "Types.h"

namespace Codec {
QStringList containers();
QString extension(const QString &container);
bool isAudioOnly(const QString &container);
bool supports10Bit(const QString &videoCodec); // Conservative supported video families.
QList<CodecEntry> videoOptions(const QString &container, const QSet<QString> &available);
QList<CodecEntry> audioOptions(const QString &container, const QSet<QString> &available);
QSet<QString> parseEncoderList(const QString &output);
// Audio encoder capabilities and reusable FFmpeg transcoding arguments.
int audioMaxChannels(const QString &encoder);
QList<int> audioBitrates(const QString &encoder);
// Describes the valid GUI bitrate control for a codec. Some encoders accept
// arbitrary whole-kbps targets, while others need standard fixed rates.
struct AudioBitrateSpec {
    int minimum = 0;
    int maximum = 0;
    QList<int> discrete; // Empty means every integer value in [minimum, maximum].
    bool enabled() const { return minimum > 0 && maximum >= minimum; }
};
AudioBitrateSpec audioBitrateSpec(const QString &encoder);
int normaliseAudioBitrate(const QString &encoder, int requestedKbps);
QStringList audioEncodeArgs(const EncodeOptions &settings, const MediaInfo &input);
QStringList buildArgs(const EncodeOptions &settings, const MediaInfo &input, const QString &output,
                      const QStringList &preInput = {}, const QStringList &extraEncoder = {}, bool vaapiUpload = false);
QString humanDuration(double seconds);
QString defaultPreset(const QString &container);
} // namespace Codec
