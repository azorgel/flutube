#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>

class WaveformDisplay : public juce::Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void waveformStartChanged(float normalisedPos) = 0;
        virtual void waveformEndChanged  (float normalisedPos) = 0;
        // Called when user clicks in the waveform body (not on a handle).
        virtual void waveformPreviewAt   (float normalisedPos) {}
        // Called when the mouse is released after a preview click.
        virtual void waveformPreviewStop () {}
    };

    WaveformDisplay();
    ~WaveformDisplay() override = default;

    void setWaveformData(const std::vector<float>& mono);
    void clearWaveform();

    void setListener(Listener* l) { listener_ = l; }

    // Thumbnail image shown semi-transparently behind the waveform (YouTube only).
    void setThumbnail(const juce::Image& img);
    void clearThumbnail();

    float getStartNorm() const noexcept { return startNorm_.load(); }
    float getEndNorm()   const noexcept { return endNorm_.load();   }

    void setStartNorm(float v);
    void setEndNorm  (float v);

    // Set playhead cursor position (0..1 = playing, < 0 = hidden).
    void setPlayheadNorm(float norm);

    void paint           (juce::Graphics&)             override;
    void resized         ()                            override;
    void mouseDown       (const juce::MouseEvent& e)   override;
    void mouseDrag       (const juce::MouseEvent& e)   override;
    void mouseUp         (const juce::MouseEvent& e)   override;
    void mouseWheelMove  (const juce::MouseEvent& e,
                          const juce::MouseWheelDetails& w) override;

private:
    void rebuildPeaks();
    int   normToX(float norm) const;
    float xToNorm(int x)      const;
    bool  nearHandle(int x, float handleNorm) const;

    // Zoom helpers
    void  applyZoom(float factor, float centerNorm);
    void  applyPan (float deltaNorm);
    void  resetZoom();

    // Zoom button hit rects (relative to component, top-right corner)
    juce::Rectangle<int> btnZoomIn()  const { return { getWidth() - 22,  2, 20, 14 }; }
    juce::Rectangle<int> btnZoomOut() const { return { getWidth() - 46,  2, 20, 14 }; }
    juce::Rectangle<int> btnFit()     const { return { getWidth() - 74,  2, 24, 14 }; }
    void drawZoomButton(juce::Graphics&, juce::Rectangle<int>, const char* label, bool active) const;

    std::vector<float> waveData_;
    std::vector<float> peaksPos_, peaksNeg_;
    juce::Image        thumbnail_;

    std::atomic<float> startNorm_ { 0.0f };
    std::atomic<float> endNorm_   { 1.0f };

    // View window — what portion of the waveform [0,1] is currently visible
    float viewStart_ = 0.0f;
    float viewEnd_   = 1.0f;

    enum class Drag { None, Start, End } dragging_ = Drag::None;
    bool      previewActive_ = false;  // true when a waveform-body preview was started
    Listener* listener_ = nullptr;

    float playheadNorm_ = -1.0f;  // < 0 = hidden

    static constexpr int HIT_PX = 12;

    // Vaporwave palette
    const juce::Colour colBg      { 0xff0d0818 };
    const juce::Colour colBorder  { 0xff3d1870 };
    const juce::Colour colBorderHi{ 0xff6630b0 };
    const juce::Colour colRegion  { 0x18ff2d87 };
    const juce::Colour colWave    { 0xffff2d87 };
    const juce::Colour colStart   { 0xff00e5ff };
    const juce::Colour colEnd     { 0xffffb800 };
    const juce::Colour colTextDim { 0xff6a3a8a };
    const juce::Colour colBtn     { 0xff2a1050 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
