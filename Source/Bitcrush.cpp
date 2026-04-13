#include "Bitcrush.h"

void Bitcrush::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sr_      = sampleRate;
    holdL_   = holdR_ = 0.0f;
    counter_ = 0.0f;
}

void Bitcrush::processBlock(float* L, float* R, int numSamples)
{
    // Both controls at zero → fully bypassed
    if (bits_ <= 0.001f && srRatio_ <= 0.001f) return;

    // bits_:    0 → 16-bit (levels=32768), 1 → 1-bit (levels=1)
    float actualBits = 16.0f - bits_ * 15.0f;
    float levels     = std::pow(2.0f, actualBits - 1.0f);

    // srRatio_: 0 → full sample rate (holdPeriod=1), 1 → 1kHz target
    float targetSr   = (float)sr_ - srRatio_ * ((float)sr_ - 1000.0f);
    float holdPeriod = (float)sr_ / targetSr;

    for (int i = 0; i < numSamples; ++i)
    {
        counter_ += 1.0f;
        if (counter_ >= holdPeriod)
        {
            counter_ -= holdPeriod;
            holdL_ = std::round(L[i] * levels) / levels;
            holdR_ = std::round(R[i] * levels) / levels;
        }
        L[i] = holdL_;
        R[i] = holdR_;
    }
}

void Bitcrush::setBits         (float v) { bits_    = std::max(0.0f, std::min(1.0f, v)); }
void Bitcrush::setSampleRateRatio(float v) { srRatio_ = std::max(0.0f, std::min(1.0f, v)); }
