#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalis
{

namespace Colours
{
    // tema "Studio" (opzione A)
    const juce::Colour bg        { 0xff0d0e13 };
    const juce::Colour side      { 0xff101118 };
    const juce::Colour header    { 0xff12131a };
    const juce::Colour panel     { 0xff151722 };
    const juce::Colour card      { 0xff171923 };
    const juce::Colour cardHi    { 0xff1d1f2a };
    const juce::Colour panelEdge { 0xff22242e };
    const juce::Colour edge2     { 0xff2c2f3d };
    const juce::Colour text      { 0xffe8e9ef };
    const juce::Colour textDim   { 0xff9a9db0 };
    const juce::Colour accent    { 0xffa78bfa };   // viola
    const juce::Colour accent2   { 0xff38bdf8 };   // ciano
    const juce::Colour pink      { 0xfff472b6 };
    const juce::Colour warn      { 0xfffbbf24 };   // ambra (modifiche)
    const juce::Colour ok        { 0xff8fd3a8 };
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
