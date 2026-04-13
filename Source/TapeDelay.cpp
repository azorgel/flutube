#include "TapeDelay.h"
#include <cstring>

void TapeDelay::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sr_ = sampleRate;
    bufL_.assign(MAX_SAMPLES, 0.0f);
    bufR_.assign(MAX_SAMPLES, 0.0f);
    writePos_  = 0;
    fbStateL_  = fbStateR_ = 0.0f;
    lfoPhase_  = 0.0f;
}

float TapeDelay::readInterp(const std::vector<float>& buf, double pos) const
{
    int    i0   = (int)pos;
    float  frac = (float)(pos - i0);
    int    i0w  = ((i0 % MAX_SAMPLES) + MAX_SAMPLES) % MAX_SAMPLES;
    int    i1w  = (i0w + 1) % MAX_SAMPLES;
    return buf[i0w] * (1.0f - frac) + buf[i1w] * frac;
}

void TapeDelay::processBlock(float* L, float* R, int numSamples)
{
    if (mix_ <= 0.001f) return;

    // 0..1 → 50ms..800ms in samples
    double baseDelay  = (0.05 + time_ * 0.75) * sr_;
    double wobbleAmp  = wobble_ * 0.005 * sr_; // up to ±5ms
    float  lfoInc     = (float)(2.0 * 3.14159265 * lfoRate_ / sr_);

    // One-pole LP coefficient (−3dB at ~8 kHz → tape roll-off)
    float lpCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * 8000.0f / (float)sr_);

    for (int i = 0; i < numSamples; ++i)
    {
        float lfo = std::sin(lfoPhase_) * (float)wobbleAmp;
        lfoPhase_ += lfoInc;
        if (lfoPhase_ > 6.28318530f) lfoPhase_ -= 6.28318530f;

        double delay = std::max(1.0, std::min((double)(MAX_SAMPLES - 2), baseDelay + lfo));

        double rposL = (double)writePos_ - delay;
        if (rposL < 0.0) rposL += MAX_SAMPLES;

        float delL = readInterp(bufL_, rposL);
        float delR = readInterp(bufR_, rposL); // same offset for both channels

        // Feedback LP
        fbStateL_ += (delL * feedback_ - fbStateL_) * lpCoeff;
        fbStateR_ += (delR * feedback_ - fbStateR_) * lpCoeff;

        bufL_[writePos_] = L[i] + fbStateL_;
        bufR_[writePos_] = R[i] + fbStateR_;

        L[i] = L[i] * (1.0f - mix_) + delL * mix_;
        R[i] = R[i] * (1.0f - mix_) + delR * mix_;

        writePos_ = (writePos_ + 1) % MAX_SAMPLES;
    }
}

void TapeDelay::setMix     (float v) { mix_      = std::max(0.0f, std::min(1.0f, v)); }
void TapeDelay::setTime    (float v) { time_     = std::max(0.0f, std::min(1.0f, v)); }
void TapeDelay::setFeedback(float v) { feedback_ = std::max(0.0f, std::min(0.9f, v)); }
void TapeDelay::setWobble  (float v) { wobble_   = std::max(0.0f, std::min(1.0f, v)); }
