#include "VinylNoise.h"
#include <algorithm>

void VinylNoise::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sr_           = sampleRate;
    b0_ = b1_ = b2_ = 0.0f;
    crackleCounter_ = 0.0f;
    crackleEnv_     = 0.0f;
    crackleDecay_   = std::exp(-1.0f / (float)(sr_ * 0.003)); // 3ms decay
    rng_.seed(0x5EED);
    dist_ = std::uniform_real_distribution<float>(-1.0f, 1.0f);
}

void VinylNoise::processBlock(float* L, float* R, int numSamples)
{
    if (noiseLevel_ <= 0.001f && crackleRate_ <= 0.001f) return;

    // Average samples between crackles: crackleRate 0→5s gap, 1→50ms gap
    float avgGap = (float)sr_ * (5.0f - crackleRate_ * 4.95f);

    for (int i = 0; i < numSamples; ++i)
    {
        // Paul Kellet's "economy" pink noise
        float w = dist_(rng_);
        b0_ = 0.99886f * b0_ + w * 0.0555179f;
        b1_ = 0.99332f * b1_ + w * 0.0750759f;
        b2_ = 0.96900f * b2_ + w * 0.1538520f;
        float pink = (b0_ + b1_ + b2_ + w * 0.5362f) * 0.11f;

        // Crackle event scheduling
        crackleCounter_ -= 1.0f;
        if (crackleCounter_ <= 0.0f)
        {
            float r = (dist_(rng_) + 1.0f) * 0.5f; // 0..1
            crackleCounter_ = avgGap * (0.2f + r * 1.6f);
            crackleEnv_     = 0.2f + r * 0.4f;
        }
        float crackle = crackleEnv_ * dist_(rng_);
        crackleEnv_ *= crackleDecay_;

        float noise = pink * noiseLevel_ * 0.25f
                    + crackle * crackleRate_ * 0.08f;

        L[i] += noise;
        R[i] += noise + dist_(rng_) * noiseLevel_ * 0.01f; // tiny stereo spread
    }
}

void VinylNoise::setNoiseLevel (float v) { noiseLevel_  = std::max(0.0f, std::min(1.0f, v)); }
void VinylNoise::setCrackleRate(float v) { crackleRate_ = std::max(0.0f, std::min(1.0f, v)); }
