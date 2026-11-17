#include "VocalisLookAndFeel.h"

namespace vocalis
{

VocalisLookAndFeel::VocalisLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId, Colours::accent);
    setColour (juce::Slider::backgroundColourId, Colours::panelEdge);
    setColour (juce::Slider::thumbColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0f13));
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, Colours::accent2);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0e0f13));
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::TextButton::buttonColourId, Colours::panelEdge);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, Colours::text);
}

void VocalisLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = s.isEnabled();

    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::panelEdge);
    g.strokePath (bgArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valArc;
    valArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    juce::ColourGradient grad (Colours::accent, centre.x - radius, centre.y, Colours::accent2, centre.x + radius, centre.y, false);
    g.setGradientFill (grad);
    if (! enabled) g.setColour (Colours::textDim);
    g.strokePath (valArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - lineW * 1.2f;
    g.setColour (juce::Colour (0xff23262f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2, knobR * 2);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobR + 3.0f, 3.0f, knobR * 0.45f, 1.5f);
    g.setColour (enabled ? Colours::text : Colours::textDim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void VocalisLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour c = on ? Colours::accent : Colours::panelEdge;
    if (over) c = c.brighter (0.12f);
    if (down) c = c.darker (0.15f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
}

void VocalisLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = juce::jmin (16.0f, r.getHeight() - 4.0f);
    auto pill = r.removeFromLeft (h * 1.8f).withSizeKeepingCentre (h * 1.8f, h);
    const bool on = b.getToggleState();

    g.setColour (on ? Colours::accent : Colours::panelEdge.brighter (over ? 0.15f : 0.0f));
    g.fillRoundedRectangle (pill, h / 2);
    const float d = h - 4.0f;
    g.setColour (on ? juce::Colours::white : Colours::textDim);
    g.fillEllipse (on ? pill.getRight() - d - 2.0f : pill.getX() + 2.0f, pill.getY() + 2.0f, d, d);

    g.setColour (b.isEnabled() ? Colours::text : Colours::textDim);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

void VocalisLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Colours::accent : Colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (Colours::textDim);
    g.fillPath (arrow);
}

juce::Label* VocalisLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (11.5f));
    l->setColour (juce::Label::textColourId, Colours::textDim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    l->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0f13));
    return l;
}

void Panel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    g.setColour (Colours::accent2);
    g.fillRoundedRectangle (14.0f, 13.0f, 3.0f, 12.0f, 1.5f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (title.toUpperCase(), 22, 9, getWidth() - 30, 20, juce::Justification::centredLeft);
}

} // namespace vocalis
