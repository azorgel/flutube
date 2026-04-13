#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// Tape-style feedback delay with LFO-modulated delay time and a
// low-pass filter in the feedback path for warmth/roll-off.
class TapeDelay
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples);

    void setMix     (float v);  // 0..1 wet/dry
    void setTime    (float v);  // 0..1 → ~50ms..800ms
    void setFeedback(float v);  // 0..0.9
    void setWobble  (float v);  // 0..1 LFO flutter depth

private:
    float readInterp(const std::vector<float>& buf, double readPos) const;

    static constexpr int MAX_SAMPLES = 192000; // 4s at 48kHz

    double sr_          = 44100.0;
    std::vector<float>  bufL_, bufR_;
    int   writePos_     = 0;

    // One-pole LP in feedback path (tape roll-off)
    float fbStateL_ = 0.0f;
    float fbStateR_ = 0.0f;

    // LFO for wow & flutter
    float lfoPhase_ = 0.0f;
    float lfoRate_  = 0.4f; // Hz

    float mix_      = 0.0f;
    float time_     = 0.35f;
    float feedback_ = 0.4f;
    float wobble_   = 0.0f;
};
