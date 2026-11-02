#pragma once

#include "Types.h"
#include <QJsonObject>

// Presentation and conservative metadata extraction for source video streams.
// Only describes input; does not alter transcode, muxing or Hive behaviour.
namespace VideoInfo {
    void readMetadata(MediaStream &track, const QJsonObject &stream);
    QString codecName(const MediaStream &track);
    QString bitrate(const MediaStream &track);
    QString bitDepth(const MediaStream &track);
    QString resolution(const MediaStream &track);
    QString toolTip(const MediaStream &track);
}
