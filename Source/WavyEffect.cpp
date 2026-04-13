#include "WavyEffect.h"
#include <algorithm>

void WavyEffect::prepare(double sr, int /*blockSize*/)
{
    sr_      = sr;
    bufL_.assign(MAX_BUF, 0.0f);
    bufR_.assign(MAX_BUF, 0.0f);
    writePos_ = 0;
    lfoPhL_   = 0.0f;
    lfoPhR_   = 3.14159265f * 0.5f;
}

float WavyEffect::readInterp(const std::vector<float>& buf, int writePos, double delaySamples) const
{
    int sz     = (int)buf.size();
    double rp  = (double)writePos - delaySamples;
    // Wrap read position into buffer
    while (rp < 0.0)   rp += sz;
    while (rp >= sz)   rp -= sz;

    int   i0   = (int)rp % sz;
    int   i1   = (i0 + 1) % sz;
    float frac = (float)(rp - (int)rp);
    return buf[i0] * (1.0f - frac) + buf[i1] * frac;
}

void WavyEffect::processBlock(float* L, float* R, int numSamples)
{
    if (amount_ < 0.001f) return;

    double baseSamp  = BASE_MS  * 0.001 * sr_;
    double depthSamp = DEPTH_MS * 0.001 * sr_ * (double)amount_;
    float  wetMix    = amount_ * 0.65f;   // max 65% wet

    const float twoPi   = 2.0f * 3.14159265f;
    const float incL    = twoPi * LFO_RATE_L / (float)sr_;
    const float incR    = twoPi * LFO_RATE_R / (float)sr_;
    const int   bufSz   = (int)bufL_.size();

    for (int i = 0; i < numSamples; ++i)
    {
        float sinL = std::sin(lfoPhL_);
        float sinR = std::sin(lfoPhR_);

        double delayL = baseSamp + (double)sinL * depthSamp;
        double delayR = baseSamp + (double)sinR * depthSamp;
        delayL = std::max(1.0, delayL);
        delayR = std::max(1.0, delayR);

        float wetL = readInterp(bufL_, writePos_, delayL);
        float wetR = readInterp(bufR_, writePos_, delayR);

        // Write dry signal into delay buffers
        bufL_[writePos_ % bufSz] = L[i];
        bufR_[writePos_ % bufSz] = R[i];
        writePos_ = (writePos_ + 1) % bufSz;

        L[i] = L[i] * (1.0f - wetMix) + wetL * wetMix;
        R[i] = R[i] * (1.0f - wetMix) + wetR * wetMix;

        lfoPhL_ += incL;
        if (lfoPhL_ >= twoPi) lfoPhL_ -= twoPi;
        lfoPhR_ += incR;
        if (lfoPhR_ >= twoPi) lfoPhR_ -= twoPi;
    }
}
