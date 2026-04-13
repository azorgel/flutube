#include "Hum.h"
#include <algorithm>

constexpr double Hum::FREQS[Hum::NUM_H];
constexpr float  Hum::AMPS [Hum::NUM_H];

void Hum::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sr_ = sampleRate;
    for (auto& p : phases_) p = 0.0;
}

void Hum::processBlock(float* L, float* R, int numSamples)
{
    if (level_ <= 0.001f) return;

    for (int i = 0; i < numSamples; ++i)
    {
        float hum = 0.0f;
        for (int h = 0; h < NUM_H; ++h)
        {
            hum      += std::sin(phases_[h]) * AMPS[h];
            phases_[h] += 2.0 * 3.14159265358979323846 * FREQS[h] / sr_;
            if (phases_[h] > 6.28318530717958647693) phases_[h] -= 6.28318530717958647693;
        }
        hum *= level_ * 0.04f; // keep hum subtle
        L[i] += hum;
        R[i] += hum;
    }
}

void Hum::setLevel(float v) { level_ = std::max(0.0f, std::min(1.0f, v)); }
