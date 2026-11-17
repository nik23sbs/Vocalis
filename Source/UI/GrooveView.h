#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class VocalisProcessor;

namespace vocalis
{

/** Area dove trascinare (o scegliere) il sample da cui estrarre il groove. */
class GrooveView : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget
{
public:
    explicit GrooveView (VocalisProcessor& p);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void load (const juce::File& f);

    VocalisProcessor& proc;
    juce::TextButton clearButton { "x" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    bool hover = false;
};

} // namespace vocalis
