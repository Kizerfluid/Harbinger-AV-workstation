#pragma once
#include "Types.h"

// Smart mode produces one plan per input. Pure QtCore calculations: no GUI,
// filesystem writes, FFmpeg process launching, or implicit stream removal.
namespace SmartEncoding {
struct Plan {
    bool ok = false;
    EncodeOptions options;
    QString error;
    QString explanation;
    double estimatedSizeMB = 0;  // estimate only, never presented as a guarantee
};
Plan makePlan(const EncodeOptions &request, const MediaInfo &source);
}
