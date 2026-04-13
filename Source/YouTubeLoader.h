#pragma once
#include <JuceHeader.h>
#include <functional>
#include <atomic>

// Runs yt-dlp as a child process on a dedicated background thread.
// On completion, calls the supplied callback on the message thread.
class YouTubeLoader : private juce::Thread
{
public:
    enum class Status { Idle, Downloading, Done, Error };

    using CompletionFn = std::function<void(
        std::shared_ptr<juce::AudioBuffer<float>> buffer,
        double sampleRate,
        const juce::String& errorMsg)>;

    YouTubeLoader();
    ~YouTubeLoader() override;

    // Called from message thread. Cancels any in-progress load first.
    void load(const juce::String& url, CompletionFn onComplete);
    // Load directly from an already-decoded WAV file (e.g. from cache).
    void loadFromFile(const juce::File& file, CompletionFn onComplete);
    void cancel();

    Status              getStatus()   const noexcept { return status_.load(); }
    const juce::String& getError()    const noexcept { return errorMsg_; }

private:
    void run() override;

    // Finds the yt-dlp binary inside the plugin bundle at runtime.
    // Works for both AU (.component) and VST3 (.vst3) bundles.
    juce::File findYtDlpBinary() const;

    juce::String   pendingUrl_;
    juce::File     pendingFile_;  // set by loadFromFile; overrides URL download
    CompletionFn   completion_;
    juce::CriticalSection lock_;

    std::atomic<Status> status_ { Status::Idle };
    juce::String        errorMsg_;

    juce::AudioFormatManager formatManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YouTubeLoader)
};
