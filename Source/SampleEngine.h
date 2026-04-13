#pragma once
#include <vector>
#include <memory>
#include <atomic>
#include <cmath>
#include <algorithm>

#ifdef FLUTUBE_HAS_RUBBERBAND
namespace RubberBand { class RubberBandStretcher; }
#endif

// Sample player. Supports pitch shifting via RubberBand when available,
// otherwise falls back to linear-resampling (changes pitch AND speed, classic
// sampler style). Enable RubberBand: brew install rubberband, then re-run cmake.
class SampleEngine
{
public:
    SampleEngine();
    ~SampleEngine();

    void prepare(double sampleRate, int maxBlockSize);
    void releaseResources();

    // Called from audio thread after a buffer swap.
    // Non-interleaved layout: [L: 0..N-1] [R: N..2N-1] (if stereo)
    void setBuffer(const float* data, int numFrames, int channels, double bufferSr);

    // Region endpoints (0..1). Written from message thread; read on audio thread.
    std::atomic<float> startPoint { 0.0f };
    std::atomic<float> endPoint   { 1.0f };

    void noteOn (int midiNote, float velocity);
    void noteOff(int midiNote);
    void stopPreview() { if (activeNote_ == -1) playing_ = false; }  // cancel waveform preview
    void setRootNote(int midiNote); // default 60 (C4)
    void setHoldMode(bool hold)    { holdMode_   = hold; }
    void setTempoRatio(float r)    { tempoRatio_ = r;    }

    // Fills outL and outR with numSamples. Returns true while playing.
    bool render(float* outL, float* outR, int numSamples);

    bool  isLoaded()        const noexcept { return bufferData_ != nullptr && numFrames_ > 0; }
    bool  isPlaying()       const noexcept { return playing_; }
    int   numFrames()       const noexcept { return numFrames_; }
    // Returns current playback position as 0..1, or -1 if not playing.
    float getPlayheadNorm() const noexcept;

    // Request a preview from the audio thread (safe to call from message thread).
    void requestPreview(float normPos);

private:
    int  computeStartSample() const;
    int  computeEndSample()   const;

#ifdef FLUTUBE_HAS_RUBBERBAND
    void resetStretcher(double pitchRatio, double timeRatio = 1.0);
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;
    std::vector<float> feedL_, feedR_;
    const float* feedPtrs_[2] = { nullptr, nullptr };
#else
    // Fallback: linear-resampling state
    double readPosF_    = 0.0; // fractional read position
    double resampleRate_ = 1.0; // samples to advance per output sample
#endif

    const float* bufferData_ = nullptr;
    int    numFrames_  = 0;
    int    channels_   = 0;
    double bufferSr_   = 44100.0;

    double sr_         = 44100.0;
    int    blockSize_  = 512;

    bool   playing_    = false;
    int    readPos_    = 0;
    float  velocity_   = 1.0f;
    int    rootNote_   = 60;
    int    activeNote_ = -1;
    bool   holdMode_   = false;
    float  tempoRatio_ = 1.0f;  // projectBPM / sampleBPM — applied at noteOn

    // Preview request from message thread: frame index to start from, -1 = none.
    std::atomic<int> pendingPreviewFrame_ { -1 };

#ifdef FLUTUBE_HAS_RUBBERBAND
    // Pre-allocated buffers for RubberBand priming (silence feed + drain sink).
    std::vector<float> primerBufL_, primerBufR_;
    // Fractional read position used for RubberBand feed (advances at bufferSr_/sr_ per output
    // sample so that the interpolated feed already accounts for any sample-rate mismatch,
    // keeping RubberBand's timeRatio purely for tempo control).
    double readPosRbF_    = 0.0;
    double lastTimeRatio_ = 1.0;
#endif
};
