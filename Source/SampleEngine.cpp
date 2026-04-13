#include "SampleEngine.h"
#include <JuceHeader.h>
#include <cstring>

#ifdef FLUTUBE_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
using namespace RubberBand;
#endif

SampleEngine::SampleEngine()  = default;
SampleEngine::~SampleEngine() = default;

void SampleEngine::prepare(double sampleRate, int maxBlockSize)
{
    sr_        = sampleRate;
    blockSize_ = maxBlockSize;

#ifdef FLUTUBE_HAS_RUBBERBAND
    feedL_.resize((size_t)maxBlockSize, 0.0f);
    feedR_.resize((size_t)maxBlockSize, 0.0f);
    feedPtrs_[0] = feedL_.data();
    feedPtrs_[1] = feedR_.data();
    primerBufL_.assign(8192, 0.0f);
    primerBufR_.assign(8192, 0.0f);
    resetStretcher(1.0);
#endif
}

void SampleEngine::releaseResources()
{
#ifdef FLUTUBE_HAS_RUBBERBAND
    stretcher_.reset();
#endif
}

#ifdef FLUTUBE_HAS_RUBBERBAND
void SampleEngine::resetStretcher(double pitchRatio, double timeRatio)
{
    stretcher_ = std::make_unique<RubberBandStretcher>(
        (size_t)sr_, 2,
        RubberBandStretcher::OptionProcessRealTime
        | RubberBandStretcher::OptionPitchHighQuality
        | RubberBandStretcher::OptionWindowShort,
        timeRatio,      // time stretch ratio (output/input duration)
        pitchRatio
    );
    stretcher_->setMaxProcessSize((size_t)blockSize_);
}
#endif

void SampleEngine::setBuffer(const float* data, int numFrames, int channels, double bufferSr)
{
    bufferData_ = data;
    numFrames_  = numFrames;
    channels_   = channels;
    bufferSr_   = bufferSr;
    playing_    = false;
    readPos_    = 0;
#ifdef FLUTUBE_HAS_RUBBERBAND
    if (stretcher_) stretcher_->reset();
#else
    readPosF_ = 0.0;
#endif
}

void SampleEngine::setRootNote(int midiNote)
{
    rootNote_ = midiNote;
}

void SampleEngine::noteOn(int midiNote, float velocity)
{
    if (!isLoaded()) return;

    velocity_ = velocity;
    readPos_  = computeStartSample();
    playing_  = true;
    activeNote_ = midiNote;

    double semitones  = (double)(midiNote - rootNote_);
    double pitchRatio = std::pow(2.0, semitones / 12.0);

#ifdef FLUTUBE_HAS_RUBBERBAND
    if (stretcher_)
    {
        // Reset without reallocating — avoids audio-thread heap alloc (the retrigger blip).
        // timeRatio is kept purely for tempo control; sample-rate conversion is handled
        // by advancing readPosRbF_ at bufferSr_/sr_ in the feed loop.
        double timeRatio = 1.0 / (double)tempoRatio_;
        readPosRbF_ = (double)computeStartSample();
        stretcher_->reset();
        stretcher_->setPitchScale(pitchRatio);
        stretcher_->setTimeRatio(timeRatio);
        lastTimeRatio_ = timeRatio;

        // Prime stretcher with silence to flush startup latency, then drain
        // the silence output so the queue is empty when the first render block runs.
        // Without the drain, retrieve() returns that silence first → blip at note start.
        int latency = (int)stretcher_->getLatency();
        int pad = (timeRatio > 0.0) ? (int)std::ceil((double)latency / timeRatio) : latency;
        pad = std::min(pad, (int)primerBufL_.size());
        if (pad > 0)
        {
            const float* silPtrs[2] = { primerBufL_.data(), primerBufR_.data() };
            stretcher_->process(silPtrs, (size_t)pad, false);

            // Drain the silence out of the output queue
            int toDrain = std::min((int)stretcher_->available(), (int)primerBufL_.size());
            if (toDrain > 0)
            {
                float* drainPtrs[2] = { primerBufL_.data(), primerBufR_.data() };
                stretcher_->retrieve(drainPtrs, (size_t)toDrain);
                // Re-zero so the buffers are clean for the next prime
                std::fill(primerBufL_.begin(), primerBufL_.end(), 0.0f);
                std::fill(primerBufR_.begin(), primerBufR_.end(), 0.0f);
            }
        }
    }
#else
    // Linear resampling: advance source at pitchRatio * tempoRatio * (bufferSr / sr) per output sample
    resampleRate_ = pitchRatio * (double)tempoRatio_ * (bufferSr_ / sr_);
    readPosF_     = (double)computeStartSample();
#endif
}

void SampleEngine::requestPreview(float normPos)
{
    if (!isLoaded()) return;
    int frame = juce::jlimit(0, numFrames_ - 1, (int)(normPos * (float)numFrames_));
    pendingPreviewFrame_.store(frame);
}

float SampleEngine::getPlayheadNorm() const noexcept
{
    if (!playing_ || numFrames_ <= 0) return -1.0f;
#ifdef FLUTUBE_HAS_RUBBERBAND
    return juce::jlimit(0.0f, 1.0f, (float)(readPosRbF_ / (double)numFrames_));
#else
    return juce::jlimit(0.0f, 1.0f, (float)(readPosF_ / (double)numFrames_));
#endif
}


void SampleEngine::noteOff(int midiNote)
{
    if (!holdMode_ && midiNote == activeNote_)
        playing_ = false;
}

int SampleEngine::computeStartSample() const
{
    return (int)(startPoint.load() * (float)numFrames_);
}

int SampleEngine::computeEndSample() const
{
    return std::min((int)(endPoint.load() * (float)numFrames_), numFrames_);
}

bool SampleEngine::render(float* outL, float* outR, int numSamples)
{
    // Apply any preview request posted from the message thread.
    // Uses exchange(-1) so this runs at most once per pending request.
    int previewFrame = pendingPreviewFrame_.exchange(-1);
    if (previewFrame >= 0 && bufferData_)
    {
        readPos_    = previewFrame;
        velocity_   = 0.85f;
        playing_    = true;
        activeNote_ = -1;
#ifdef FLUTUBE_HAS_RUBBERBAND
        if (stretcher_) stretcher_->reset();
        readPosRbF_ = (double)previewFrame;
#else
        readPosF_     = (double)previewFrame;
        resampleRate_ = bufferSr_ / sr_;
#endif
    }

    std::fill(outL, outL + numSamples, 0.0f);
    std::fill(outR, outR + numSamples, 0.0f);

    if (!playing_ || !bufferData_) return false;

    int endSample = computeEndSample();

#ifdef FLUTUBE_HAS_RUBBERBAND
    // ── RubberBand path ──────────────────────────────────────────────────────
    if (!stretcher_) return false;

    // NOTE: timeRatio is only updated at noteOn(), not mid-playback.
    // Applying setTimeRatio() every block (e.g. when sync is toggled) causes
    // discontinuities in RubberBand's internal buffers that sound like crackling.
    // The new ratio takes effect on the next note trigger — correct sampler behaviour.

    if (readPosRbF_ >= (double)endSample) { playing_ = false; return false; }

    if ((int)feedL_.size() < numSamples)
    {
        feedL_.resize((size_t)numSamples, 0.0f);
        feedR_.resize((size_t)numSamples, 0.0f);
        feedPtrs_[0] = feedL_.data();
        feedPtrs_[1] = feedR_.data();
    }

    const float* chL = bufferData_;
    const float* chR = (channels_ >= 2) ? bufferData_ + numFrames_ : bufferData_;

    // Interpolated feed: advance at bufferSr_/sr_ per output sample.
    // This is a one-pass linear SRC that corrects for mismatched file/engine sample rates
    // (e.g. 44100 Hz file in a 48000 Hz session) without touching RubberBand's timeRatio.
    // When bufferSr_ == sr_ the fractional step is exactly 1.0 and this is identical to
    // direct integer indexing with no rounding cost.
    double srcRate = bufferSr_ / sr_;
    double endF    = (double)endSample;

    for (int i = 0; i < numSamples; ++i)
    {
        double srcPos = readPosRbF_ + (double)i * srcRate;
        if (srcPos >= endF)
        {
            // Zero-pad from here to end of block (past end of source region)
            std::fill(feedL_.begin() + i, feedL_.begin() + numSamples, 0.0f);
            std::fill(feedR_.begin() + i, feedR_.begin() + numSamples, 0.0f);
            break;
        }
        int   p0   = (int)srcPos;
        int   p1   = std::min(p0 + 1, endSample - 1);
        float frac = (float)(srcPos - (double)p0);
        feedL_[i] = (chL[p0] * (1.0f - frac) + chL[p1] * frac) * velocity_;
        feedR_[i] = (chR[p0] * (1.0f - frac) + chR[p1] * frac) * velocity_;
    }

    // Never pass isFinal=true: keeps the stretcher in a non-finished state so
    // that reset() on the next noteOn always works cleanly without draining first.
    stretcher_->process(feedPtrs_, (size_t)numSamples, false);
    readPosRbF_ += (double)numSamples * srcRate;

    int avail = stretcher_->available();
    if (avail > 0)
    {
        int toGet = std::min(avail, numSamples);
        float* outs[2] = { outL, outR };
        stretcher_->retrieve(outs, (size_t)toGet);
    }

    if (readPosRbF_ >= endF)
        playing_ = false;

    return playing_ || avail > 0;


#else
    // ── Linear-resampling fallback ───────────────────────────────────────────
    const float* chL = bufferData_;
    const float* chR = (channels_ >= 2) ? bufferData_ + numFrames_ : bufferData_;
    double endF      = (double)endSample;

    for (int i = 0; i < numSamples; ++i)
    {
        if (readPosF_ >= endF)
        {
            playing_ = false;
            break;
        }

        int    p0   = (int)readPosF_;
        int    p1   = std::min(p0 + 1, endSample - 1);
        float  frac = (float)(readPosF_ - p0);

        outL[i] = (chL[p0] * (1.0f - frac) + chL[p1] * frac) * velocity_;
        outR[i] = (chR[p0] * (1.0f - frac) + chR[p1] * frac) * velocity_;

        readPosF_ += resampleRate_;
    }
    return playing_;
#endif
}
