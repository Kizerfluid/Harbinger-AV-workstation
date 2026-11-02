#include "WaveMatch.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace WaveMatch {
std::vector<double> loudnessFeatures(const std::vector<float> &samples) {
    std::vector<double> out;
    out.reserve(samples.size() / kFrameSamples);
    // A log-energy envelope is insensitive to polarity changes and much more
    // robust to alternative languages / mixing than raw sample correlation.
    for (size_t i = 0; i + kFrameSamples <= samples.size(); i += kFrameSamples) {
        double energy = 0.0;
        for (size_t j = i; j < i + kFrameSamples; ++j) {
            const double v = std::isfinite(samples[j]) ? samples[j] : 0.0;
            energy += v*v;
        }
        out.push_back(std::log1p(80.0 * std::sqrt(energy / kFrameSamples)));
    }
    // Remove slowly varying loudness, retaining events such as onsets and cuts.
    if (out.size() < 3) return {};
    std::vector<double> detail(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const int low = std::max(0, static_cast<int>(i) - 25);
        const int high = std::min(static_cast<int>(out.size()), static_cast<int>(i) + 26);
        double mean = 0.0;
        for (int j = low; j < high; ++j) mean += out[j];
        detail[i] = out[i] - mean / (high-low);
    }
    return detail;
}
Result correlate(const std::vector<double>& reference, double referenceStartSeconds,
                 const std::vector<double>& candidate, double candidateStartSeconds) {
    Result result;
    const int n = static_cast<int>(reference.size());
    const int m = static_cast<int>(candidate.size());
    if (n < 250 || m < n || !std::isfinite(referenceStartSeconds) || !std::isfinite(candidateStartSeconds))
        return result;
    double refSum = 0, refSq = 0;
    for (const double v : reference) { refSum += v; refSq += v*v; }
    const double refVar = refSq - refSum*refSum/n;
    if (refVar < 0.002) return result; // silence / uninformative sample
    std::vector<double> prefix(m+1), squares(m+1);
    for (int i=0;i<m;++i) {
        prefix[i+1]=prefix[i]+candidate[i];
        squares[i+1]=squares[i]+candidate[i]*candidate[i];
    }
    double best = -2;
    int bestAt = -1;
    std::vector<double> scores(m-n+1, -2);
    for (int k=0;k<=m-n;++k) {
        const double sum = prefix[k+n]-prefix[k];
        const double var = squares[k+n]-squares[k]-sum*sum/n;
        if (var <= 0.002) continue;
        double dot=0;
        for (int j=0;j<n;++j) dot+= reference[j]*candidate[k+j];
        const double corr = (dot - refSum*sum/n)/std::sqrt(refVar*var);
        if (!std::isfinite(corr)) continue;
        scores[k]=corr;
        if (corr>best) {best=corr;bestAt=k;}
    }
    if (bestAt<0) return result;
    double runnerUp=-1;
    // Nearby scores are the same peak; exclude +/- 0.7 s for ambiguity check.
    for (int k=0;k<static_cast<int>(scores.size());++k)
        if (std::abs(k-bestAt)>35 && scores[k]>runnerUp) runnerUp=scores[k];
    result.found = true;
    result.score = std::clamp(best,-1.0,1.0);
    result.distinctiveness = best-std::max(0.0,runnerUp);
    result.offsetSeconds = referenceStartSeconds - (candidateStartSeconds + double(bestAt)/kFeaturesPerSecond);
    result.comparedFrames=n;
    return result;
}
}
