#include "WaveformDisplay.h"
#include <algorithm>

WaveformDisplay::WaveformDisplay()
{
    setOpaque(true);
}

void WaveformDisplay::setThumbnail(const juce::Image& img)
{
    thumbnail_ = img;
    repaint();
}

void WaveformDisplay::clearThumbnail()
{
    thumbnail_ = {};
    repaint();
}

void WaveformDisplay::setWaveformData(const std::vector<float>& mono)
{
    waveData_ = mono;
    viewStart_ = 0.0f;
    viewEnd_   = 1.0f;
    rebuildPeaks();
    repaint();
}

void WaveformDisplay::clearWaveform()
{
    waveData_.clear();
    peaksPos_.clear();
    peaksNeg_.clear();
    viewStart_ = 0.0f;
    viewEnd_   = 1.0f;
    repaint();
}

void WaveformDisplay::setStartNorm(float v)
{
    startNorm_.store(juce::jlimit(0.0f, 1.0f, v));
    repaint();
}

void WaveformDisplay::setEndNorm(float v)
{
    endNorm_.store(juce::jlimit(0.0f, 1.0f, v));
    repaint();
}

void WaveformDisplay::setPlayheadNorm(float norm)
{
    if (std::abs(norm - playheadNorm_) > 0.0001f)
    {
        playheadNorm_ = norm;
        repaint();
    }
}


void WaveformDisplay::rebuildPeaks()
{
    int w = getWidth();
    if (w <= 0 || waveData_.empty())
    {
        peaksPos_.clear();
        peaksNeg_.clear();
        return;
    }

    peaksPos_.resize((size_t)w, 0.0f);
    peaksNeg_.resize((size_t)w, 0.0f);

    int numSamples  = (int)waveData_.size();
    float viewRange = viewEnd_ - viewStart_;

    for (int x = 0; x < w; ++x)
    {
        // Map pixel to the portion of the waveform visible in this view
        float normStart = viewStart_ + viewRange * ((float)x       / (float)w);
        float normEnd   = viewStart_ + viewRange * ((float)(x + 1) / (float)w);

        int s0 = (int)(normStart * (float)numSamples);
        int s1 = (int)(normEnd   * (float)numSamples);
        s0 = juce::jlimit(0, numSamples - 1, s0);
        s1 = juce::jlimit(s0 + 1, numSamples, s1);

        float maxV = 0.0f, minV = 0.0f;
        for (int s = s0; s < s1; ++s)
        {
            float v = waveData_[(size_t)s];
            maxV = std::max(maxV, v);
            minV = std::min(minV, v);
        }
        peaksPos_[(size_t)x] = maxV;
        peaksNeg_[(size_t)x] = minV;
    }
}

void WaveformDisplay::resized() { rebuildPeaks(); }

// ── Coordinate mapping (zoom-aware) ──────────────────────────────────────────

int WaveformDisplay::normToX(float norm) const
{
    float viewRange = viewEnd_ - viewStart_;
    if (viewRange <= 0.0f) return 0;
    float rel = (norm - viewStart_) / viewRange;
    return (int)(rel * (float)getWidth());
}

float WaveformDisplay::xToNorm(int x) const
{
    float viewRange = viewEnd_ - viewStart_;
    float rel = (float)x / (float)getWidth();
    return juce::jlimit(0.0f, 1.0f, viewStart_ + rel * viewRange);
}

bool WaveformDisplay::nearHandle(int x, float handleNorm) const
{
    return std::abs(x - normToX(handleNorm)) <= HIT_PX;
}

// ── Zoom / pan ────────────────────────────────────────────────────────────────

void WaveformDisplay::applyZoom(float factor, float centerNorm)
{
    float viewRange = viewEnd_ - viewStart_;
    float newRange  = juce::jlimit(0.005f, 1.0f, viewRange * factor);

    float newStart = centerNorm - newRange * ((centerNorm - viewStart_) / viewRange);
    float newEnd   = newStart + newRange;

    if (newStart < 0.0f) { newEnd -= newStart; newStart = 0.0f; }
    if (newEnd   > 1.0f) { newStart -= (newEnd - 1.0f); newEnd = 1.0f; }

    viewStart_ = juce::jlimit(0.0f, 1.0f, newStart);
    viewEnd_   = juce::jlimit(0.0f, 1.0f, newEnd);
    rebuildPeaks();
    repaint();
}

void WaveformDisplay::applyPan(float deltaNorm)
{
    float viewRange = viewEnd_ - viewStart_;
    float newStart  = juce::jlimit(0.0f, 1.0f - viewRange, viewStart_ + deltaNorm);
    viewStart_ = newStart;
    viewEnd_   = newStart + viewRange;
    rebuildPeaks();
    repaint();
}

void WaveformDisplay::resetZoom()
{
    viewStart_ = 0.0f;
    viewEnd_   = 1.0f;
    rebuildPeaks();
    repaint();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WaveformDisplay::drawZoomButton(juce::Graphics& g,
                                      juce::Rectangle<int> r,
                                      const char* label,
                                      bool active) const
{
    g.setColour(active ? colBorderHi.withAlpha(0.9f) : colBtn.withAlpha(0.85f));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
    g.setColour(active ? juce::Colours::white.withAlpha(0.9f)
                       : colBorderHi.withAlpha(0.8f));
    g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
    g.setColour(active ? juce::Colours::white : colBorderHi);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("Bold")));
    g.drawText(label, r, juce::Justification::centred, false);
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    auto  b    = getLocalBounds();
    float w    = (float)b.getWidth();
    float h    = (float)b.getHeight();
    float midY = h * 0.5f;
    float halfH = h * 0.44f;

    // Background
    g.fillAll(colBg);

    // YouTube thumbnail — semi-transparent full fill behind the waveform
    if (thumbnail_.isValid())
    {
        g.setOpacity(0.18f);
        g.drawImage(thumbnail_,
                    0, 0, (int)w, (int)h,
                    0, 0, thumbnail_.getWidth(), thumbnail_.getHeight());
        g.setOpacity(1.0f);
    }

    // Inset edge shadow
    g.setColour(juce::Colour(0x20000000));
    g.fillRect(0, 0, (int)w, 2);
    g.fillRect(0, 0, 2, (int)h);

    // ── Zoom indicator bar at top (shows view window position) ────────────────
    if (viewStart_ > 0.0f || viewEnd_ < 1.0f)
    {
        // Full track strip
        g.setColour(colBtn);
        g.fillRect(0, 0, (int)w, 4);
        // Highlighted portion = current view
        g.setColour(colBorderHi.withAlpha(0.7f));
        g.fillRect((int)(viewStart_ * w), 0,
                   (int)((viewEnd_ - viewStart_) * w), 4);
    }

    int sx = normToX(startNorm_.load());
    int ex = normToX(endNorm_.load());

    // Region tint — only draw within visible bounds
    {
        int drawSx = juce::jlimit(0, (int)w, sx);
        int drawEx = juce::jlimit(0, (int)w, ex);
        if (drawEx > drawSx)
        {
            g.setColour(colRegion);
            g.fillRect(drawSx, 0, drawEx - drawSx, (int)h);
        }
    }

    if (!peaksPos_.empty())
    {
        int nCols = std::min((int)peaksPos_.size(), (int)w);

        // Upper fill
        juce::Path upperPath;
        upperPath.startNewSubPath(0.0f, midY);
        for (int x = 0; x < nCols; ++x)
            upperPath.lineTo((float)x, midY - peaksPos_[(size_t)x] * halfH);
        upperPath.lineTo((float)(nCols - 1), midY);
        upperPath.closeSubPath();

        g.setGradientFill(juce::ColourGradient(
            colWave.withAlpha(0.45f), 0.0f, 0.0f,
            colWave.withAlpha(0.04f), 0.0f, midY, false));
        g.fillPath(upperPath);

        // Lower fill
        juce::Path lowerPath;
        lowerPath.startNewSubPath(0.0f, midY);
        for (int x = 0; x < nCols; ++x)
            lowerPath.lineTo((float)x, midY - peaksNeg_[(size_t)x] * halfH);
        lowerPath.lineTo((float)(nCols - 1), midY);
        lowerPath.closeSubPath();

        g.setGradientFill(juce::ColourGradient(
            colWave.withAlpha(0.45f), 0.0f, h,
            colWave.withAlpha(0.04f), 0.0f, midY, false));
        g.fillPath(lowerPath);

        // Contour strokes
        {
            juce::Path uContour;
            uContour.startNewSubPath(0.0f, midY - peaksPos_[0] * halfH);
            for (int x = 1; x < nCols; ++x)
                uContour.lineTo((float)x, midY - peaksPos_[(size_t)x] * halfH);

            juce::Path lContour;
            lContour.startNewSubPath(0.0f, midY - peaksNeg_[0] * halfH);
            for (int x = 1; x < nCols; ++x)
                lContour.lineTo((float)x, midY - peaksNeg_[(size_t)x] * halfH);

            g.setColour(colWave.withAlpha(0.65f));
            g.strokePath(uContour, juce::PathStrokeType(1.0f));
            g.strokePath(lContour, juce::PathStrokeType(1.0f));
        }

        // Centre line
        g.setColour(colBorderHi.withAlpha(0.20f));
        g.drawHorizontalLine((int)midY, 0.0f, w);
    }
    else
    {
        g.setColour(colTextDim);
        g.setFont(juce::Font(juce::FontOptions{}
            .withName(juce::Font::getDefaultSansSerifFontName()).withHeight(11.0f)));
        g.drawText("NO SAMPLE LOADED", b, juce::Justification::centred, false);
    }

    // ── Start handle (cyan) ───────────────────────────────────────────────────
    if (sx >= 0 && sx <= (int)w)
    {
        g.setColour(colStart.withAlpha(0.15f));
        g.drawVerticalLine(sx - 1, 0.0f, h);
        g.drawVerticalLine(sx + 1, 0.0f, h);
        g.setColour(colStart.withAlpha(0.80f));
        g.drawVerticalLine(sx, 0.0f, h);

        float dx = 6.5f;
        juce::Path diamond;
        diamond.startNewSubPath((float)sx,        4.0f);
        diamond.lineTo          ((float)sx + dx,  4.0f + dx);
        diamond.lineTo          ((float)sx,        4.0f + dx * 2.0f);
        diamond.lineTo          ((float)sx - dx,  4.0f + dx);
        diamond.closeSubPath();

        g.setColour(juce::Colour(0x50000000));
        g.fillPath(diamond, juce::AffineTransform::translation(1.5f, 2.0f));
        g.setColour(colStart.withAlpha(0.85f));
        g.fillPath(diamond);
        g.setColour(juce::Colours::white.withAlpha(0.30f));
        g.strokePath(diamond, juce::PathStrokeType(0.8f));
    }

    // ── End handle (gold) ─────────────────────────────────────────────────────
    if (ex >= 0 && ex <= (int)w)
    {
        g.setColour(colEnd.withAlpha(0.15f));
        g.drawVerticalLine(ex - 1, 0.0f, h);
        g.drawVerticalLine(ex + 1, 0.0f, h);
        g.setColour(colEnd.withAlpha(0.80f));
        g.drawVerticalLine(ex, 0.0f, h);

        float dx = 6.5f;
        juce::Path diamond;
        diamond.startNewSubPath((float)ex,        4.0f);
        diamond.lineTo          ((float)ex + dx,  4.0f + dx);
        diamond.lineTo          ((float)ex,        4.0f + dx * 2.0f);
        diamond.lineTo          ((float)ex - dx,  4.0f + dx);
        diamond.closeSubPath();

        g.setColour(juce::Colour(0x50000000));
        g.fillPath(diamond, juce::AffineTransform::translation(1.5f, 2.0f));
        g.setColour(colEnd.withAlpha(0.85f));
        g.fillPath(diamond);
        g.setColour(juce::Colours::white.withAlpha(0.30f));
        g.strokePath(diamond, juce::PathStrokeType(0.8f));
    }

    // ── Playhead cursor ───────────────────────────────────────────────────────
    if (playheadNorm_ >= 0.0f)
    {
        int px = normToX(playheadNorm_);
        if (px >= 0 && px <= (int)w)
        {
            // Soft glow lines
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.drawVerticalLine(px - 1, 0.0f, h);
            g.drawVerticalLine(px + 1, 0.0f, h);
            // Main cursor line
            g.setColour(juce::Colours::white.withAlpha(0.80f));
            g.drawVerticalLine(px, 0.0f, h);
            // Small downward triangle at top
            juce::Path tri;
            tri.addTriangle((float)px - 5.0f, 0.0f,
                            (float)px + 5.0f, 0.0f,
                            (float)px,         8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.90f));
            g.fillPath(tri);
        }
    }

    // ── Outer border ──────────────────────────────────────────────────────────
    g.setColour(colBorder.withAlpha(0.7f));
    g.drawRect(b);

    // ── Zoom buttons (top-right corner overlay) ───────────────────────────────
    bool zoomed = (viewStart_ > 0.001f || viewEnd_ < 0.999f);
    drawZoomButton(g, btnZoomIn(),  "+",   false);
    drawZoomButton(g, btnZoomOut(), "-",   false);
    drawZoomButton(g, btnFit(),     "FIT", zoomed);
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    // Check zoom buttons first
    if (btnZoomIn().contains(e.x, e.y))
    {
        float center = viewStart_ + (viewEnd_ - viewStart_) * 0.5f;
        applyZoom(0.5f, center);
        return;
    }
    if (btnZoomOut().contains(e.x, e.y))
    {
        float center = viewStart_ + (viewEnd_ - viewStart_) * 0.5f;
        applyZoom(2.0f, center);
        return;
    }
    if (btnFit().contains(e.x, e.y))
    {
        resetZoom();
        return;
    }

    // Handle drag detection
    if (nearHandle(e.x, startNorm_.load()))
        dragging_ = Drag::Start;
    else if (nearHandle(e.x, endNorm_.load()))
        dragging_ = Drag::End;
    else
    {
        dragging_ = Drag::None;
        // Click in the waveform body → preview from this position
        if (!waveData_.empty() && listener_)
        {
            previewActive_ = true;
            listener_->waveformPreviewAt(xToNorm(e.x));
        }
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ == Drag::None) return;

    float norm = xToNorm(e.x);
    if (dragging_ == Drag::Start)
    {
        norm = std::min(norm, endNorm_.load() - 0.005f);
        norm = std::max(norm, 0.0f);
        startNorm_.store(norm);
        if (listener_) listener_->waveformStartChanged(norm);
    }
    else
    {
        norm = std::max(norm, startNorm_.load() + 0.005f);
        norm = std::min(norm, 1.0f);
        endNorm_.store(norm);
        if (listener_) listener_->waveformEndChanged(norm);
    }
    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent&)
{
    if (previewActive_ && listener_)
        listener_->waveformPreviewStop();
    previewActive_ = false;
    dragging_ = Drag::None;
}

void WaveformDisplay::mouseWheelMove(const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& w)
{
    if (!waveData_.empty())
    {
        float cursorNorm = xToNorm(e.x);

        if (std::abs(w.deltaY) > std::abs(w.deltaX))
        {
            // Vertical scroll = zoom in/out at cursor position
            float factor = (w.deltaY > 0) ? 0.65f : 1.55f;
            applyZoom(factor, cursorNorm);
        }
        else if (std::abs(w.deltaX) > 0.0f)
        {
            // Horizontal scroll = pan
            float viewRange = viewEnd_ - viewStart_;
            applyPan(w.deltaX * viewRange * 0.4f);
        }
    }
}
