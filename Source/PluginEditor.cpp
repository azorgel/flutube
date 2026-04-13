#include "PluginEditor.h"
#include <cmath>

//==============================================================================
const char* FluTubeEditor::PARAM_IDS[FluTubeEditor::NUM_KNOBS] = {
    "tape_mix", "tape_time", "tape_feedback", "tape_wobble",
    "vinyl_noise", "vinyl_crackle",
    "hum_level",
    "bitcrush_bits", "bitcrush_rate",
    "hazy_amount", "wavy_amount",
    "master_gain"
};

const char* FluTubeEditor::KNOB_LABELS[FluTubeEditor::NUM_KNOBS] = {
    "MIX", "TIME", "FDBK", "WBL",
    "NOISE", "CRK",
    "HUM",
    "BITS", "RATE",
    "HAZY", "WAVY",
    "MASTER"
};

// Layout (700px wide):
//  TAPE DELAY x=14 w=152 | VINYL x=170 w=136 | HUM x=310 w=80
//  BITCRUSH x=394 w=108  | HAZY  x=506 w=52  | WAVY x=562 w=52
//  MASTER x=618 w=68
// All knob relX/relY are relative to effectsBounds().getX() + MARGIN.
const FluTubeEditor::KnobInfo FluTubeEditor::KNOB_INFO[FluTubeEditor::NUM_KNOBS] = {
    {  40,  60, 0xff00e5ff }, // K_TAPE_MIX    — cyan
    { 100,  60, 0xff00e5ff }, // K_TAPE_TIME   — cyan
    {  40, 135, 0xffb826ff }, // K_TAPE_FDBK   — purple
    { 100, 135, 0xffb826ff }, // K_TAPE_WBL    — purple
    { 210,  60, 0xffff2d87 }, // K_VINYL_NOISE — pink
    { 270,  60, 0xffff2d87 }, // K_VINYL_CRK   — pink
    { 336,  90, 0xffb826ff }, // K_HUM         — purple
    { 411,  60, 0xff00e5ff }, // K_BITS        — cyan
    { 461,  60, 0xffb826ff }, // K_RATE        — purple
    { 519,  90, 0xffcc88ee }, // K_HAZY        — pastel lavender
    { 575,  90, 0xff22ddaa }, // K_WAVY        — teal
    { 638,  90, 0xffffb800 }, // K_MASTER      — gold
};

//==============================================================================
FluTubeEditor::FluTubeEditor(FluTubeProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(W, H);
    setResizable(false, false);

    // ── Pre-render noise texture ──────────────────────────────────────────────
    noiseTexture_ = juce::Image(juce::Image::ARGB, W, H, true);
    {
        juce::Graphics tg(noiseTexture_);
        juce::Random rng(0xdeadbeef);
        for (int y = 0; y < H; y += 2)
        {
            for (int x = 0; x < W; ++x)
            {
                float v = rng.nextFloat();
                if (v > 0.95f)
                {
                    float a = 0.02f + rng.nextFloat() * 0.025f;
                    tg.setColour(juce::Colours::white.withAlpha(a));
                    tg.fillRect(x, y, 1, 1);
                }
            }
        }
    }

    juce::String monoName = juce::Font::getDefaultMonospacedFontName();

    // ── URL input ─────────────────────────────────────────────────────────────
    urlInput_.setMultiLine(false);
    urlInput_.setReturnKeyStartsNewLine(false);
    urlInput_.setScrollbarsShown(false);
    urlInput_.setPopupMenuEnabled(false);
    urlInput_.setFont(juce::Font(juce::FontOptions{}.withName(monoName).withHeight(11.5f)));
    urlInput_.setColour(juce::TextEditor::backgroundColourId,     colSurface);
    urlInput_.setColour(juce::TextEditor::textColourId,           colText);
    urlInput_.setColour(juce::TextEditor::outlineColourId,        colBorder);
    urlInput_.setColour(juce::TextEditor::focusedOutlineColourId, colCyan);
    urlInput_.setColour(juce::TextEditor::highlightColourId,      colCyan.withAlpha(0.25f));
    urlInput_.setTextToShowWhenEmpty("https://www.youtube.com/watch?v=...",
                                     colTextDim.withAlpha(0.9f));
    urlInput_.setText(processor_.lastLoadedUrl, false);
    urlInput_.onReturnKey = [this] { loadButton_.triggerClick(); };
    addAndMakeVisible(urlInput_);

    // ── Load button ──────────────────────────────────────────────────────────
    loadButton_.setColour(juce::TextButton::buttonColourId,   colSurface2);
    loadButton_.setColour(juce::TextButton::buttonOnColourId, colBorder);
    loadButton_.setColour(juce::TextButton::textColourOffId,  colCyan);
    loadButton_.setColour(juce::TextButton::textColourOnId,   colCyan.brighter(0.3f));
    loadButton_.onClick = [this]()
    {
        juce::String url = urlInput_.getText().trim();
        if (url.isNotEmpty())
        {
            statusLabel_.setText("Downloading...", juce::dontSendNotification);
            processor_.loadFromUrl(url);
        }
    };
    addAndMakeVisible(loadButton_);

    // ── File browser button ───────────────────────────────────────────────────
    fileButton_.setColour(juce::TextButton::buttonColourId,  colSurface2);
    fileButton_.setColour(juce::TextButton::textColourOffId, colText);
    fileButton_.onClick = [this]()
    {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Load Audio File",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg;*.m4a");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    urlInput_.setText(f.getFullPathName(), false);
                    statusLabel_.setText("Loading...", juce::dontSendNotification);
                    processor_.loadFromUrl(f.getFullPathName());
                }
            });
    };
    addAndMakeVisible(fileButton_);

    // ── Status label ─────────────────────────────────────────────────────────
    statusLabel_.setFont(juce::Font(juce::FontOptions{}.withName(monoName).withHeight(10.0f)));
    statusLabel_.setColour(juce::Label::textColourId, colTextDim);
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setText("READY", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    // ── Waveform display ──────────────────────────────────────────────────────
    waveformDisplay_.setListener(this);
    addAndMakeVisible(waveformDisplay_);

    // ── Root note combo ───────────────────────────────────────────────────────
    static const char* noteNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    for (int n = 0; n <= 127; ++n)
    {
        int octave = n / 12 - 1;
        juce::String name = juce::String(noteNames[n % 12]) + juce::String(octave);
        rootNoteCombo_.addItem(name, n + 1);
    }
    rootNoteCombo_.setColour(juce::ComboBox::backgroundColourId, colSurface);
    rootNoteCombo_.setColour(juce::ComboBox::textColourId,       colText);
    rootNoteCombo_.setColour(juce::ComboBox::outlineColourId,    colBorder);
    rootNoteCombo_.setColour(juce::ComboBox::arrowColourId,      colCyan);
    int initRoot = (int)*processor_.apvts.getRawParameterValue("root_note");
    rootNoteCombo_.setSelectedId(initRoot + 1, juce::dontSendNotification);
    rootNoteCombo_.onChange = [this]()
    {
        int note = rootNoteCombo_.getSelectedId() - 1;
        if (auto* param = processor_.apvts.getParameter("root_note"))
            param->setValueNotifyingHost((float)note / 127.0f);
    };
    addAndMakeVisible(rootNoteCombo_);

    // ── Bypass button ─────────────────────────────────────────────────────────
    bypassButton_.setClickingTogglesState(true);
    bypassButton_.setColour(juce::TextButton::buttonColourId,   colSurface2);
    bypassButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d0028));
    bypassButton_.setColour(juce::TextButton::textColourOffId,  colGreen);
    bypassButton_.setColour(juce::TextButton::textColourOnId,   colPink);
    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.apvts, "fx_bypass", bypassButton_);
    addAndMakeVisible(bypassButton_);

    // ── Hold button ───────────────────────────────────────────────────────────
    holdButton_.setClickingTogglesState(true);
    holdButton_.setColour(juce::TextButton::buttonColourId,   colSurface2);
    holdButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff002840));
    holdButton_.setColour(juce::TextButton::textColourOffId,  colTextDim);
    holdButton_.setColour(juce::TextButton::textColourOnId,   colCyan);
    holdAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.apvts, "hold_mode", holdButton_);
    addAndMakeVisible(holdButton_);

    // ── Sync BPM button (latching toggle) ────────────────────────────────────
    syncButton_.setClickingTogglesState(true);
    syncButton_.setColour(juce::TextButton::buttonColourId,   colSurface2);
    syncButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff281800));
    syncButton_.setColour(juce::TextButton::textColourOffId,  colGold);
    syncButton_.setColour(juce::TextButton::textColourOnId,   colGold.brighter(0.4f));
    syncButton_.onClick = [this]()
    {
        if (syncButton_.getToggleState())
        {
            // Turning sync ON: calculate how many musical beats the current region
            // represents at project BPM, snap the end handle to that boundary, and
            // store the stretch ratio so the next noteOn plays the region in exactly
            // that many beats. No reliance on BPM detection — works purely from
            // the region length and the DAW's current tempo.
            double projectBPM = processor_.getCurrentBPM();
            int    frames     = processor_.getNumSampleFrames();
            double sr         = processor_.getActiveSampleRate();
            if (frames > 0 && sr > 0.0 && projectBPM > 1.0)
            {
                float  startN     = *processor_.apvts.getRawParameterValue("start_point");
                float  endN       = *processor_.apvts.getRawParameterValue("end_point");
                double totalSec   = (double)frames / sr;
                double regionSec  = (double)(endN - startN) * totalSec;
                double rawBeats   = regionSec * projectBPM / 60.0;

                // Snap to nearest musical beat count
                // (favouring common loop lengths: 1, 2, 4, 8, 16, 32 beats)
                const int candidates[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
                int N = std::max(1, (int)std::round(rawBeats));
                double bestDiff = std::abs((double)N - rawBeats);
                for (int c : candidates)
                {
                    double diff = std::abs((double)c - rawBeats);
                    if (diff < bestDiff) { bestDiff = diff; N = c; }
                }

                double targetSec = N / (projectBPM / 60.0);
                // syncTempoRatio = regionSec / targetSec
                // timeRatio in RubberBand = 1 / syncTempoRatio = targetSec / regionSec
                // i.e. the sample is stretched to fit exactly N beats at projectBPM.
                processor_.syncTempoRatio.store((float)(regionSec / targetSec));

                // Snap the end handle to the clean beat boundary
                float newEndN = startN + (float)(targetSec / totalSec);
                newEndN = juce::jlimit(startN + 0.001f, 1.0f, newEndN);
                if (auto* param = processor_.apvts.getParameter("end_point"))
                    param->setValueNotifyingHost(newEndN);
                waveformDisplay_.setEndNorm(newEndN);
            }
        }
        else
        {
            // Turning sync OFF: restore unity tempo ratio
            processor_.syncTempoRatio.store(1.0f);
        }
    };
    addAndMakeVisible(syncButton_);

    // ── Initial knob values ───────────────────────────────────────────────────
    for (int k = 0; k < NUM_KNOBS; ++k)
    {
        if (auto* param = processor_.apvts.getParameter(PARAM_IDS[k]))
            knobValues_[k] = param->getValue();
    }

    startTimerHz(30);
    resized();
}

FluTubeEditor::~FluTubeEditor()
{
    stopTimer();
}

//==============================================================================
void FluTubeEditor::timerCallback()
{
    for (int k = 0; k < NUM_KNOBS; ++k)
    {
        if (auto* param = processor_.apvts.getParameter(PARAM_IDS[k]))
            knobValues_[k] = param->getValue();
    }

    {
        int note = (int)*processor_.apvts.getRawParameterValue("root_note");
        if (rootNoteCombo_.getSelectedId() != note + 1)
            rootNoteCombo_.setSelectedId(note + 1, juce::dontSendNotification);
    }

    if (processor_.newWaveformReady.exchange(false))
    {
        juce::ScopedLock sl(processor_.waveformLock);
        waveformDisplay_.setWaveformData(processor_.pendingWaveformData);
    }

    if (processor_.newThumbnailReady.exchange(false))
    {
        juce::ScopedLock sl(processor_.thumbnailLock);
        if (processor_.pendingThumbnail_.isValid())
            waveformDisplay_.setThumbnail(processor_.pendingThumbnail_);
        else
            waveformDisplay_.clearThumbnail();
    }

    {
        auto st = processor_.getLoaderStatus();
        juce::String txt;
        juce::Colour col = colTextDim;
        switch (st)
        {
            case YouTubeLoader::Status::Downloading:
                txt = "> DOWNLOADING..."; col = colCyan; break;
            case YouTubeLoader::Status::Done:
                txt = "> LOADED  " + juce::String(processor_.getNumSampleFrames() / 44100) + "s";
                col = colGreen; break;
            case YouTubeLoader::Status::Error:
                txt = "> ERR: " + processor_.getLoaderError().substring(0, 60);
                col = colRed; break;
            default:
                txt = "> READY";
        }
        statusLabel_.setText(txt, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, col);
    }

    bool bypassed = bypassButton_.getToggleState();
    bypassButton_.setButtonText(bypassed ? "BYPASSED" : "FX ACTIVE");

    // Update sync tempo ratio continuously so projectBPM changes are tracked
    {
        bool syncing = syncButton_.getToggleState();
        if (syncing)
        {
            double projectBPM = processor_.getCurrentBPM();
            int    frames     = processor_.getNumSampleFrames();
            double sr         = processor_.getActiveSampleRate();
            if (frames > 0 && sr > 0.0 && projectBPM > 1.0)
            {
                float  startN    = *processor_.apvts.getRawParameterValue("start_point");
                float  endN      = *processor_.apvts.getRawParameterValue("end_point");
                double totalSec  = (double)frames / sr;
                double regionSec = (double)(endN - startN) * totalSec;
                double rawBeats  = regionSec * projectBPM / 60.0;

                const int candidates[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
                int N = std::max(1, (int)std::round(rawBeats));
                double bestDiff = std::abs((double)N - rawBeats);
                for (int c : candidates)
                {
                    double diff = std::abs((double)c - rawBeats);
                    if (diff < bestDiff) { bestDiff = diff; N = c; }
                }
                double targetSec = N / (projectBPM / 60.0);
                processor_.syncTempoRatio.store((float)(regionSec / targetSec));
            }
            syncButton_.setButtonText("SYNC ON");
        }
        else
        {
            processor_.syncTempoRatio.store(1.0f);
            float detBPM = processor_.detectedSampleBPM.load();
            syncButton_.setButtonText(detBPM > 0.0f ? "SYNC BPM" : "SYNC BPM");
        }
    }

    float vu = processor_.vuLevel.load();
    vuSmoothed_ = vuSmoothed_ * 0.7f + vu * 0.3f;

    // Push playhead cursor into waveform display
    waveformDisplay_.setPlayheadNorm(processor_.getPlayheadNorm());

    // Advance WAVY knob animation phase (~0.4 Hz visual oscillation at 30 fps)
    wavyAnimPhase_ += 0.083f;
    if (wavyAnimPhase_ > 6.28318f) wavyAnimPhase_ -= 6.28318f;

    repaint(headerBounds());
    repaint(pitchBounds());              // BPM display + sync button state
    repaint(effectsBounds().withHeight(24));
    // Repaint WAVY knob area continuously for animation
    if (knobValues_[K_WAVY] > 0.01f)
        repaint(knobHitBounds(K_WAVY).expanded(12));
}

//==============================================================================
juce::Rectangle<int> FluTubeEditor::headerBounds()   const { return { 0, 0, W, HDR_H }; }
juce::Rectangle<int> FluTubeEditor::urlBounds()      const { return { 0, HDR_H, W, URL_H }; }
juce::Rectangle<int> FluTubeEditor::waveformBounds() const { return { 0, HDR_H + URL_H, W, WAVE_H }; }
juce::Rectangle<int> FluTubeEditor::pitchBounds()    const { return { 0, HDR_H + URL_H + WAVE_H, W, PITCH_H }; }
juce::Rectangle<int> FluTubeEditor::effectsBounds()  const { return { 0, HDR_H + URL_H + WAVE_H + PITCH_H, W, FX_H }; }
juce::Rectangle<int> FluTubeEditor::footerBounds()   const { return { 0, H - FOOTER_H, W, FOOTER_H }; }

juce::Rectangle<int> FluTubeEditor::knobHitBounds(int k) const
{
    auto eff = effectsBounds();
    const auto& info = KNOB_INFO[k];
    int cx = eff.getX() + MARGIN + info.relX;
    int cy = eff.getY() + info.relY;
    int r  = 26;
    return { cx - r, cy - r, r * 2, r * 2 };
}

//==============================================================================
void FluTubeEditor::resized()
{
    int urlRowY = HDR_H + 14;
    // Layout right-to-left: [margin][FILE 62px][4px][LOAD 68px][6px][urlInput_][margin]
    int fileW = 62, loadW = 68;
    int fileX = W - MARGIN - fileW;
    int loadX = fileX - 4 - loadW;
    int inputW = loadX - MARGIN - 6;
    urlInput_  .setBounds(MARGIN, urlRowY, inputW, 30);
    loadButton_.setBounds(loadX,  urlRowY, loadW,  30);
    fileButton_.setBounds(fileX,  urlRowY, fileW,  30);
    statusLabel_.setBounds(MARGIN, urlRowY + 34, W - MARGIN * 2, 14);

    auto wb = waveformBounds();
    // Button strip at top of waveform section (22px strip)
    int stripY = wb.getY() + 3;
    holdButton_.setBounds(wb.getRight() - MARGIN - 88, stripY, 88, 16);
    // Waveform display below the button strip
    waveformDisplay_.setBounds(wb.getX() + MARGIN, wb.getY() + 22,
                               wb.getWidth() - MARGIN * 2, wb.getHeight() - 30);

    auto pb = pitchBounds();
    rootNoteCombo_.setBounds(pb.getX() + MARGIN + 100, pb.getY() + 20, 90, 28);
    // Sync button sits in the pitch section, clearly labelled alongside root note
    syncButton_.setBounds(pb.getX() + MARGIN + 210, pb.getY() + 20, 100, 28);

    auto eb = effectsBounds();
    bypassButton_.setBounds(W - MARGIN - 92, eb.getY() + 3, 92, 18);
}

//==============================================================================
void FluTubeEditor::paint(juce::Graphics& g)
{
    paintBackground(g);
    paintHeader(g);
    paintURLSection(g);
    paintWaveformLabel(g);
    paintPitchSection(g);
    paintEffects(g);
    paintFooter(g);
}

void FluTubeEditor::paintGrid(juce::Graphics& g)
{
    // Subtle horizontal grid lines — retro synthwave aesthetic
    g.setColour(colGrid.withAlpha(0.45f));
    for (int y = 0; y < H; y += 18)
        g.fillRect(0, y, W, 1);
}

void FluTubeEditor::paintBackground(juce::Graphics& g)
{
    // Radial gradient: slightly lighter purple in center, dark at edges
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff1a1030), (float)W * 0.5f, (float)H * 0.4f,
        colBg,                    (float)W * 0.5f, (float)H,
        true));
    g.fillAll();

    paintGrid(g);

    // Draw panel surfaces per section (slightly elevated)
    auto drawPanel = [&](juce::Rectangle<int> r)
    {
        g.setColour(colSurface.withAlpha(0.6f));
        g.fillRect(r);
        // Left accent stripe
        g.setColour(colBorderHi.withAlpha(0.08f));
        g.fillRect(r.getX(), r.getY(), 2, r.getHeight());
    };
    drawPanel(urlBounds());
    drawPanel(waveformBounds());
    drawPanel(pitchBounds());
    drawPanel(effectsBounds());

    // Header + footer slightly different
    g.setColour(colSurface2.withAlpha(0.7f));
    g.fillRect(headerBounds());
    g.setColour(colSurface2.withAlpha(0.5f));
    g.fillRect(footerBounds());

    // Section dividers — neon glow lines
    for (int y : { HDR_H, HDR_H + URL_H,
                   HDR_H + URL_H + WAVE_H,
                   HDR_H + URL_H + WAVE_H + PITCH_H,
                   H - FOOTER_H })
        drawDivider(g, y, colCyan);

    // Noise grain
    g.setOpacity(1.0f);
    g.drawImageAt(noiseTexture_, 0, 0);
}

void FluTubeEditor::paintHeader(juce::Graphics& g)
{
    // Top neon stripe — pink
    g.setColour(colPink);
    g.fillRect(0, 0, W, 2);
    g.setColour(colPink.withAlpha(0.25f));
    g.fillRect(0, 2, W, 2);

    drawLogo(g, (float)MARGIN + 26.0f, 50.0f, 26.0f);
    drawLogoText(g, (float)MARGIN + 58.0f, 10.0f);

    // VU meter area
    int vuW = 168, vuH = 10;
    int vuX = W - MARGIN - vuW;
    int vuY = 28;

    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText("OUTPUT", vuX - 46, vuY, 42, vuH, juce::Justification::centredRight, false);
    drawLEDMeter(g, { vuX, vuY, vuW, vuH }, vuSmoothed_);

    // Status box
    {
        auto st = processor_.getLoaderStatus();
        juce::String statusText;
        juce::Colour col = colTextDim;
        switch (st)
        {
            case YouTubeLoader::Status::Downloading: statusText = "DOWNLOADING"; col = colCyan;  break;
            case YouTubeLoader::Status::Done:        statusText = "LOADED";      col = colGreen; break;
            case YouTubeLoader::Status::Error:       statusText = "ERROR";       col = colRed;   break;
            default:                                 statusText = "READY";
        }

        int bx = vuX, by = vuY + vuH + 6, bw = vuW, bh = 20;
        // Panel
        g.setColour(colBg.withAlpha(0.8f));
        g.fillRect(bx, by, bw, bh);
        g.setColour(col.withAlpha(0.30f));
        g.drawRect(bx, by, bw, bh, 1);
        // Outer glow on border
        g.setColour(col.withAlpha(0.08f));
        g.drawRect(bx - 1, by - 1, bw + 2, bh + 2, 1);

        // LED dot
        g.setColour(col.withAlpha(0.20f));
        g.fillEllipse((float)(bx + 5), (float)(by + 5), 10.0f, 10.0f);
        g.setColour(col);
        g.fillEllipse((float)(bx + 7), (float)(by + 7), 6.0f, 6.0f);

        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions{}
            .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(9.5f)));
        g.drawText(statusText, bx + 20, by, bw - 24, bh, juce::Justification::centredLeft, false);
    }
}

void FluTubeEditor::paintURLSection(juce::Graphics& g)
{
    auto b = urlBounds();
    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText("URL", MARGIN, b.getY() + 4, 30, 10,
               juce::Justification::centredLeft, false);
}

void FluTubeEditor::paintWaveformLabel(juce::Graphics& g)
{
    auto b = waveformBounds();
    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText("SAMPLE REGION", MARGIN, b.getY() + 4, 120, 10,
               juce::Justification::centredLeft, false);

    g.setColour(colTextDim.withAlpha(0.4f));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(7.5f)));
    g.drawText("scroll to zoom / drag to pan",
               MARGIN + 124, b.getY() + 5, 200, 9,
               juce::Justification::centredLeft, false);
}

void FluTubeEditor::paintPitchSection(juce::Graphics& g)
{
    auto b = pitchBounds();
    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText("ROOT NOTE", b.getX() + MARGIN, b.getY() + 6, 80, 10,
               juce::Justification::centredLeft, false);
    g.drawText("TEMPO SYNC", b.getX() + MARGIN + 210, b.getY() + 6, 100, 10,
               juce::Justification::centredLeft, false);

    // Show sync info under the sync button
    {
        bool syncing = syncButton_.getToggleState();
        int    frames = processor_.getNumSampleFrames();
        double sr     = processor_.getActiveSampleRate();
        double projBPM = processor_.getCurrentBPM();

        if (frames > 0 && sr > 0.0 && projBPM > 1.0)
        {
            float  startN    = *processor_.apvts.getRawParameterValue("start_point");
            float  endN      = *processor_.apvts.getRawParameterValue("end_point");
            double regionSec = (double)(endN - startN) * (double)frames / sr;
            double rawBeats  = regionSec * projBPM / 60.0;

            const int candidates[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
            int N = std::max(1, (int)std::round(rawBeats));
            double bestDiff = std::abs((double)N - rawBeats);
            for (int c : candidates)
            {
                double diff = std::abs((double)c - rawBeats);
                if (diff < bestDiff) { bestDiff = diff; N = c; }
            }

            juce::Colour infoCol = syncing ? colGold : colTextDim.withAlpha(0.7f);
            g.setColour(infoCol);
            juce::String info;
            if (syncing)
                info = juce::String(N) + " beats @ " + juce::String(projBPM, 1) + " BPM  ("
                       + juce::String((double)processor_.syncTempoRatio.load(), 2) + "x)";
            else
                info = "region: " + juce::String(regionSec, 2) + "s  ~"
                       + juce::String(rawBeats, 1) + " beats";
            g.drawText(info, b.getX() + MARGIN + 210, b.getY() + 52, 280, 10,
                       juce::Justification::centredLeft, false);
        }
    }

    g.setColour(colTextDim.withAlpha(0.5f));
    g.drawText("MIDI pitch offset from root",
               b.getX() + MARGIN + 330, b.getY() + 26, 340, 16,
               juce::Justification::centredLeft, false);
}

void FluTubeEditor::paintEffects(juce::Graphics& g)
{
    auto b = effectsBounds();

    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText("LO-FI CHAIN", b.getX() + MARGIN, b.getY() + 5, 100, 10,
               juce::Justification::centredLeft, false);

    bool bypassed = bypassButton_.getToggleState();
    float alpha   = bypassed ? 0.28f : 1.0f;

    drawGroupBox(g, 14,  b.getY() + 22, 152, FX_H - 30, "TAPE DELAY", colCyan,                 alpha);
    drawGroupBox(g, 170, b.getY() + 22, 136, FX_H - 30, "VINYL",      colPink,                 alpha);
    drawGroupBox(g, 310, b.getY() + 22, 80,  FX_H - 30, "HUM",        colPurple,               alpha);
    drawGroupBox(g, 394, b.getY() + 22, 108, FX_H - 30, "BITCRUSH",   colCyan,                 alpha);
    drawGroupBox(g, 506, b.getY() + 22, 52,  FX_H - 30, "HAZY",       juce::Colour(0xffcc88ee), alpha);
    drawGroupBox(g, 562, b.getY() + 22, 52,  FX_H - 30, "WAVY",       juce::Colour(0xff22ddaa), alpha);
    drawGroupBox(g, 618, b.getY() + 22, 68,  FX_H - 30, "MASTER",     colGold,                 alpha);

    for (int k = 0; k < NUM_KNOBS; ++k)
        paintKnob(g, k, alpha);

    if (bypassed)
    {
        g.setColour(colBg.withAlpha(0.55f));
        g.fillRect(b.getX() + 14, b.getY() + 22, W - 28, FX_H - 30);

        g.setColour(colPink.withAlpha(0.55f));
        g.setFont(juce::Font(juce::FontOptions{}
            .withName(juce::Font::getDefaultMonospacedFontName())
            .withHeight(22.0f).withStyle("Bold")));
        g.drawText("[ BYPASSED ]", b, juce::Justification::centred, false);
    }
}

void FluTubeEditor::paintKnob(juce::Graphics& g, int k, float alpha)
{
    if (k == K_HAZY) { paintKnobHazy(g, k, alpha); return; }
    if (k == K_WAVY) { paintKnobWavy(g, k, alpha); return; }

    auto hitB  = knobHitBounds(k);
    float cx   = (float)hitB.getCentreX();
    float cy   = (float)hitB.getCentreY();
    float rimR = 21.0f;
    float faceR = 16.0f;
    float trackR = faceR - 1.0f;

    juce::Colour accent = juce::Colour(KNOB_INFO[k].accentArgb).withMultipliedAlpha(alpha);

    static constexpr float START_ANG = 2.356f;
    static constexpr float SWEEP     = 4.712f;

    // ── 1. Outer neon halo ────────────────────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        accent.withAlpha(0.10f * alpha), cx, cy,
        accent.withAlpha(0.0f),          cx + rimR + 10.0f, cy, true));
    g.fillEllipse(cx - rimR - 10.0f, cy - rimR - 10.0f,
                  (rimR + 10.0f) * 2.0f, (rimR + 10.0f) * 2.0f);

    // ── 2. Drop shadow ────────────────────────────────────────────────────────
    g.setColour(juce::Colour(0x60000000).withMultipliedAlpha(alpha));
    g.fillEllipse(cx - rimR + 2.5f, cy - rimR + 4.0f, rimR * 2.0f, rimR * 2.0f);

    // ── 3. Rim — dark body with bevel ─────────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff2e2048).withMultipliedAlpha(alpha), cx - rimR * 0.55f, cy - rimR * 0.65f,
        juce::Colour(0xff080610).withMultipliedAlpha(alpha), cx + rimR * 0.45f, cy + rimR * 0.55f,
        false));
    g.fillEllipse(cx - rimR, cy - rimR, rimR * 2.0f, rimR * 2.0f);

    // Top-left bright bevel edge
    {
        juce::Path rimHi;
        rimHi.addArc(cx - rimR + 0.5f, cy - rimR + 0.5f,
                     rimR * 2.0f - 1.0f, rimR * 2.0f - 1.0f,
                     3.9f, 6.4f, true);
        g.setColour(juce::Colours::white.withAlpha(0.35f * alpha));
        g.strokePath(rimHi, juce::PathStrokeType(1.5f));
    }
    // Bottom-right shadow edge
    {
        juce::Path rimSh;
        rimSh.addArc(cx - rimR + 0.5f, cy - rimR + 0.5f,
                     rimR * 2.0f - 1.0f, rimR * 2.0f - 1.0f,
                     0.7f, 3.5f, true);
        g.setColour(juce::Colour(0x65000000).withMultipliedAlpha(alpha));
        g.strokePath(rimSh, juce::PathStrokeType(2.0f));
    }

    // ── 4. Inner face ─────────────────────────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff14102a).withMultipliedAlpha(alpha), cx - faceR * 0.3f, cy - faceR * 0.5f,
        juce::Colour(0xff060410).withMultipliedAlpha(alpha), cx + faceR * 0.3f, cy + faceR * 0.4f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    g.setColour(accent.withAlpha(0.20f * alpha));
    g.drawEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

    // ── 5. Track arc ──────────────────────────────────────────────────────────
    {
        juce::Path track;
        track.addArc(cx - trackR, cy - trackR, trackR * 2.0f, trackR * 2.0f,
                     START_ANG, START_ANG + SWEEP, true);
        g.setColour(juce::Colour(0xff1a1238).withMultipliedAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(3.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colBorderHi.withAlpha(0.35f * alpha));
        g.strokePath(track, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── 6. Value arc with neon glow ───────────────────────────────────────────
    float endAng = START_ANG + knobValues_[k] * SWEEP;
    if (endAng > START_ANG + 0.02f)
    {
        juce::Path arc;
        arc.addArc(cx - trackR, cy - trackR, trackR * 2.0f, trackR * 2.0f,
                   START_ANG, endAng, true);

        // Wide neon glow
        g.setColour(accent.withAlpha(0.22f * alpha));
        g.strokePath(arc, juce::PathStrokeType(6.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(0.12f * alpha));
        g.strokePath(arc, juce::PathStrokeType(9.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Bright core
        g.setColour(accent.withMultipliedAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── 7. Specular highlight ─────────────────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(0.14f * alpha),
        cx - faceR * 0.3f, cy - faceR * 0.55f,
        juce::Colours::white.withAlpha(0.0f),
        cx + faceR * 0.35f, cy + faceR * 0.25f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    // ── 8. Indicator tick ─────────────────────────────────────────────────────
    {
        float innerR = trackR * 0.40f;
        float outerR = trackR * 0.82f;
        float inX  = cx + innerR * std::sin(endAng);
        float inY  = cy - innerR * std::cos(endAng);
        float outX = cx + outerR * std::sin(endAng);
        float outY = cy - outerR * std::cos(endAng);

        g.setColour(juce::Colours::white.withAlpha(0.10f * alpha));
        g.drawLine(inX, inY, outX, outY, 3.5f);
        g.setColour(juce::Colours::white.withAlpha(0.88f * alpha));
        g.drawLine(inX, inY, outX, outY, 1.5f);
    }

    // ── 9. Label ──────────────────────────────────────────────────────────────
    g.setColour(colTextDim.withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText(KNOB_LABELS[k],
               (int)(cx - 24.0f), (int)(cy + rimR + 5.0f), 48, 11,
               juce::Justification::centred, false);
}

// ── HAZY knob — foggy multi-layer blur aesthetic ──────────────────────────────
void FluTubeEditor::paintKnobHazy(juce::Graphics& g, int k, float alpha)
{
    auto hitB  = knobHitBounds(k);
    float cx   = (float)hitB.getCentreX();
    float cy   = (float)hitB.getCentreY();
    float rimR = 21.0f;
    float faceR = 16.0f;
    float trackR = faceR - 1.0f;

    juce::Colour accent = juce::Colour(KNOB_INFO[k].accentArgb).withMultipliedAlpha(alpha);
    static constexpr float START_ANG = 2.356f;
    static constexpr float SWEEP     = 4.712f;

    // 1. Foggy multi-layer halo — 6 spread rings at very low alpha
    for (int ring = 1; ring <= 6; ++ring)
    {
        float r = rimR + (float)ring * 3.5f;
        g.setGradientFill(juce::ColourGradient(
            accent.withAlpha(0.04f * alpha), cx, cy,
            accent.withAlpha(0.0f),          cx + r, cy, true));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    // 2. Drop shadow
    g.setColour(juce::Colour(0x50000000).withMultipliedAlpha(alpha));
    g.fillEllipse(cx - rimR + 2.5f, cy - rimR + 4.0f, rimR * 2.0f, rimR * 2.0f);

    // 3. Rim — slightly desaturated bevel
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff261c38).withMultipliedAlpha(alpha), cx - rimR * 0.55f, cy - rimR * 0.65f,
        juce::Colour(0xff060410).withMultipliedAlpha(alpha), cx + rimR * 0.45f, cy + rimR * 0.55f,
        false));
    g.fillEllipse(cx - rimR, cy - rimR, rimR * 2.0f, rimR * 2.0f);
    {
        juce::Path rimHi;
        rimHi.addArc(cx - rimR + 0.5f, cy - rimR + 0.5f,
                     rimR * 2.0f - 1.0f, rimR * 2.0f - 1.0f, 3.9f, 6.4f, true);
        g.setColour(juce::Colours::white.withAlpha(0.22f * alpha));
        g.strokePath(rimHi, juce::PathStrokeType(1.5f));
    }

    // 4. Inner face — cool frosted tint
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff1a1430).withMultipliedAlpha(alpha), cx - faceR * 0.3f, cy - faceR * 0.5f,
        juce::Colour(0xff060410).withMultipliedAlpha(alpha), cx + faceR * 0.3f, cy + faceR * 0.4f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    // Fog overlay on face
    g.setGradientFill(juce::ColourGradient(
        accent.withAlpha(0.10f * alpha), cx, cy - faceR * 0.3f,
        accent.withAlpha(0.0f),          cx, cy + faceR,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    // 5. Track arc
    {
        juce::Path track;
        track.addArc(cx - trackR, cy - trackR, trackR * 2.0f, trackR * 2.0f,
                     START_ANG, START_ANG + SWEEP, true);
        g.setColour(juce::Colour(0xff1a1238).withMultipliedAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // 6. Value arc — blurred: 5 concentric arcs at offset radii, very low alpha
    float endAng = START_ANG + knobValues_[k] * SWEEP;
    if (endAng > START_ANG + 0.02f)
    {
        for (int blur = -2; blur <= 2; ++blur)
        {
            float br = trackR + (float)blur * 1.8f;
            if (br < 4.0f) continue;
            juce::Path blurArc;
            blurArc.addArc(cx - br, cy - br, br * 2.0f, br * 2.0f,
                           START_ANG, endAng, true);
            float a = (blur == 0) ? 0.55f : (0.12f - std::abs(blur) * 0.03f);
            g.setColour(accent.withAlpha(a * alpha));
            g.strokePath(blurArc, juce::PathStrokeType(blur == 0 ? 2.5f : 4.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // 7. Specular
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(0.10f * alpha), cx - faceR * 0.3f, cy - faceR * 0.55f,
        juce::Colours::white.withAlpha(0.0f),           cx + faceR * 0.35f, cy + faceR * 0.25f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    // 8. Indicator tick — softened
    {
        float innerR = trackR * 0.40f;
        float outerR = trackR * 0.82f;
        float inX  = cx + innerR * std::sin(endAng);
        float inY  = cy - innerR * std::cos(endAng);
        float outX = cx + outerR * std::sin(endAng);
        float outY = cy - outerR * std::cos(endAng);
        g.setColour(accent.withAlpha(0.30f * alpha));
        g.drawLine(inX, inY, outX, outY, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.70f * alpha));
        g.drawLine(inX, inY, outX, outY, 1.5f);
    }

    // 9. Label
    g.setColour(juce::Colour(0xffcc88ee).withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText(KNOB_LABELS[k],
               (int)(cx - 24.0f), (int)(cy + rimR + 5.0f), 48, 11,
               juce::Justification::centred, false);
}

// ── WAVY knob — animated wavy arc aesthetic ───────────────────────────────────
void FluTubeEditor::paintKnobWavy(juce::Graphics& g, int k, float alpha)
{
    auto hitB  = knobHitBounds(k);
    float cx   = (float)hitB.getCentreX();
    float cy   = (float)hitB.getCentreY();
    float rimR = 21.0f;
    float faceR = 16.0f;
    float trackR = faceR - 1.0f;

    juce::Colour accent = juce::Colour(KNOB_INFO[k].accentArgb).withMultipliedAlpha(alpha);
    static constexpr float START_ANG = 2.356f;
    static constexpr float SWEEP     = 4.712f;

    float pulseScale = 1.0f + 0.12f * std::sin(wavyAnimPhase_);

    // 1. Animated pulsing halo
    {
        float haloR = (rimR + 10.0f) * pulseScale;
        g.setGradientFill(juce::ColourGradient(
            accent.withAlpha(0.12f * alpha), cx, cy,
            accent.withAlpha(0.0f),           cx + haloR, cy, true));
        g.fillEllipse(cx - haloR, cy - haloR, haloR * 2.0f, haloR * 2.0f);
    }

    // 2. Drop shadow
    g.setColour(juce::Colour(0x60000000).withMultipliedAlpha(alpha));
    g.fillEllipse(cx - rimR + 2.5f, cy - rimR + 4.0f, rimR * 2.0f, rimR * 2.0f);

    // 3. Rim
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff0a2828).withMultipliedAlpha(alpha), cx - rimR * 0.55f, cy - rimR * 0.65f,
        juce::Colour(0xff060410).withMultipliedAlpha(alpha), cx + rimR * 0.45f, cy + rimR * 0.55f,
        false));
    g.fillEllipse(cx - rimR, cy - rimR, rimR * 2.0f, rimR * 2.0f);
    {
        juce::Path rimHi;
        rimHi.addArc(cx - rimR + 0.5f, cy - rimR + 0.5f,
                     rimR * 2.0f - 1.0f, rimR * 2.0f - 1.0f, 3.9f, 6.4f, true);
        g.setColour(juce::Colours::white.withAlpha(0.28f * alpha));
        g.strokePath(rimHi, juce::PathStrokeType(1.5f));
    }

    // 4. Inner face — teal-tinted
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff081820).withMultipliedAlpha(alpha), cx - faceR * 0.3f, cy - faceR * 0.5f,
        juce::Colour(0xff040c10).withMultipliedAlpha(alpha), cx + faceR * 0.3f, cy + faceR * 0.4f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour(accent.withAlpha(0.18f * alpha));
    g.drawEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

    // 5. Track arc
    {
        juce::Path track;
        track.addArc(cx - trackR, cy - trackR, trackR * 2.0f, trackR * 2.0f,
                     START_ANG, START_ANG + SWEEP, true);
        g.setColour(juce::Colour(0xff091818).withMultipliedAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(0.25f * alpha));
        g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // 6. Value arc — wavy sine-displaced path
    float endAng = START_ANG + knobValues_[k] * SWEEP;
    if (endAng > START_ANG + 0.02f)
    {
        // Wide glow
        {
            juce::Path glowArc;
            glowArc.addArc(cx - trackR, cy - trackR, trackR * 2.0f, trackR * 2.0f,
                           START_ANG, endAng, true);
            g.setColour(accent.withAlpha(0.20f * alpha));
            g.strokePath(glowArc, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // Wavy core path: points displaced radially by a sine wave
        const int numSteps = 48;
        float waveAmp = 2.5f + 1.5f * knobValues_[k]; // bigger wave at high values
        float waveFreq = 6.0f;
        juce::Path wavyArc;
        for (int step = 0; step <= numSteps; ++step)
        {
            float t   = (float)step / (float)numSteps;
            float ang = START_ANG + t * (endAng - START_ANG);
            float disp = waveAmp * std::sin(waveFreq * t * 6.28318f + wavyAnimPhase_);
            float r    = trackR + disp;
            float px   = cx + r * std::sin(ang);
            float py   = cy - r * std::cos(ang);
            if (step == 0) wavyArc.startNewSubPath(px, py);
            else           wavyArc.lineTo(px, py);
        }
        g.setColour(accent.withMultipliedAlpha(alpha));
        g.strokePath(wavyArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    // 7. Specular
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(0.12f * alpha), cx - faceR * 0.3f, cy - faceR * 0.55f,
        juce::Colours::white.withAlpha(0.0f),           cx + faceR * 0.35f, cy + faceR * 0.25f,
        false));
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);

    // 8. Indicator tick
    {
        float innerR = trackR * 0.40f;
        float outerR = trackR * 0.82f;
        float inX  = cx + innerR * std::sin(endAng);
        float inY  = cy - innerR * std::cos(endAng);
        float outX = cx + outerR * std::sin(endAng);
        float outY = cy - outerR * std::cos(endAng);
        g.setColour(juce::Colours::white.withAlpha(0.10f * alpha));
        g.drawLine(inX, inY, outX, outY, 3.5f);
        g.setColour(juce::Colours::white.withAlpha(0.88f * alpha));
        g.drawLine(inX, inY, outX, outY, 1.5f);
    }

    // 9. Label
    g.setColour(juce::Colour(0xff22ddaa).withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(8.0f)));
    g.drawText(KNOB_LABELS[k],
               (int)(cx - 24.0f), (int)(cy + rimR + 5.0f), 48, 11,
               juce::Justification::centred, false);
}

void FluTubeEditor::paintFooter(juce::Graphics& g)
{
    auto b = footerBounds();

    // Bottom neon stripe
    g.setColour(colPurple.withAlpha(0.55f));
    g.fillRect(0, H - 1, W, 1);
    g.setColour(colPurple.withAlpha(0.15f));
    g.fillRect(0, H - 2, W, 1);

    g.setColour(colTextDim.withAlpha(0.5f));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(9.0f)));
    g.drawText("FLUTUBE  //  VOLTA LABS  //  MODEL FT-1  //  v1.0",
               b, juce::Justification::centred, false);
}

//==============================================================================
void FluTubeEditor::drawDivider(juce::Graphics& g, int y, juce::Colour col)
{
    // Neon glow divider
    g.setColour(col.withAlpha(0.06f));
    g.fillRect(0, y - 2, W, 1);
    g.setColour(col.withAlpha(0.15f));
    g.fillRect(0, y - 1, W, 1);
    g.setColour(col.withAlpha(0.35f));
    g.fillRect(0, y, W, 1);
    g.setColour(col.withAlpha(0.15f));
    g.fillRect(0, y + 1, W, 1);
    g.setColour(col.withAlpha(0.06f));
    g.fillRect(0, y + 2, W, 1);
}

void FluTubeEditor::drawLogoText(juce::Graphics& g, float x, float y)
{
    juce::Font boldFont(juce::FontOptions{}.withHeight(38.0f).withStyle("Bold"));
    juce::GlyphArrangement ga;
    ga.addLineOfText(boldFont, "FLUTUBE", x, y + 38.0f);

    juce::Path textPath;
    ga.createPath(textPath);
    auto tb = textPath.getBounds();

    // Outer glow passes (pink-purple)
    for (int pass = 3; pass >= 1; --pass)
    {
        g.saveState();
        float s = 1.0f + (float)pass * 0.016f;
        g.addTransform(juce::AffineTransform::scale(s, s, tb.getCentreX(), tb.getCentreY()));
        g.setColour(colPink.withAlpha(0.05f * (float)pass));
        g.fillPath(textPath);
        g.restoreState();
    }

    // Gradient fill: cyan left -> pink right
    g.setGradientFill(juce::ColourGradient(
        colCyan, tb.getX(),       tb.getCentreY(),
        colPink, tb.getRight(),   tb.getCentreY(),
        false));
    g.fillPath(textPath);

    // Specular white highlight at top
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::white.withAlpha(0.22f), x, y + 6.0f,
        juce::Colours::white.withAlpha(0.0f),  x, y + 24.0f,
        false));
    g.fillPath(textPath);

    // Subtitle
    g.setColour(colTextDim);
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName()).withHeight(9.0f)));
    g.drawText("VOLTA LABS  //  MODEL FT-1",
               (int)x, (int)(y + 50.0f), 340, 14,
               juce::Justification::centredLeft, false);

    // Thin accent line below subtitle
    g.setColour(colCyan.withAlpha(0.25f));
    g.fillRect((int)x, (int)(y + 67.0f), 280, 1);
}

void FluTubeEditor::drawLogo(juce::Graphics& g, float cx, float cy, float r)
{
    // ── Outer glow ring ───────────────────────────────────────────────────────
    g.setGradientFill(juce::ColourGradient(
        colPink.withAlpha(0.18f), cx, cy,
        colPink.withAlpha(0.0f),  cx + r + 12.0f, cy, true));
    g.fillEllipse(cx - r - 12.0f, cy - r - 12.0f, (r + 12.0f) * 2.0f, (r + 12.0f) * 2.0f);

    // ── Dark body ─────────────────────────────────────────────────────────────
    g.setColour(juce::Colour(0xff0a0615));
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    // ── Neon pink border ring ─────────────────────────────────────────────────
    g.setColour(colPink.withAlpha(0.85f));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);
    g.setColour(colPink.withAlpha(0.20f));
    g.drawEllipse(cx - r - 1.5f, cy - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f, 1.0f);

    // ── Play triangle (cyan → pink gradient) ─────────────────────────────────
    float triH = r * 0.72f;
    float triW = triH * 0.88f;
    float triX = cx - triW * 0.42f;   // slightly left of centre
    float triY = cy;

    juce::Path tri;
    tri.addTriangle(triX,          triY - triH * 0.5f,
                    triX + triW,   triY,
                    triX,          triY + triH * 0.5f);

    g.setGradientFill(juce::ColourGradient(
        colCyan, triX,          triY,
        colPink, triX + triW,   triY, false));
    g.fillPath(tri);

    // ── Sine wave arc across the bottom of the circle ────────────────────────
    {
        juce::Path wave;
        const int steps = 48;
        float waveY  = cy + r * 0.62f;
        float waveX0 = cx - r * 0.72f;
        float waveX1 = cx + r * 0.72f;
        float waveAmp = r * 0.18f;
        for (int i = 0; i <= steps; ++i)
        {
            float t = (float)i / (float)steps;
            float wx = waveX0 + t * (waveX1 - waveX0);
            float wy = waveY + std::sin(t * juce::MathConstants<float>::twoPi * 1.5f) * waveAmp;
            if (i == 0) wave.startNewSubPath(wx, wy);
            else        wave.lineTo(wx, wy);
        }
        g.setColour(colCyan.withAlpha(0.80f));
        g.strokePath(wave, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        // Glow pass
        g.setColour(colCyan.withAlpha(0.20f));
        g.strokePath(wave, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }
}

void FluTubeEditor::drawLEDMeter(juce::Graphics& g,
                                   juce::Rectangle<int> bounds,
                                   float level)
{
    constexpr int NUM_SEGS  = 18;
    constexpr int ORANGE_AT = 12;
    constexpr int RED_AT    = 15;

    float segW   = (float)bounds.getWidth() / (float)NUM_SEGS;
    float segGap = 1.5f;
    float litSegs = level * (float)NUM_SEGS;

    for (int i = 0; i < NUM_SEGS; ++i)
    {
        float sx = (float)bounds.getX() + (float)i * segW;
        float sy = (float)bounds.getY();
        float sw = segW - segGap;
        float sh = (float)bounds.getHeight();
        bool lit  = (float)i < litSegs;

        juce::Colour on, off;
        if (i < ORANGE_AT)
        {
            on  = colCyan;
            off = juce::Colour(0xff031822);
        }
        else if (i < RED_AT)
        {
            on  = colGold;
            off = juce::Colour(0xff1e1204);
        }
        else
        {
            on  = colPink;
            off = juce::Colour(0xff1e0410);
        }

        g.setColour(lit ? on : off.withAlpha(0.8f));
        g.fillRect(sx, sy, sw, sh);

        if (lit)
        {
            g.setColour(on.withAlpha(0.30f));
            g.fillRect(sx - 0.5f, sy - 0.5f, sw + 1.0f, sh + 1.0f);
        }
    }
}

void FluTubeEditor::drawGroupBox(juce::Graphics& g,
                                   int x, int y, int w, int h,
                                   const char* title,
                                   juce::Colour accentCol,
                                   float alpha)
{
    juce::Rectangle<float> r((float)x, (float)y, (float)w, (float)h);

    // Very subtle fill tint
    g.setColour(accentCol.withAlpha(0.04f * alpha));
    g.fillRect(r);

    // Outer neon glow (1px outside the border)
    g.setColour(accentCol.withAlpha(0.10f * alpha));
    g.drawRect(r.expanded(1.0f), 1.0f);

    // Main border
    g.setColour(accentCol.withAlpha(0.45f * alpha));
    g.drawRect(r, 1.0f);

    // Top accent bar (3px)
    g.setColour(accentCol.withAlpha(0.70f * alpha));
    g.fillRect((float)x, (float)y, (float)w, 3.0f);

    // Group title
    g.setColour(accentCol.withAlpha(0.85f * alpha));
    g.setFont(juce::Font(juce::FontOptions{}
        .withName(juce::Font::getDefaultMonospacedFontName())
        .withHeight(8.0f).withStyle("Bold")));
    g.drawText(title, x + 5, y + 5, w - 10, 11,
               juce::Justification::centredLeft, false);
}

//==============================================================================
void FluTubeEditor::mouseDown(const juce::MouseEvent& e)
{
    dragKnob_ = -1;
    for (int k = 0; k < NUM_KNOBS; ++k)
    {
        if (knobHitBounds(k).contains(e.x, e.y))
        {
            dragKnob_     = k;
            dragStartY_   = (float)e.y;
            dragStartVal_ = knobValues_[k];
            return;
        }
    }
}

void FluTubeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragKnob_ < 0) return;

    float delta  = (dragStartY_ - (float)e.y) / 150.0f;
    float newVal = juce::jlimit(0.0f, 1.0f, dragStartVal_ + delta);
    knobValues_[dragKnob_] = newVal;

    if (auto* param = processor_.apvts.getParameter(PARAM_IDS[dragKnob_]))
        param->setValueNotifyingHost(newVal);

    repaint(effectsBounds());
}

void FluTubeEditor::mouseUp(const juce::MouseEvent&)
{
    dragKnob_ = -1;
}

//==============================================================================
void FluTubeEditor::waveformStartChanged(float norm)
{
    if (auto* param = processor_.apvts.getParameter("start_point"))
        param->setValueNotifyingHost(norm);
}

void FluTubeEditor::waveformEndChanged(float norm)
{
    if (auto* param = processor_.apvts.getParameter("end_point"))
        param->setValueNotifyingHost(norm);
}

void FluTubeEditor::waveformPreviewAt(float norm)
{
    processor_.requestPreview(norm);
}

void FluTubeEditor::waveformPreviewStop()
{
    processor_.stopPreview();
}
