#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformDisplay.h"

class FluTubeEditor : public juce::AudioProcessorEditor,
                      private juce::Timer,
                      public WaveformDisplay::Listener
{
public:
    explicit FluTubeEditor(FluTubeProcessor&);
    ~FluTubeEditor() override;

    void paint    (juce::Graphics&)           override;
    void resized  ()                          override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;

    void waveformStartChanged(float norm) override;
    void waveformEndChanged  (float norm) override;
    void waveformPreviewAt   (float norm) override;
    void waveformPreviewStop ()           override;

private:
    void timerCallback() override;

    // ── Paint helpers ─────────────────────────────────────────────────────────
    void paintBackground   (juce::Graphics&);
    void paintHeader       (juce::Graphics&);
    void paintURLSection   (juce::Graphics&);
    void paintWaveformLabel(juce::Graphics&);
    void paintPitchSection (juce::Graphics&);
    void paintEffects      (juce::Graphics&);
    void paintFooter       (juce::Graphics&);
    void paintGrid         (juce::Graphics&);
    void paintKnob         (juce::Graphics&, int knobIdx, float alpha = 1.0f);
    void paintKnobHazy     (juce::Graphics&, int knobIdx, float alpha);
    void paintKnobWavy     (juce::Graphics&, int knobIdx, float alpha);

    // Decorative helpers
    void drawDivider  (juce::Graphics&, int y, juce::Colour col);
    void drawLogoText (juce::Graphics&, float x, float y);
    void drawLogo     (juce::Graphics&, float cx, float cy, float r);
    void drawLEDMeter (juce::Graphics&, juce::Rectangle<int> bounds, float level);
    void drawGroupBox (juce::Graphics&, int x, int y, int w, int h,
                       const char* title, juce::Colour accentCol, float alpha = 1.0f);

    // Layout
    juce::Rectangle<int> headerBounds()   const;
    juce::Rectangle<int> urlBounds()      const;
    juce::Rectangle<int> waveformBounds() const;
    juce::Rectangle<int> pitchBounds()    const;
    juce::Rectangle<int> effectsBounds()  const;
    juce::Rectangle<int> footerBounds()   const;
    juce::Rectangle<int> knobHitBounds(int k) const;

    FluTubeProcessor& processor_;

    // ── JUCE components ──────────────────────────────────────────────────────
    juce::TextEditor urlInput_;
    juce::TextButton loadButton_   { "LOAD" };
    juce::TextButton fileButton_   { "FILE" };
    juce::Label      statusLabel_;
    WaveformDisplay  waveformDisplay_;
    juce::ComboBox   rootNoteCombo_;
    juce::TextButton bypassButton_ { "FX ACTIVE" };
    juce::TextButton holdButton_   { "HOLD" };
    juce::TextButton syncButton_   { "SYNC BPM" };
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment_;

    // ── Knob state ───────────────────────────────────────────────────────────
    enum : int {
        K_TAPE_MIX = 0, K_TAPE_TIME, K_TAPE_FDBK, K_TAPE_WBL,
        K_VINYL_NOISE, K_VINYL_CRK,
        K_HUM,
        K_BITS, K_RATE,
        K_HAZY, K_WAVY,
        K_MASTER,
        NUM_KNOBS
    };

    static const char* PARAM_IDS[NUM_KNOBS];
    static const char* KNOB_LABELS[NUM_KNOBS];

    struct KnobInfo { int relX, relY; uint32_t accentArgb; };
    static const KnobInfo KNOB_INFO[NUM_KNOBS];

    float knobValues_[NUM_KNOBS] = {};
    int   dragKnob_     = -1;
    float dragStartY_   = 0.0f;
    float dragStartVal_ = 0.0f;

    // ── Animation ────────────────────────────────────────────────────────────
    float vuSmoothed_    = 0.0f;
    float wavyAnimPhase_ = 0.0f;   // advances each timer tick for WAVY knob animation

    // ── Background noise texture ──────────────────────────────────────────────
    juce::Image noiseTexture_;

    // ── Layout constants ─────────────────────────────────────────────────────
    static constexpr int W         = 700;
    static constexpr int H         = 660;
    static constexpr int HDR_H     = 100;
    static constexpr int URL_H     = 64;
    static constexpr int WAVE_H    = 184;
    static constexpr int PITCH_H   = 68;
    static constexpr int FX_H      = 216;
    static constexpr int FOOTER_H  = 28;
    static constexpr int MARGIN    = 14;

    static_assert(HDR_H + URL_H + WAVE_H + PITCH_H + FX_H + FOOTER_H == H,
                  "Layout height mismatch");

    // ── Vaporwave / Synthwave palette ─────────────────────────────────────────
    const juce::Colour colBg       { 0xff0d0818 };  // deep space purple
    const juce::Colour colSurface  { 0xff160e28 };  // raised panel
    const juce::Colour colSurface2 { 0xff1e1238 };  // elevated surface
    const juce::Colour colGrid     { 0xff2a1050 };  // grid lines
    const juce::Colour colBorder   { 0xff3d1870 };  // border
    const juce::Colour colBorderHi { 0xff6630b0 };  // bright border
    const juce::Colour colPink     { 0xffff2d87 };  // hot neon pink
    const juce::Colour colCyan     { 0xff00e5ff };  // electric cyan
    const juce::Colour colPurple   { 0xffb826ff };  // electric purple
    const juce::Colour colGold     { 0xffffb800 };  // warm gold
    const juce::Colour colText     { 0xffcc99ee };  // soft lavender
    const juce::Colour colTextDim  { 0xff6a3a8a };  // dim purple
    const juce::Colour colRed      { 0xffff1a4a };  // hot red
    const juce::Colour colGreen    { 0xff44ffcc };  // mint green

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluTubeEditor)
};
