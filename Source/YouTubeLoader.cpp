#include "YouTubeLoader.h"
#include <cmath>

YouTubeLoader::YouTubeLoader()
    : juce::Thread("FluTube-YTLoader")
{
    formatManager_.registerBasicFormats();
}

YouTubeLoader::~YouTubeLoader()
{
    cancel();
}

void YouTubeLoader::load(const juce::String& url, CompletionFn onComplete)
{
    cancel(); // stop any running download first

    {
        juce::ScopedLock sl(lock_);
        pendingUrl_  = url;
        pendingFile_ = juce::File();  // clear any pending file
        completion_  = std::move(onComplete);
    }

    status_.store(Status::Downloading);
    startThread(juce::Thread::Priority::background);
}

void YouTubeLoader::loadFromFile(const juce::File& file, CompletionFn onComplete)
{
    cancel();

    {
        juce::ScopedLock sl(lock_);
        pendingUrl_  = {};
        pendingFile_ = file;
        completion_  = std::move(onComplete);
    }

    status_.store(Status::Downloading);
    startThread(juce::Thread::Priority::background);
}

void YouTubeLoader::cancel()
{
    if (isThreadRunning())
    {
        signalThreadShouldExit();
        stopThread(8000);
    }
    status_.store(Status::Idle);
}

juce::File YouTubeLoader::findYtDlpBinary() const
{
    // currentExecutableFile returns, e.g.:
    //   .../FluTube.component/Contents/MacOS/FluTube
    //   .../FluTube.vst3/Contents/MacOS/FluTube
    // Navigate up to Contents/, then into Resources/yt-dlp.
    juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File resources = exe.getParentDirectory()  // MacOS/
                              .getParentDirectory()  // Contents/
                              .getChildFile("Resources");
#if JUCE_WINDOWS
    juce::File binary = resources.getChildFile("yt-dlp.exe");
#else
    juce::File binary = resources.getChildFile("yt-dlp");
#endif

    // Debug: log path to stderr so developers can verify during testing
    DBG("FluTube: looking for yt-dlp at " + binary.getFullPathName()
        + (binary.existsAsFile() ? " [found]" : " [NOT FOUND]"));

    return binary;
}

void YouTubeLoader::run()
{
    juce::String url;
    juce::File   pendingFile;
    CompletionFn completion;

    {
        juce::ScopedLock sl(lock_);
        url         = pendingUrl_;
        pendingFile = pendingFile_;
        completion  = completion_;
    }

    auto reportError = [&](const juce::String& msg)
    {
        errorMsg_ = msg;
        status_.store(Status::Error);
        if (completion)
        {
            juce::MessageManager::callAsync([completion, msg]()
            {
                completion(nullptr, 44100.0, msg);
            });
        }
    };

    // ── Fast path: load directly from a file (e.g. cache) ───────────────────
    if (pendingFile.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(pendingFile));

        if (!reader)
        {
            reportError("Could not read cached audio file: " + pendingFile.getFullPathName());
            return;
        }

        const int maxFrames  = (int)(reader->sampleRate * 600.0);
        const int numFrames  = (int)std::min(reader->lengthInSamples, (juce::int64)maxFrames);
        const int numCh      = (int)std::min((int)reader->numChannels, 2);
        const double fileSr  = reader->sampleRate;

        auto buf = std::make_shared<juce::AudioBuffer<float>>(numCh, numFrames);
        reader->read(buf.get(), 0, numFrames, 0, true, true);

        status_.store(Status::Done);
        if (completion)
        {
            juce::MessageManager::callAsync([completion, buf, fileSr]()
            {
                completion(buf, fileSr, {});
            });
        }
        return;
    }

    // ── Locate yt-dlp ────────────────────────────────────────────────────
    juce::File ytdlp = findYtDlpBinary();
    if (!ytdlp.existsAsFile())
    {
        reportError("yt-dlp not found at " + ytdlp.getFullPathName()
                    + ". Please add it to the plugin bundle's Resources folder.");
        return;
    }
    ytdlp.setExecutePermission(true);

    // ── Temp output file ─────────────────────────────────────────────────
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::String stem  = "flutube_" + juce::String(juce::Time::currentTimeMillis());
    juce::File   expectedWav  = tempDir.getChildFile(stem + ".wav");
    juce::String outputTemplate = tempDir.getChildFile(stem + ".%(ext)s").getFullPathName();

    // ── Locate ffmpeg (DAWs often lack /opt/homebrew/bin in PATH) ────────────
    juce::String ffmpegPath;
    for (const auto* candidate : { "/opt/homebrew/bin/ffmpeg",
                                    "/usr/local/bin/ffmpeg",
                                    "/usr/bin/ffmpeg" })
    {
        if (juce::File(candidate).existsAsFile())
        {
            ffmpegPath = candidate;
            break;
        }
    }

    // ── Launch yt-dlp ─────────────────────────────────────────────────────
    juce::StringArray args;
    args.add(ytdlp.getFullPathName());
    args.add("-x");
    args.add("--audio-format"); args.add("wav");
    args.add("--audio-quality"); args.add("0");   // best quality
    args.add("--no-playlist");
    args.add("--no-part");
    if (ffmpegPath.isNotEmpty())
    {
        args.add("--ffmpeg-location"); args.add(ffmpegPath);
    }
    args.add("-o"); args.add(outputTemplate);
    args.add(url);

    juce::ChildProcess proc;
    if (!proc.start(args))
    {
        reportError("Failed to start yt-dlp. Check that the binary is executable.");
        return;
    }

    // Drain output while watching for thread exit signal
    juce::String procOutput;
    while (proc.isRunning())
    {
        if (threadShouldExit())
        {
            proc.kill();
            return;
        }
        juce::Thread::sleep(200);
        procOutput += proc.readAllProcessOutput();
    }
    procOutput += proc.readAllProcessOutput();
    proc.waitForProcessToFinish(5000);

    if (threadShouldExit()) return;

    // ── Decode the resulting WAV ──────────────────────────────────────────
    if (!expectedWav.existsAsFile())
    {
        // Extract last non-empty line from yt-dlp output for a useful error
        juce::StringArray lines = juce::StringArray::fromLines(procOutput.trim());
        juce::String lastLine;
        for (int i = lines.size() - 1; i >= 0; --i)
        {
            if (lines[i].trim().isNotEmpty()) { lastLine = lines[i].trim(); break; }
        }
        juce::String detail = lastLine.isEmpty() ? "URL may be invalid or unavailable."
                                                 : lastLine.substring(0, 120);
        reportError("yt-dlp failed: " + detail);
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(expectedWav));

    if (!reader)
    {
        expectedWav.deleteFile();
        reportError("Could not read the downloaded audio file.");
        return;
    }

    const int maxFrames  = (int)(reader->sampleRate * 600.0); // cap at 10 minutes
    const int numFrames  = (int)std::min(reader->lengthInSamples, (juce::int64)maxFrames);
    const int numCh      = (int)std::min((int)reader->numChannels, 2);
    const double fileSr  = reader->sampleRate;

    auto buf = std::make_shared<juce::AudioBuffer<float>>(numCh, numFrames);
    reader->read(buf.get(), 0, numFrames, 0, true, true);
    reader.reset();
    expectedWav.deleteFile();

    if (threadShouldExit()) return;

    status_.store(Status::Done);
    if (completion)
    {
        juce::MessageManager::callAsync([completion, buf, fileSr]()
        {
            completion(buf, fileSr, {});
        });
    }
}
