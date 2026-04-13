#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
#include <thread>

//==============================================================================
// BPM detection via onset-strength autocorrelation.
// Returns estimated BPM in range [60, 200], or 0 if signal too short.
static float detectBPM(const std::vector<float>& mono, double sr)
{
    const int hopSize = 512;
    const int numHops = (int)mono.size() / hopSize;

    // Need at least ~3 seconds
    if (numHops < (int)(sr * 3.0 / hopSize))
        return 0.0f;

    // Build onset strength: half-wave rectified spectral flux (energy derivative)
    std::vector<float> onset(numHops, 0.0f);
    float prevEnergy = 0.0f;
    for (int h = 0; h < numHops; ++h)
    {
        float energy = 0.0f;
        int s0 = h * hopSize;
        int s1 = std::min(s0 + hopSize, (int)mono.size());
        for (int i = s0; i < s1; ++i)
            energy += mono[i] * mono[i];
        energy /= (float)(s1 - s0);
        onset[h] = std::max(0.0f, energy - prevEnergy);
        prevEnergy = energy;
    }

    // Autocorrelate onset strength over lags for 60-200 BPM
    double hopDur = (double)hopSize / sr;
    int minLag = std::max(1, (int)std::round(60.0 / (200.0 * hopDur)));
    int maxLag = std::min((int)std::round(60.0 / (60.0 * hopDur)), numHops / 2);

    float bestCorr = -1.0f;
    int   bestLag  = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float corr = 0.0f;
        int   n    = numHops - lag;
        for (int i = 0; i < n; ++i)
            corr += onset[i] * onset[i + lag];
        corr /= (float)n;
        if (corr > bestCorr) { bestCorr = corr; bestLag = lag; }
    }

    double bpm = 60.0 / ((double)bestLag * hopDur);
    // Round to nearest 0.5 BPM and clamp
    bpm = std::round(bpm * 2.0) / 2.0;
    return (float)juce::jlimit(60.0, 200.0, bpm);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FluTubeProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addFloat = [&](const juce::String& id, const juce::String& name,
                        float min, float max, float def)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, name,
            juce::NormalisableRange<float>(min, max), def));
    };
    auto addInt = [&](const juce::String& id, const juce::String& name,
                      int min, int max, int def)
    {
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ id, 1 }, name, min, max, def));
    };

    addFloat("master_gain",    "Master Gain",    0.0f,  1.0f,  0.85f);
    addInt  ("root_note",      "Root Note",      0,     127,   60);
    addFloat("start_point",    "Start",          0.0f,  1.0f,  0.0f);
    addFloat("end_point",      "End",            0.0f,  1.0f,  1.0f);
    addFloat("tape_mix",       "Tape Mix",       0.0f,  1.0f,  0.0f);
    addFloat("tape_time",      "Tape Time",      0.0f,  1.0f,  0.35f);
    addFloat("tape_feedback",  "Tape Feedback",  0.0f,  0.9f,  0.4f);
    addFloat("tape_wobble",    "Tape Wobble",    0.0f,  1.0f,  0.25f);
    addFloat("vinyl_noise",    "Vinyl Noise",    0.0f,  1.0f,  0.0f);
    addFloat("vinyl_crackle",  "Vinyl Crackle",  0.0f,  1.0f,  0.0f);
    addFloat("hum_level",      "Hum Level",      0.0f,  1.0f,  0.0f);
    addFloat("bitcrush_bits",  "Bit Crush",   0.0f,  1.0f,  0.0f);
    addFloat("bitcrush_rate",  "Crush Rate",  0.0f,  1.0f,  0.0f);
    addFloat("hazy_amount",    "Hazy",           0.0f,  1.0f,  0.0f);
    addFloat("wavy_amount",    "Wavy",           0.0f,  1.0f,  0.0f);
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "fx_bypass", 1 }, "FX Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "hold_mode", 1 }, "Hold Mode", false));

    return { params.begin(), params.end() };
}

//==============================================================================
FluTubeProcessor::FluTubeProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FluTubeParams", createParameterLayout())
{
}

FluTubeProcessor::~FluTubeProcessor()
{
    thumbAlive_->store(false);   // signal any live thumbnail threads to abort
    loader_.cancel();
}

//==============================================================================
void FluTubeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleEngine_.prepare(sampleRate, samplesPerBlock);
    loFiChain_.prepare(sampleRate, samplesPerBlock);

    renderBufL_.resize((size_t)samplesPerBlock, 0.0f);
    renderBufR_.resize((size_t)samplesPerBlock, 0.0f);
}

void FluTubeProcessor::releaseResources()
{
    sampleEngine_.releaseResources();
}

bool FluTubeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    return true;
}

//==============================================================================
void FluTubeProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    // ── Thread-safe buffer swap ──────────────────────────────────────────────
    if (pendingReady_.load())
    {
        juce::SpinLock::ScopedTryLockType tryLock(bufferLock_);
        if (tryLock.isLocked())
        {
            std::swap(activeBuffer_,   pendingBuffer_);
            std::swap(activeChannels_, pendingChannels_);
            std::swap(activeFrames_,   pendingFrames_);
            std::swap(activeSr_,       pendingSr_);
            pendingReady_.store(false);

            sampleEngine_.setBuffer(activeBuffer_.empty() ? nullptr : activeBuffer_.data(),
                                    activeFrames_, activeChannels_, activeSr_);
        }
    }

    // ── Sync APVTS params to DSP objects ─────────────────────────────────────
    {
        auto& a = apvts;
        sampleEngine_.startPoint.store(*a.getRawParameterValue("start_point"));
        sampleEngine_.endPoint  .store(*a.getRawParameterValue("end_point"));
        sampleEngine_.setRootNote((int)*a.getRawParameterValue("root_note"));

        loFiChain_.setTapeDelayMix       (*a.getRawParameterValue("tape_mix"));
        loFiChain_.setTapeDelayTime      (*a.getRawParameterValue("tape_time"));
        loFiChain_.setTapeDelayFeedback  (*a.getRawParameterValue("tape_feedback"));
        loFiChain_.setTapeDelayWobble    (*a.getRawParameterValue("tape_wobble"));
        loFiChain_.setVinylNoise         (*a.getRawParameterValue("vinyl_noise"));
        loFiChain_.setVinylCrackle       (*a.getRawParameterValue("vinyl_crackle"));
        loFiChain_.setHumLevel           (*a.getRawParameterValue("hum_level"));
        loFiChain_.setBitcrushBits       (*a.getRawParameterValue("bitcrush_bits"));
        loFiChain_.setBitcrushSampleRate (*a.getRawParameterValue("bitcrush_rate"));
        loFiChain_.setHazyAmount         (*a.getRawParameterValue("hazy_amount"));
        loFiChain_.setWavyAmount         (*a.getRawParameterValue("wavy_amount"));

        sampleEngine_.setHoldMode(*a.getRawParameterValue("hold_mode") > 0.5f);
        sampleEngine_.setTempoRatio(syncTempoRatio.load());
    }

    // ── MIDI events → noteOn/noteOff ─────────────────────────────────────────
    for (const auto& meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getVelocity() > 0)
            sampleEngine_.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
            sampleEngine_.noteOff(msg.getNoteNumber());
    }

    // ── Render sample engine ──────────────────────────────────────────────────
    if ((int)renderBufL_.size() < numSamples)
    {
        renderBufL_.resize((size_t)numSamples, 0.0f);
        renderBufR_.resize((size_t)numSamples, 0.0f);
    }
    std::fill(renderBufL_.begin(), renderBufL_.begin() + numSamples, 0.0f);
    std::fill(renderBufR_.begin(), renderBufR_.begin() + numSamples, 0.0f);

    sampleEngine_.render(renderBufL_.data(), renderBufR_.data(), numSamples);

    // ── Lo-fi effects ─────────────────────────────────────────────────────────
    bool bypassed = *apvts.getRawParameterValue("fx_bypass") > 0.5f;
    if (!bypassed)
        loFiChain_.processBlock(renderBufL_.data(), renderBufR_.data(), numSamples);

    // ── Apply master gain + write to output ───────────────────────────────────
    float gain = *apvts.getRawParameterValue("master_gain");
    if (buffer.getNumChannels() >= 2)
    {
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            L[i] = renderBufL_[i] * gain;
            R[i] = renderBufR_[i] * gain;
        }
    }

    // ── VU meter ──────────────────────────────────────────────────────────────
    float peak = 0.0f;
    if (buffer.getNumChannels() > 0)
    {
        auto* data = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
    vuLevel.store(std::max(peak, vuLevel.load() * 0.92f));

    midiMessages.clear();
}

//==============================================================================
void FluTubeProcessor::loadFromUrl(const juce::String& urlOrPath)
{
    lastLoadedUrl = urlOrPath;

    // ── Local file path ───────────────────────────────────────────────────────
    juce::File localFile(urlOrPath);
    if (localFile.existsAsFile())
    {
        // Clear any previous thumbnail — local files don't have one
        { juce::ScopedLock sl(thumbnailLock); pendingThumbnail_ = {}; }
        newThumbnailReady.store(true);

        loader_.loadFromFile(localFile, [this](std::shared_ptr<juce::AudioBuffer<float>> buf,
                                               double sr, const juce::String& err)
        {
            onSampleLoaded(std::move(buf), sr, err);
        });
        return;
    }

    // ── URL: use cached WAV if available ─────────────────────────────────────
    juce::File cached = getCacheFile(urlOrPath);
    if (cached.existsAsFile())
    {
        loader_.loadFromFile(cached, [this](std::shared_ptr<juce::AudioBuffer<float>> buf,
                                            double sr, const juce::String& err)
        {
            onSampleLoaded(std::move(buf), sr, err);
        });
        return;
    }

    loader_.load(urlOrPath, [this](std::shared_ptr<juce::AudioBuffer<float>> buf,
                                   double sr, const juce::String& err)
    {
        onSampleLoaded(std::move(buf), sr, err);
    });
}

void FluTubeProcessor::onSampleLoaded(std::shared_ptr<juce::AudioBuffer<float>> buf,
                                      double fileSr,
                                      const juce::String& err)
{
    // Called on the message thread via callAsync in YouTubeLoader.
    if (!err.isEmpty() || !buf) return;

    int numCh     = std::min(buf->getNumChannels(), 2);
    int numFrames = buf->getNumSamples();

    // Build non-interleaved flat buffer: [L: 0..N-1] [R: N..2N-1]
    std::vector<float> data((size_t)(numCh * numFrames));
    for (int ch = 0; ch < numCh; ++ch)
        std::copy(buf->getReadPointer(ch),
                  buf->getReadPointer(ch) + numFrames,
                  data.data() + (size_t)(ch * numFrames));

    // Swap in (holds lock only for the std::swap)
    {
        juce::SpinLock::ScopedLockType lock(bufferLock_);
        pendingBuffer_   = std::move(data);
        pendingChannels_ = numCh;
        pendingFrames_   = numFrames;
        pendingSr_       = fileSr;
        pendingReady_.store(true);
    }

    // Build mono waveform for the editor
    std::vector<float> mono((size_t)numFrames);
    auto* chL = buf->getReadPointer(0);
    auto* chR = (numCh >= 2) ? buf->getReadPointer(1) : buf->getReadPointer(0);
    for (int i = 0; i < numFrames; ++i)
        mono[(size_t)i] = (chL[i] + chR[i]) * 0.5f;

    // Detect BPM before moving mono (runs on message thread, can be slow)
    float bpm = detectBPM(mono, fileSr);
    detectedSampleBPM.store(bpm);

    {
        juce::ScopedLock sl(waveformLock);
        pendingWaveformData = std::move(mono);
        newWaveformReady.store(true);
    }

    // Persist to cache so the sample survives project save/reopen.
    // saveToCache is a no-op if the file already exists (i.e. we loaded from cache).
    if (lastLoadedUrl.isNotEmpty())
        saveToCache(lastLoadedUrl, buf, fileSr);

    // Kick off thumbnail download for YouTube URLs
    juce::String videoId = extractYouTubeVideoId(lastLoadedUrl);
    if (videoId.isNotEmpty())
        startThumbnailDownload(videoId);
}

//==============================================================================
juce::AudioProcessorEditor* FluTubeProcessor::createEditor()
{
    return new FluTubeEditor(*this);
}

//==============================================================================
void FluTubeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state = apvts.copyState();

    // Persist the last loaded URL so the UI can show it on project reload
    state.setProperty("lastLoadedUrl", lastLoadedUrl, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FluTubeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml) return;

    juce::ValueTree state = juce::ValueTree::fromXml(*xml);
    if (state.isValid())
    {
        apvts.replaceState(state);
        lastLoadedUrl = state.getProperty("lastLoadedUrl", "").toString();

        // Reload the sample on the next message loop cycle so the plugin
        // is fully initialised (prepareToPlay may not have run yet).
        if (lastLoadedUrl.isNotEmpty())
        {
            juce::String url = lastLoadedUrl;
            juce::MessageManager::callAsync([this, url]()
            {
                loadFromUrl(url);
            });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FluTubeProcessor();
}

juce::File FluTubeProcessor::getCacheFile(const juce::String& url) const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("FluTube").getChildFile("cache");
    dir.createDirectory();
    return dir.getChildFile("flutube_" + juce::String::toHexString(url.hashCode64()) + ".wav");
}

void FluTubeProcessor::saveToCache(const juce::String& url,
                                    std::shared_ptr<juce::AudioBuffer<float>> buf,
                                    double fileSr)
{
    juce::File cacheFile = getCacheFile(url);
    if (cacheFile.existsAsFile()) return; // already cached

    juce::WavAudioFormat wavFmt;
    auto os = std::unique_ptr<juce::FileOutputStream>(cacheFile.createOutputStream());
    if (!os) return;

    const int numCh     = buf->getNumChannels();
    const int numFrames = buf->getNumSamples();
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wavFmt.createWriterFor(os.get(), fileSr, (unsigned int)numCh, 16, {}, 0));
    if (writer)
    {
        os.release(); // writer takes ownership of stream
        writer->writeFromAudioSampleBuffer(*buf, 0, numFrames);
        // writer destructor flushes and closes
    }
}


double FluTubeProcessor::getCurrentBPM() const
{
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                return *bpm;
        }
    }
    return 120.0;
}

//==============================================================================
juce::String FluTubeProcessor::extractYouTubeVideoId(const juce::String& url)
{
    // https://youtu.be/VIDEO_ID
    if (url.contains("youtu.be/"))
    {
        int idx = url.indexOf("youtu.be/") + 9;
        return url.substring(idx)
                  .upToFirstOccurrenceOf("?", false, false)
                  .upToFirstOccurrenceOf("&", false, false)
                  .trim();
    }
    // https://www.youtube.com/watch?v=VIDEO_ID
    if (url.contains("v="))
    {
        int idx = url.indexOf("v=") + 2;
        return url.substring(idx)
                  .upToFirstOccurrenceOf("&", false, false)
                  .upToFirstOccurrenceOf("#", false, false)
                  .trim();
    }
    return {};
}

juce::File FluTubeProcessor::getThumbnailCacheFile(const juce::String& videoId) const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("FluTube").getChildFile("cache");
    dir.createDirectory();
    return dir.getChildFile("thumb_" + videoId + ".jpg");
}

void FluTubeProcessor::startThumbnailDownload(const juce::String& videoId)
{
    juce::File cached = getThumbnailCacheFile(videoId);

    // Fast path: already cached
    if (cached.existsAsFile())
    {
        juce::Image img = juce::ImageFileFormat::loadFrom(cached);
        if (img.isValid())
        {
            juce::ScopedLock sl(thumbnailLock);
            pendingThumbnail_ = img;
            newThumbnailReady.store(true);
        }
        return;
    }

    // Download in a detached thread. thumbAlive_ lets the thread detect destruction.
    auto alive = thumbAlive_;
    juce::String thumbUrl = "https://img.youtube.com/vi/" + videoId + "/mqdefault.jpg";
    juce::String cachePath = cached.getFullPathName();

    std::thread([this, alive, thumbUrl, cachePath]()
    {
        auto stream = juce::URL(thumbUrl).createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(8000)
                .withNumRedirectsToFollow(3));
        if (!stream || !alive->load()) return;

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock(data);
        if (data.isEmpty() || !alive->load()) return;

        juce::File(cachePath).replaceWithData(data.getData(), data.getSize());

        juce::MemoryInputStream mis(data.getData(), data.getSize(), false);
        juce::Image img = juce::ImageFileFormat::loadFrom(mis);
        if (!img.isValid() || !alive->load()) return;

        juce::ScopedLock sl(thumbnailLock);
        pendingThumbnail_ = img;
        newThumbnailReady.store(true);
    }).detach();
}
