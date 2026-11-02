#pragma once

#include "Types.h"
#include <QJsonObject>

// Presentation and metadata extraction for source audio streams. This does not
// change any FFmpeg transcoding, passthrough or MKV stream-mapping behaviour.
namespace AudioInfo {
    void readMetadata(MediaStream &track, const QJsonObject &stream);
    QString codecName(const MediaStream &track);
    QString bitrate(const MediaStream &track);
    QString bitDepth(const MediaStream &track);
    QString channels(const MediaStream &track);
    QString toolTip(const MediaStream &track);
}
