#include "HazyEffect.h"
#include <algorithm>

void HazyEffect::processBlock(float* L, float* R, int numSamples)
{
    if (amount_ < 0.001f) return;

    // LP cutoff: 18 kHz at 0, 600 Hz at 1  (exponential sweep)
    float fc    = 18000.0f * std::pow(600.0f / 18000.0f, amount_);
    float alpha = 1.0f - std::exp(-2.0f * 3.14159265f * fc / (float)sr_);

    // Soft-saturation drive: 1× at 0, 4× at 1 (tanh clips harmonics)
    float drive    = 1.0f + 3.0f * amount_ * amount_;
    float invDrive = 1.0f / drive;

    for (int i = 0; i < numSamples; ++i)
    {
        // One-pole low-pass
        lpL_ += alpha * (L[i] - lpL_);
        lpR_ += alpha * (R[i] - lpR_);

        // Blend dry → LP (amount_ controls how much LP mixes in)
        float l = L[i] + amount_ * (lpL_ - L[i]);
        float r = R[i] + amount_ * (lpR_ - R[i]);

        // Soft saturation via tanh — preserves level (divide by drive)
        l = std::tanh(l * drive) * invDrive;
        r = std::tanh(r * drive) * invDrive;

        L[i] = l;
        R[i] = r;
    }
}
