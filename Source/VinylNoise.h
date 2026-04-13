#pragma once
#include <random>
#include <cmath>

// Vinyl noise: pink-ish hiss + randomised crackle impulses.
class VinylNoise
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples);

    void setNoiseLevel (float v);  // 0..1
    void setCrackleRate(float v);  // 0..1 (density)

private:
    double sr_           = 44100.0;
    float  noiseLevel_   = 0.0f;
    float  crackleRate_  = 0.0f;

    // 3-filter approximation of pink noise
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;

    // Crackle state
    float crackleCounter_ = 0.0f;
    float crackleEnv_     = 0.0f;
    float crackleDecay_   = 0.9f;

    std::mt19937                         rng_;
    std::uniform_real_distribution<float> dist_{ -1.0f, 1.0f };
};
