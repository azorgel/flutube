#include "LoFiChain.h"

void LoFiChain::prepare(double sr, int blockSize)
{
    bitcrush_.prepare(sr, blockSize);
    hum_.prepare(sr, blockSize);
    vinylNoise_.prepare(sr, blockSize);
    hazyFx_.prepare(sr, blockSize);
    wavyFx_.prepare(sr, blockSize);
    tapeDelay_.prepare(sr, blockSize);
}

void LoFiChain::processBlock(float* L, float* R, int numSamples)
{
    bitcrush_.processBlock(L, R, numSamples);
    hum_.processBlock(L, R, numSamples);
    vinylNoise_.processBlock(L, R, numSamples);
    hazyFx_.processBlock(L, R, numSamples);
    wavyFx_.processBlock(L, R, numSamples);
    tapeDelay_.processBlock(L, R, numSamples);
}

void LoFiChain::setTapeDelayMix      (float v) { tapeDelay_.setMix(v); }
void LoFiChain::setTapeDelayTime     (float v) { tapeDelay_.setTime(v); }
void LoFiChain::setTapeDelayFeedback (float v) { tapeDelay_.setFeedback(v); }
void LoFiChain::setTapeDelayWobble   (float v) { tapeDelay_.setWobble(v); }
void LoFiChain::setVinylNoise        (float v) { vinylNoise_.setNoiseLevel(v); }
void LoFiChain::setVinylCrackle      (float v) { vinylNoise_.setCrackleRate(v); }
void LoFiChain::setHumLevel          (float v) { hum_.setLevel(v); }
void LoFiChain::setBitcrushBits      (float v) { bitcrush_.setBits(v); }
void LoFiChain::setBitcrushSampleRate(float v) { bitcrush_.setSampleRateRatio(v); }
