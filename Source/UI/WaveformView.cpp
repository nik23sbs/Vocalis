#include "WaveformView.h"
#include "VocalisLookAndFeel.h"
#include "PluginProcessor.h"

namespace vocalis
{

WaveformView::WaveformView (VocalisProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Trascina la waveform nella DAW per importare il vocal");
    startTimerHz (30);
}

void WaveformView::setClip (RenderedClip::Ptr c)
{
    clip = c;
    rebuildPeaks();
    repaint();
}

void WaveformView::rebuildPeaks()
{
    peaks.clear();
    const int w = juce::jmax (1, getWidth());
    if (clip == nullptr || clip->audio.getNumSamples() == 0)
        return;

    const int n = clip->audio.getNumSamples();
    const float* d = clip->audio.getReadPointer (0);
    peaks.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
        float lo = 0, hi = 0;
        for (int i = a; i < juce::jmin (b, n); ++i) { lo = juce::jmin (lo, d[i]); hi = juce::jmax (hi, d[i]); }
        peaks[(size_t) x] = { lo, hi };
    }
}

void WaveformView::timerCallback()
{
    const double pos = proc.getPlayPosition();
    if (pos != lastPos)
    {
        lastPos = pos;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0c0d11));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    if (clip == nullptr || clip->audio.getNumSamples() == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isRendering() ? "Generazione in corso..." : "Scrivi un testo per generare il vocal",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float w = (float) getWidth(), h = (float) getHeight();
    const int n = clip->audio.getNumSamples();
    const double samplesPerBeat = 60.0 / clip->bpm * clip->sampleRate;
    const double totalBeats = n / samplesPerBeat;
    const float labelH = 16.0f;
    const float waveTop = labelH + 4.0f, waveH = h - waveTop - 6.0f, mid = waveTop + waveH / 2;

    // Griglia: battute e quarti
    for (int b = 0; b <= (int) totalBeats; ++b)
    {
        const float x = (float) (b / totalBeats * w);
        const bool bar = (b % 4) == 0;
        g.setColour (bar ? Colours::panelEdge.brighter (0.35f) : Colours::panelEdge);
        g.drawVerticalLine ((int) x, waveTop, h - 4);
        if (bar)
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (juce::String (b / 4 + 1), (int) x + 3, (int) (h - 16), 30, 12, juce::Justification::left);
        }
    }

    // Sillabe
    g.setFont (juce::FontOptions (11.0f));
    for (size_t i = 0; i < clip->markers.size(); ++i)
    {
        const auto& m = clip->markers[i];
        const float x0 = (float) m.start / n * w, x1 = (float) m.end / n * w;
        g.setColour ((i % 2 ? Colours::accent2 : Colours::accent).withAlpha (0.13f));
        g.fillRect (x0, waveTop, juce::jmax (1.0f, x1 - x0), waveH);
        g.setColour (Colours::text.withAlpha (0.85f));
        g.drawText (m.label, (int) x0, 2, juce::jmax (24, (int) (x1 - x0) + 20), (int) labelH, juce::Justification::left, false);
    }

    // Waveform
    juce::Path p;
    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float y = mid - peaks[x].second * waveH * 0.5f;
        if (x == 0) p.startNewSubPath (0, y); else p.lineTo ((float) x, y);
    }
    for (int x = (int) peaks.size() - 1; x >= 0; --x)
        p.lineTo ((float) x, mid - peaks[(size_t) x].first * waveH * 0.5f);
    p.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Colours::accent, 0, 0, Colours::accent2, w, 0, false));
    g.fillPath (p);

    // Cursore
    const double pos = proc.getPlayPosition();
    if (pos >= 0)
    {
        const float x = (float) (pos / n * w);
        g.setColour (juce::Colours::white);
        g.fillRect (x, 0.0f, 1.5f, h);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || clip == nullptr || e.getDistanceFromDragStart() < 6)
        return;

    dragStarted = true;
    juce::String error;
    auto f = proc.createTempExport (error);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

} // namespace vocalis
