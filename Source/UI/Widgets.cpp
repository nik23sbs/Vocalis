#include "Widgets.h"
#include "PluginProcessor.h"

namespace vocalis
{

void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    juce::String spaced;
    for (auto c : t.toUpperCase()) spaced << juce::String::charToString (c);
    g.drawText (spaced, r, juce::Justification::centredLeft, true);
}

// ============================================================================ SegmentedChoice

SegmentedChoice::SegmentedChoice (juce::RangedAudioParameter& p, juce::StringArray labels) : param (p), items (std::move (labels))
{
    att = std::make_unique<juce::ParameterAttachment> (param, [this] (float v) { current = juce::roundToInt (v); repaint(); if (onChange) onChange (current); });
    att->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1b1d26));
    g.fillRoundedRectangle (r, 10.0f);
    const float w = (r.getWidth() - 8.0f) / (float) juce::jmax (1, items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (4.0f + i * w, 4.0f, w, r.getHeight() - 8.0f);
        if (i == current)
        {
            g.setColour (Colours::accent);
            g.fillRoundedRectangle (cell, 7.0f);
        }
        g.setColour (i == current ? Colours::bg : juce::Colour (0xffb4b7c7));
        g.setFont (juce::FontOptions (13.0f, i == current ? juce::Font::bold : juce::Font::plain));
        g.drawText (items[i], cell, juce::Justification::centred);
    }
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    const float w = (getWidth() - 8.0f) / (float) juce::jmax (1, items.size());
    const int i = juce::jlimit (0, items.size() - 1, (int) ((e.position.x - 4.0f) / w));
    att->setValueAsCompleteGesture ((float) i);
}

// ============================================================================ LabelledKnob

LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& s, const char* id, juce::String n, juce::Colour c,
                            juce::String suffix, int decimals, bool b)
    : name (std::move (n)), colour (c), big (b)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2d3a));
    slider.setPopupDisplayEnabled (true, false, nullptr);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    slider.textFromValueFunction = [decimals, suffix] (double v) { return (decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v))) + suffix; };
    slider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getDoubleValue(); };
    if (auto* p = s.getParameter (id)) slider.setTooltip (p->getName (64));
    addAndMakeVisible (slider);
}

void LabelledKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (big ? 20 : 16);
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void LabelledKnob::paint (juce::Graphics& g)
{
    if (big)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);
    }
    g.setColour (big ? Colours::text : Colours::textDim);
    g.setFont (juce::FontOptions (big ? 12.0f : 10.5f, juce::Font::bold));
    g.drawText (name, getLocalBounds().removeFromBottom (big ? 22 : 16), juce::Justification::centredTop, true);
}

// ============================================================================ LabelledCombo

LabelledCombo::LabelledCombo (juce::AudioProcessorValueTreeState& s, const char* id, juce::String n) : name (std::move (n))
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    addAndMakeVisible (box);
}

void LabelledCombo::resized() { box.setBounds (getLocalBounds().withTrimmedTop (name.isEmpty() ? 0 : 16)); }

void LabelledCombo::paint (juce::Graphics& g)
{
    if (name.isEmpty()) return;
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (name, getLocalBounds().removeFromTop (15), juce::Justification::centredLeft, true);
}

// ============================================================================ Chip / PillButton

void Chip::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? colour.withAlpha (0.15f) : (over ? Colours::cardHi : juce::Colours::transparentBlack));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? colour : Colours::edge2.brighter (0.2f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (on ? colour.brighter (0.4f) : juce::Colour (0xffc8cad6));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

void PillButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    if (isPrimary || on)
    {
        g.setColour ((on ? Colours::accent : Colours::text).withMultipliedBrightness (down ? 0.85f : over ? 1.05f : 1.0f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Colours::bg);
    }
    else
    {
        g.setColour (over ? Colours::cardHi : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff343747));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (isEnabled() ? Colours::text : Colours::textDim);
    }
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

void PlayButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    g.setColour ((getToggleState() ? Colours::accent : Colours::text).withMultipliedBrightness (down ? 0.85f : over ? 1.08f : 1.0f));
    g.fillEllipse (r);
    g.setColour (Colours::bg);
    const auto c = r.getCentre();
    if (getToggleState())
        g.fillRect (juce::Rectangle<float> (d * 0.3f, d * 0.3f).withCentre (c));
    else
    {
        juce::Path t;
        t.addTriangle (c.x - d * 0.14f, c.y - d * 0.19f, c.x - d * 0.14f, c.y + d * 0.19f, c.x + d * 0.2f, c.y);
        g.fillPath (t);
    }
}

// ============================================================================ VoiceCard

VoiceCard::VoiceCard (juce::AudioProcessorValueTreeState& s, const char* id, juce::String t, juce::Colour a, juce::Colour b)
    : tag (std::move (t)), c1 (a), c2 (b)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    box.setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    box.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    box.onChange = [this] { repaint(); };
    addAndMakeVisible (box);
}

juce::String VoiceCard::voiceName() const
{
    auto t = box.getText();
    return t == "Nessuna" ? juce::String() : t.upToFirstOccurrenceOf (" (", false, false);
}

void VoiceCard::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    r.removeFromLeft (40);
    r.removeFromRight (40);
    box.setBounds (r);
}

void VoiceCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool active = weight > 0.01f && voiceName().isNotEmpty();
    g.setColour (active ? Colours::cardHi : Colours::card);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (active ? c1.interpolatedWith (c2, 0.5f).withAlpha (0.4f + 0.6f * weight) : juce::Colour (0xff262936));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    auto av = juce::Rectangle<float> (10.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f);
    if (voiceName().isNotEmpty())
        g.setGradientFill (juce::ColourGradient (c1, av.getX(), av.getY(), c2, av.getRight(), av.getBottom(), false));
    else
        g.setColour (juce::Colour (0xff2a2d3a));
    g.fillEllipse (av);
    g.setColour (Colours::bg.withAlpha (0.85f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (tag, av, juce::Justification::centred);

    g.setColour (active ? c1.brighter (0.2f) : Colours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (voiceName().isNotEmpty() ? juce::String (juce::roundToInt (weight * 100.0f)) + "%" : juce::String ("-"),
                getLocalBounds().removeFromRight (40).reduced (4, 0), juce::Justification::centredRight);
}

// ============================================================================ VoiceOrb

VoiceOrb::VoiceOrb (juce::AudioProcessorValueTreeState& s) : state (s)
{
    ax = std::make_unique<juce::ParameterAttachment> (*s.getParameter (IDs::orbX), [this] (float v) { x = v / 100.0f; repaint(); });
    ay = std::make_unique<juce::ParameterAttachment> (*s.getParameter (IDs::orbY), [this] (float v) { y = v / 100.0f; repaint(); });
    ax->sendInitialUpdate(); ay->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    names = { "A", "B", "C", "D" };
}

void VoiceOrb::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 12.0f);
    const juce::Colour cs[4] = { Colours::pink, Colours::accent2, Colours::warn, juce::Colour (0xff34d399) };
    const juce::Point<float> cp[4] = { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() };
    for (int i = 0; i < 4; ++i)
    {
        if (names[i].isEmpty()) continue;
        juce::ColourGradient grad (cs[i].withAlpha (0.38f), cp[i].x, cp[i].y, cs[i].withAlpha (0.0f),
                                   cp[i].x + (i % 2 ? -1 : 1) * r.getWidth() * 0.75f, cp[i].y, true);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 12.0f);
    }
    g.setColour (juce::Colour (0xff262936));
    g.drawRoundedRectangle (r, 12.0f, 1.0f);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto in = r.reduced (10.0f, 8.0f);
    const juce::Justification js[4] = { juce::Justification::topLeft, juce::Justification::topRight,
                                        juce::Justification::bottomLeft, juce::Justification::bottomRight };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (names[i].isEmpty() ? Colours::textDim.withAlpha (0.5f) : cs[i].brighter (0.3f));
        g.drawText (names[i].isEmpty() ? juce::String ("-") : names[i], in, js[i], true);
    }

    const float px = r.getX() + 14.0f + x * (r.getWidth() - 28.0f), py = r.getY() + 14.0f + y * (r.getHeight() - 28.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillEllipse (px - 17.0f, py - 17.0f, 34.0f, 34.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (px - 11.0f, py - 11.0f, 22.0f, 22.0f);
}

void VoiceOrb::setFrom (juce::Point<float> p)
{
    auto r = getLocalBounds().toFloat();
    const float nx = juce::jlimit (0.0f, 1.0f, (p.x - 14.0f) / (r.getWidth() - 28.0f));
    const float ny = juce::jlimit (0.0f, 1.0f, (p.y - 14.0f) / (r.getHeight() - 28.0f));
    ax->setValueAsPartOfGesture (nx * 100.0f);
    ay->setValueAsPartOfGesture (ny * 100.0f);
}

void VoiceOrb::mouseDown (const juce::MouseEvent& e) { ax->beginGesture(); ay->beginGesture(); setFrom (e.position); }
void VoiceOrb::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void VoiceOrb::mouseUp (const juce::MouseEvent&) { ax->endGesture(); ay->endGesture(); }

// ============================================================================ OutputMeter

OutputMeter::OutputMeter (VocalisProcessor& p) : proc (p) { startTimerHz (30); }

void OutputMeter::timerCallback()
{
    const float pk = proc.getOutputPeak();
    shown = juce::jmax (pk, shown * 0.88f);
    held = juce::jmax (pk, held * 0.995f);
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = r.removeFromTop (10.0f);
    g.setColour (juce::Colour (0xff2a2d3a));
    g.fillRoundedRectangle (bar, 5.0f);
    auto toX = [&] (float lin) { const float db = juce::Decibels::gainToDecibels (lin, -60.0f); return (db + 60.0f) / 60.0f; };
    const float f = juce::jlimit (0.0f, 1.0f, toX (shown));
    if (f > 0.0f)
    {
        juce::ColourGradient grad (juce::Colour (0xff22c55e), bar.getX(), 0, juce::Colour (0xffef4444), bar.getRight(), 0, false);
        grad.addColour (0.75, juce::Colour (0xff22c55e));
        grad.addColour (0.9, Colours::warn);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * f), 5.0f);
    }
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (11.5f));
    const float db = juce::Decibels::gainToDecibels (held, -60.0f);
    g.drawText (held > 0.0001f ? "picco " + juce::String (db, 1) + " dB" : juce::String ("silenzio"),
                r.withTrimmedTop (4.0f), juce::Justification::centredLeft);
}

// ============================================================================ DragOutHandle

DragOutHandle::DragOutHandle (VocalisProcessor& p) : proc (p)
{
    setTooltip ("Trascina nella DAW per importare il vocal (WAV con effetti)");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragOutHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::text);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::bg);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Trascina nella DAW", getLocalBounds(), juce::Justification::centred);
}

void DragOutHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 5) return;
    started = true;
    juce::String error;
    auto f = proc.createTempExport (error);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

} // namespace vocalis
