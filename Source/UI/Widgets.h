#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VocalisLookAndFeel.h"

class VocalisProcessor;

namespace vocalis
{

/** Etichetta di sezione maiuscola e tenue ("VOCI", "MISCELA"...). */
void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t);

/** Scelta a segmenti (Parlato / Rap / Canto) legata a un parametro a scelta. */
class SegmentedChoice : public juce::Component
{
public:
    SegmentedChoice (juce::RangedAudioParameter& p, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onChange;
private:
    juce::RangedAudioParameter& param;
    juce::StringArray items;
    std::unique_ptr<juce::ParameterAttachment> att;
    int current = 0;
};

/** Manopola con etichetta e colore, legata a un parametro. */
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& s, const char* id, juce::String name, juce::Colour c,
                  juce::String suffix = {}, int decimals = 0, bool big = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
private:
    juce::String name;
    juce::Colour colour;
    bool big;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

/** Combo con etichetta sopra, legata a un parametro a scelta. */
class LabelledCombo : public juce::Component
{
public:
    LabelledCombo (juce::AudioProcessorValueTreeState& s, const char* id, juce::String name);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox box;
private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

/** Pulsante a pillola (stile della sillaba, opzioni on/off). */
class Chip : public juce::ToggleButton
{
public:
    explicit Chip (juce::String t, juce::Colour c = Colours::warn) : juce::ToggleButton (std::move (t)), colour (c) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    juce::Colour colour;
};

/** Pulsante principale (sfondo chiaro) o secondario (bordo). */
class PillButton : public juce::TextButton
{
public:
    PillButton (juce::String t, bool primary = false) : juce::TextButton (std::move (t)), isPrimary (primary) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool isPrimary;
};

/** Pulsante rotondo di riproduzione (triangolo / quadrato). */
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("play") {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** Scheda di una voce (A/B/C/D): avatar, nome scelto con il menu, peso nella miscela. */
class VoiceCard : public juce::Component
{
public:
    VoiceCard (juce::AudioProcessorValueTreeState& s, const char* id, juce::String tag, juce::Colour c1, juce::Colour c2);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setWeight (float w) { if (std::abs (w - weight) > 0.004f) { weight = w; repaint(); } }
    juce::String voiceName() const;
    juce::ComboBox box;
private:
    juce::String tag;
    juce::Colour c1, c2;
    float weight = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

/** Sfera voce: pad XY che miscela le quattro voci agli angoli. */
class VoiceOrb : public juce::Component, public juce::SettableTooltipClient
{
public:
    VoiceOrb (juce::AudioProcessorValueTreeState& s);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setCornerNames (juce::StringArray n) { if (n != names) { names = n; repaint(); } }
private:
    void setFrom (juce::Point<float> p);
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::ParameterAttachment> ax, ay;
    float x = 0, y = 0;
    juce::StringArray names;
};

/** Indicatore di livello in uscita (picco, verde/ambra/rosso). */
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    explicit OutputMeter (VocalisProcessor& p);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    VocalisProcessor& proc;
    float shown = 0.0f, held = 0.0f;
};

/** Trascina il vocal (WAV con effetti) direttamente nella DAW. */
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
