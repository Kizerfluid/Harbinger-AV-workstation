#include "../src/WaveMatch.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr,"FAILED: %s\n",message);std::exit(1); }
}
static double soundEffect(double t) {
    // Shared, stereo-positioned music and several distinct effect patterns.
    const double beat=std::fmod(t,1.37);
    const double accent=beat<0.085?std::exp(-beat*50.0)*0.19:0.0;
    const double unique=std::sin(0.074*t*t)*0.055;
    return (0.07+unique)*std::sin(2.0*3.141592653589793*(170.0*t+0.08*t*t)) + accent;
}
static double voice(double t,int language) {
    // Loud, different dialogue, centered equally in L/R.
    const double envelope=0.3+0.7*std::abs(std::sin((language?4.3:3.7)*t));
    return 0.34*envelope*std::sin(2.0*3.141592653589793*(language?373.0:307.0)*t);
}
static void make(double shift, int language, std::vector<float> &mid, std::vector<float> &side) {
    constexpr int sampleRate=WaveMatch::kSampleRate;
    const int frames=sampleRate*75;
    mid.resize(frames);side.resize(frames);
    for(int i=0;i<frames;++i){
        const double time=double(i)/sampleRate-shift;
        if(time<0){mid[i]=0;side[i]=0;continue;}
        const double bg=soundEffect(time);
        const double center=voice(time,language);
        const double left=1.0*bg+center;
        const double right=0.65*bg+center;
        mid[i]=float(0.5*(left+right));
        side[i]=float(0.5*(left-right));
    }
}
int main(){
    constexpr int hz=WaveMatch::kFeaturesPerSecond;
    std::vector<float> rm,rs,cm,cs;
    make(0.0,0,rm,rs);
    make(1.6,1,cm,cs);
    const auto refSide=WaveMatch::loudnessFeatures(std::vector<float>(rs.begin()+12*WaveMatch::kSampleRate,rs.begin()+30*WaveMatch::kSampleRate));
    const auto tarSide=WaveMatch::loudnessFeatures(std::vector<float>(cs.begin()+4*WaveMatch::kSampleRate,cs.begin()+40*WaveMatch::kSampleRate));
    const auto match=WaveMatch::correlate(refSide,12.0,tarSide,4.0);
    require(match.found,"side-channel waveform matched");
    require(std::abs(match.offsetSeconds+1.6)<=1.0/hz,"dub match correctly finds 1600ms delay");
    require(match.score>0.95,"dub shared soundtrack correlates despite voices");
    require(match.distinctiveness>0.025,"dub match has a distinct peak");
    std::printf("Dub matching passed: lag %.3f s, score %.3f, distinctiveness %.3f\n",
        match.offsetSeconds,match.score,match.distinctiveness);
}
