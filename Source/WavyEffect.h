#pragma once
#include <vector>
#include <cmath>

// Wavy — stereo chorus / vibrato.
// Two independent LFOs (slightly different rates) modulate short delay lines,
// producing a lush pitch-and-time modulation that widens and animates the sound.
class WavyEffect
{
public:
    void prepare(double sr, int blockSize);
    void processBlock(float* L, float* R, int numSamples);
    void setAmount(float v) { amount_ = v; }

private:
    float readInterp(const std::vector<float>& buf, int writePos, double delaySamples) const;

    static constexpr int    MAX_BUF      = 8192;   // ~186 ms at 44.1 kHz
    static constexpr float  BASE_MS      = 10.0f;  // base delay (ms)
    static constexpr float  DEPTH_MS     = 7.5f;   // LFO depth (±ms)
    static constexpr float  LFO_RATE_L   = 0.63f;  // Hz — left channel
    static constexpr float  LFO_RATE_R   = 0.79f;  // Hz — right (slight detune)

    double sr_         = 44100.0;
    float  amount_     = 0.0f;

    std::vector<float> bufL_, bufR_;
    int    writePos_   = 0;

    float  lfoPhL_     = 0.0f;             // LFO phase, left  (radians)
    float  lfoPhR_     = 3.14159265f * 0.5f; // LFO phase, right (90° offset)
};
