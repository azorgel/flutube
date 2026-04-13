#pragma once
#include <cmath>

// Mains hum: 60Hz fundamental + harmonics (120, 180, 300 Hz).
class Hum
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples);
    void setLevel(float v); // 0..1

private:
    static constexpr int    NUM_H      = 4;
    static constexpr double FREQS[NUM_H] = { 60.0, 120.0, 180.0, 300.0 };
    static constexpr float  AMPS [NUM_H] = { 1.0f,  0.5f, 0.25f, 0.10f };

    double phases_[NUM_H] = {};
    float  level_  = 0.0f;
    double sr_     = 44100.0;
};
