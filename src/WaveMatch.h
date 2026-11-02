#pragma once
#include <vector>

// Small, standalone correlation module. No Qt dependency, so its matching
// algorithm can be tested independently from the FFmpeg and GUI integration.
namespace WaveMatch {
constexpr int kSampleRate = 8000;
constexpr int kFrameSamples = 160; // 20 ms / 50 features per second
constexpr int kFeaturesPerSecond = kSampleRate / kFrameSamples;
struct Result {
    bool found = false;
    double offsetSeconds = 0.0;   // desired timestamp delay on candidate relative to reference
    double score = 0.0;           // normalized correlation [-1,1]
    double distinctiveness = 0.0; // best score minus distant runner-up
    int comparedFrames = 0;
};
// Each sample vector is 8-kHz mono PCM, extracted from the starts indicated.
// Results compare the actual audio, not the files' reported durations.
std::vector<double> loudnessFeatures(const std::vector<float>& samples);
Result correlate(const std::vector<double>& reference, double referenceStartSeconds,
                 const std::vector<double>& candidate, double candidateStartSeconds);
}
