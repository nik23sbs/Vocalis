#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Render/Renderer.h"

class VocalisProcessor;

namespace vocalis
{

/** Timeline dello "Studio": righello, corsia delle sillabe, piano roll con note, curva d'intonazione e forma d'onda.
      corsia:  trascina = sposta (1/32, Alt = libero), bordo destro = durata, Shift = intonazione,
               rotella = volume, doppio clic = muta, tasto destro = menu, clic = seleziona
      piano roll: trascina una nota in verticale = nota (Canto) o intonazione (semitoni)
      si puo' trascinare qui un file MIDI per la melodia. */
class Timeline : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Timeline (VocalisProcessor& p);
    void setClip (RenderedClip::Ptr c);
    void setSelected (int syllable) { if (selected != syllable) { selected = syllable; repaint(); } }
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override { rebuildPeaks(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::String getTooltip() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    static juce::String noteName (int midi);

    /** Solo test: centro del blocco della sillaba (corsia), centro della sua nota (piano roll), bordo destro. */
    juce::Point<float> blockCentreForTest (int i) const { return blockBounds (i).getCentre(); }
    juce::Point<float> blockRightEdgeForTest (int i) const { auto b = blockBounds (i); return { b.getRight() - 2.0f, b.getCentreY() }; }
    juce::Point<float> noteCentreForTest (int i) const;
    double beatsPerPixelForTest() const;
    juce::Point<float> emptyRollPointForTest() const { auto r = rollArea(); return { r.getRight() - 8.0f, r.getY() + 10.0f }; }

private:
    enum class Drag { None, Move, Resize, Pitch, Note, Vocal };

    void timerCallback() override;
    void rebuildPeaks();
    int markerAt (juce::Point<float> p, bool& onRightEdge) const;
    int noteAt (juce::Point<float> p) const;
    juce::Rectangle<float> blockBounds (int markerIndex) const;
    juce::Rectangle<float> rulerArea() const;
    juce::Rectangle<float> laneArea() const;
    juce::Rectangle<float> rollArea() const;
    float sampleToX (double s) const;
    float midiToY (float m) const;
    double samplesPerBeat() const;
    void updatePitchRange();
    void showMenu (int markerIndex);
    int displayNote (int markerIndex) const;

    VocalisProcessor& proc;
    RenderedClip::Ptr clip;
    std::vector<std::pair<float, float>> peaks;
    double lastPos = -2.0;
    float lowMidi = 48.0f, highMidi = 72.0f;

    int hover = -1, dragIndex = -1, selected = -1;
    juce::Point<float> lastMouse;
    bool lastAlt = false;
    Drag drag = Drag::None;
    SyllableEdit dragStartEdit;
    float dragDx = 0, dragDy = 0;
    juce::String dragInfo;
};

} // namespace vocalis
