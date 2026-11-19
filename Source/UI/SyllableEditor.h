#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Render/Renderer.h"

class VocalisProcessor;

namespace vocalis
{

/** Editor delle sillabe stile "piano roll":
      - trascina una sillaba per spostarla (aggancio a 1/32, Alt = libero)
      - trascina il bordo destro per allungarla/accorciarla
      - Shift + trascina in verticale per cambiarne l'intonazione (semitoni)
      - rotella = volume della sillaba; doppio clic = muta/riattiva; tasto destro = menu
    Sotto i blocchi c'e' la waveform con la griglia delle battute e il cursore di riproduzione. */
class SyllableEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit SyllableEditor (VocalisProcessor& p);
    void setClip (RenderedClip::Ptr c);

    void paint (juce::Graphics&) override;
    void resized() override { rebuildPeaks(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    enum class Drag { None, Move, Resize, Pitch };

    void timerCallback() override;
    void rebuildPeaks();
    int markerAt (juce::Point<float> p, bool& onRightEdge) const;
    juce::Rectangle<float> blockBounds (int markerIndex) const;
    juce::Rectangle<float> laneArea() const;
    juce::Rectangle<float> waveArea() const;
    float sampleToX (double s) const;
    double samplesPerBeat() const;
    void showMenu (int markerIndex);

    VocalisProcessor& proc;
    RenderedClip::Ptr clip;
    std::vector<std::pair<float, float>> peaks;
    double lastPos = -2.0;

    int hover = -1, dragIndex = -1;
    Drag drag = Drag::None;
    SyllableEdit dragStartEdit;
    float dragDx = 0, dragDy = 0;
    juce::String dragInfo;
};

/** Maniglia per trascinare il vocal (WAV con effetti) direttamente nella DAW. */
class DragOutHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragOutHandle (VocalisProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    VocalisProcessor& proc;
    bool started = false;
};

} // namespace vocalis
