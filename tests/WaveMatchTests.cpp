#include "../src/WaveMatch.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void check(bool ok,const char* why) {if (!ok) {std::fprintf(stderr,"FAILED: %s\n",why);std::exit(1);}}
int main() {
    constexpr int hz=WaveMatch::kFeaturesPerSecond;
    std::vector<double> base(6000);
    // Deterministic dynamic envelope with identifiable transients.
    for (size_t i=0;i<base.size();++i) {
        double x=0.03*std::sin(i*0.083) + 0.02*std::cos(i*0.37);
        if ((i%173)<9) x += (i/173%2 ? 0.38 : -0.48);
        if ((i%397)<16) x += 0.43;
        if ((i%107)<14) x -= 0.13;
        base[i]=x;
    }
    const auto snippet=std::vector<double>(base.begin()+1500,base.begin()+2300);
    auto testShift=[&](int shiftFrames) {
        // Candidate has the SAME content shifted later by shiftFrames.
        std::vector<double> candidate(3000, 0.01);
        for (int i=0;i<3000;++i) {
            int src=1100+i-shiftFrames;
            if (src>=0 && src<static_cast<int>(base.size()))
                candidate[i]=base[src]*0.7 + 0.002*std::sin(i*0.17);
        }
        const auto match=WaveMatch::correlate(snippet,30.0,candidate,22.0);
        check(match.found,"detect valid waveform");
        const double expected = 30.0-(22.0+(1500-1100+shiftFrames)/double(hz));
        check(std::abs(match.offsetSeconds-expected)<0.021,"correct lag sign and value");
        check(match.score>0.95,"strong match despite gain and noise");
    };
    testShift(85);testShift(-85);testShift(0);
    std::vector<double> quiet(800,0.0);
    check(!WaveMatch::correlate(quiet,10.0,base,0.0).found,"reject silent sample");
    check(!WaveMatch::correlate({},10.0,base,0.0).found,"reject missing sample");
    std::puts("WaveMatch tests passed");
    return 0;
}
