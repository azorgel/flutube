#pragma once
#include <JuceHeader.h>
#include "YouTubeLoader.h"
#include "SampleEngine.h"
#include "LoFiChain.h"
#include <atomic>
#include <vector>
#include <memory>

class FluTubeProcessor : public juce::AudioProcessor
{
public:
    FluTubeProcessor();
    ~FluTubeProcessor() override;

    // ── AudioProcessor ──────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return true; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Parameter tree ──────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── YouTube loading (message thread) ────────────────────────────────────
    void loadFromUrl(const juce::String& urlOrPath);  // also handles local file paths
    YouTubeLoader::Status getLoaderStatus()   const { return loader_.getStatus(); }
    const juce::String&   getLoaderError()    const { return loader_.getError();  }

    // ── Thumbnail (YouTube only; updated on message thread via newThumbnailReady) ──
    std::atomic<bool>   newThumbnailReady { false };
    juce::CriticalSection thumbnailLock;
    juce::Image         pendingThumbnail_;  // guarded by thumbnailLock

    // ── VU meter (audio thread writes, UI reads) ────────────────────────────
    std::atomic<float> vuLevel { 0.0f };

    // ── BPM sync (editor writes syncTempoRatio, audio thread reads it) ─────
    std::atomic<float> detectedSampleBPM { 0.0f };   // detected on load
    std::atomic<float> syncTempoRatio    { 1.0f };   // projectBPM/sampleBPM or 1.0

    // ── Waveform data for the editor (message thread only) ──────────────────
    std::atomic<bool>  newWaveformReady { false };
    juce::CriticalSection waveformLock;
    std::vector<float> pendingWaveformData; // mono mix

    // ── Last loaded URL (for state persistence) ─────────────────────────────
    juce::String lastLoadedUrl;

    // Sample info
    int    getNumSampleFrames()   const noexcept { return activeFrames_; }
    double getActiveSampleRate()  const noexcept { return activeSr_; }
    double getCurrentBPM()        const;

    // Waveform preview — safe to call from message thread
    void  requestPreview(float normPos) { sampleEngine_.requestPreview(normPos); }
    void  stopPreview()                 { sampleEngine_.stopPreview(); }
    float getPlayheadNorm()       const { return sampleEngine_.getPlayheadNorm(); }

    // DSP objects (read by Editor for waveform init after load)
    SampleEngine sampleEngine_;

private:
    void onSampleLoaded(std::shared_ptr<juce::AudioBuffer<float>> buf,
                        double fileSr,
                        const juce::String& err);

    // Returns the persistent cache file for a given URL (creates dir if needed).
    juce::File getCacheFile(const juce::String& url) const;
    // Writes buf to cacheFile as 16-bit WAV in the background.
    void saveToCache(const juce::String& url,
                     std::shared_ptr<juce::AudioBuffer<float>> buf,
                     double fileSr);

    // Thumbnail helpers
    static juce::String extractYouTubeVideoId(const juce::String& url);
    juce::File getThumbnailCacheFile(const juce::String& videoId) const;
    void startThumbnailDownload(const juce::String& videoId);
    // Shared flag lets background download threads detect plugin destruction safely.
    std::shared_ptr<std::atomic<bool>> thumbAlive_ = std::make_shared<std::atomic<bool>>(true);

    YouTubeLoader loader_;
    LoFiChain     loFiChain_;

    // ── Thread-safe buffer swap ─────────────────────────────────────────────
    // Non-interleaved layout: [L: 0..N-1] [R: N..2N-1] (if stereo)
    std::vector<float> activeBuffer_, pendingBuffer_;
    int    activeChannels_  = 0, pendingChannels_  = 0;
    int    activeFrames_    = 0, pendingFrames_    = 0;
    double activeSr_        = 44100.0, pendingSr_  = 44100.0;

    juce::SpinLock       bufferLock_;
    std::atomic<bool>    pendingReady_ { false };

    // Per-block stereo render scratch buffers
    std::vector<float> renderBufL_, renderBufR_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluTubeProcessor)
};
