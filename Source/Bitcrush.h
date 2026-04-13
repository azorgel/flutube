#pragma once
#include <cmath>
#include <algorithm>

// Bitcrusher: sample-and-hold rate reduction + N-bit quantisation.
// Both controls are 0..1 where 0 = no effect, 1 = maximum effect.
class Bitcrush
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples);

    void setBits         (float v); // 0=16-bit (clean), 1=1-bit (max crush)
    void setSampleRateRatio(float v); // 0=full SR (clean), 1=1kHz (max crush)

private:
    double sr_           = 44100.0;
    float  bits_         = 0.0f;   // 0 = no crush
    float  srRatio_      = 0.0f;   // 0 = no reduction

    float  holdL_        = 0.0f;
    float  holdR_        = 0.0f;
    float  counter_      = 0.0f;
};
