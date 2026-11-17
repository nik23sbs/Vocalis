#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Render/Renderer.h"

class VocalisProcessor;

namespace vocalis
{

/** Waveform della clip con griglia di battute/quarti, sillabe e cursore di riproduzione.
    Trascinandola si porta il WAV direttamente nella DAW. */
class WaveformView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit WaveformView (VocalisProcessor& p);
    void setClip (RenderedClip::Ptr c);

    void paint (juce::Graphics&) override;
    void resized() override { rebuildPeaks(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

private:
    void timerCallback() override;
    void rebuildPeaks();

    VocalisProcessor& proc;
    RenderedClip::Ptr clip;
    std::vector<std::pair<float, float>> peaks;
    double lastPos = -2.0;
    bool dragStarted = false;
};

} // namespace vocalis
