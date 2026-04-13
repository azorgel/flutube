#pragma once
#include <vector>
#include <cmath>

// Hazy — soft low-pass + tape saturation combo.
// As amount increases: high frequencies roll off and soft-clip harmonics are added,
// giving a fog-like, degraded warmth.
class HazyEffect
{
public:
    void prepare(double sr, int /*blockSize*/) { sr_ = sr; lpL_ = lpR_ = 0.0f; }
    void processBlock(float* L, float* R, int numSamples);
    void setAmount(float v) { amount_ = v; }

private:
    double sr_      = 44100.0;
    float  amount_  = 0.0f;
    float  lpL_     = 0.0f;  // one-pole LP state, left
    float  lpR_     = 0.0f;  // one-pole LP state, right
};
