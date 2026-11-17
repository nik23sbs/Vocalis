#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalis
{

namespace Colours
{
    const juce::Colour bg        { 0xff121318 };
    const juce::Colour panel     { 0xff1b1d24 };
    const juce::Colour panelEdge { 0xff2a2d38 };
    const juce::Colour text      { 0xffe6e7ec };
    const juce::Colour textDim   { 0xff8b8f9c };
    const juce::Colour accent    { 0xff7c5cff };   // viola
    const juce::Colour accent2   { 0xff22d3ee };   // ciano
    const juce::Colour warn      { 0xfff59e0b };
}

class VocalisLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VocalisLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f, juce::Font::bold)); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** Pannello con titolo (le sezioni dell'interfaccia). */
class Panel : public juce::Component
{
public:
    explicit Panel (juce::String t) : title (std::move (t)) {}
    void paint (juce::Graphics& g) override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (12).withTrimmedTop (22); }
private:
    juce::String title;
};

} // namespace vocalis
