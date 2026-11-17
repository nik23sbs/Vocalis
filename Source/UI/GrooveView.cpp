#include "GrooveView.h"
#include "VocalisLookAndFeel.h"
#include "PluginProcessor.h"

namespace vocalis
{

GrooveView::GrooveView (VocalisProcessor& p) : proc (p)
{
    addAndMakeVisible (clearButton);
    clearButton.setTooltip ("Rimuovi il groove");
    clearButton.onClick = [this] { proc.clearGroove(); message.clear(); repaint(); };
    setTooltip ("Trascina qui un loop audio (o clicca per sceglierlo): il vocal seguira' il suo groove");
}

void GrooveView::resized()
{
    clearButton.setBounds (getWidth() - 24, 4, 20, 20);
}

bool GrooveView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void GrooveView::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    if (! files.isEmpty())
        load (juce::File (files[0]));
}

void GrooveView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    chooser = std::make_unique<juce::FileChooser> ("Scegli un loop per il groove", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  load (fc.getResult());
                          });
}

void GrooveView::load (const juce::File& f)
{
    juce::String err;
    if (! proc.loadGrooveFile (f, err))
        message = err;
    else
        message = err; // eventuale avviso
    repaint();
}

void GrooveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff0c0d11));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (hover ? Colours::accent2 : Colours::panelEdge);
    const float dashes[] = { 5.0f, 4.0f };
    juce::Path border;
    border.addRoundedRectangle (r, 6.0f);
    juce::PathStrokeType (hover ? 2.0f : 1.0f).createDashedStroke (border, border, dashes, 2);
    g.fillPath (border);

    const auto& gr = proc.getGroove();
    clearButton.setVisible (! gr.isEmpty());

    if (gr.isEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (message.isNotEmpty() ? message : "Trascina qui un loop audio\nper estrarre il groove",
                          getLocalBounds().reduced (8), juce::Justification::centred, 3);
        return;
    }

    auto area = getLocalBounds().reduced (8).withTrimmedTop (18).toFloat();
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (gr.sourceName + "   " + juce::String (gr.lengthBeats / 4.0, 1) + " batt.   "
                    + juce::String (gr.onsetBeats.size()) + " colpi",
                8, 4, getWidth() - 40, 16, juce::Justification::left);

    const double L = gr.lengthBeats;
    // griglia dei sedicesimi
    for (int s = 0; s <= (int) (L * 4); ++s)
    {
        const float x = area.getX() + (float) (s / (L * 4)) * area.getWidth();
        g.setColour (s % 4 == 0 ? Colours::panelEdge.brighter (0.3f) : Colours::panelEdge);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }
    // colpi
    for (size_t i = 0; i < gr.onsetBeats.size(); ++i)
    {
        const float x = area.getX() + (float) (gr.onsetBeats[i] / L) * area.getWidth();
        const float hh = area.getHeight() * (0.25f + 0.75f * gr.onsetStrength[i]);
        g.setColour (Colours::accent2);
        g.fillRoundedRectangle (x - 1.5f, area.getBottom() - hh, 3.0f, hh, 1.5f);
    }
}

} // namespace vocalis
