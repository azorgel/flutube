#pragma once
#include "TapeDelay.h"
#include "VinylNoise.h"
#include "Hum.h"
#include "Bitcrush.h"
#include "HazyEffect.h"
#include "WavyEffect.h"

// Lo-fi effects chain: Bitcrush → Hum → Vinyl → Hazy → Wavy → Tape Delay
class LoFiChain
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples);

    void setTapeDelayMix       (float v);
    void setTapeDelayTime      (float v);
    void setTapeDelayFeedback  (float v);
    void setTapeDelayWobble    (float v);
    void setVinylNoise         (float v);
    void setVinylCrackle       (float v);
    void setHumLevel           (float v);
    void setBitcrushBits       (float v);
    void setBitcrushSampleRate (float v);
    void setHazyAmount         (float v) { hazyFx_.setAmount(v); }
    void setWavyAmount         (float v) { wavyFx_.setAmount(v); }

private:
    TapeDelay   tapeDelay_;
    VinylNoise  vinylNoise_;
    Hum         hum_;
    Bitcrush    bitcrush_;
    HazyEffect  hazyFx_;
    WavyEffect  wavyFx_;
};
