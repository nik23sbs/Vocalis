#pragma once
#include "PluginProcessor.h"
#include "UI/VocalisLookAndFeel.h"
#include "UI/SyllableEditor.h"
#include "UI/GrooveView.h"

class VocalisEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit VocalisEditor (VocalisProcessor&);
    ~VocalisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    struct Combo
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };

    struct FxRow
    {
        juce::ToggleButton toggle;
        std::unique_ptr<ButtonAtt> att;
        juce::Array<Knob*> knobs;
    };

    Knob* makeKnob (const char* paramId, const juce::String& label, const juce::String& suffix = {}, int decimals = 0);
    Combo* makeCombo (const char* paramId, const juce::String& label);
    juce::ToggleButton* makeToggle (const char* paramId, const juce::String& label);
    FxRow* makeFx (const char* onId, const juce::String& name, std::initializer_list<Knob*> knobs);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void setEngine (int e);
    void updateEngineUI();
    void showDictionary();
    void layoutKnobs (juce::Rectangle<int> area, juce::Array<Knob*> knobs, int cols, int rowH);
    void exportWav();

    VocalisProcessor& proc;
    vocalis::VocalisLookAndFeel lnf;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Combo> combos;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<ButtonAtt> toggleAtts;
    juce::OwnedArray<FxRow> fxRows;

    vocalis::Panel textPanel { "Testo" }, voicePanel { "Voce" }, rhythmPanel { "Ritmo e groove" },
                   playPanel { "Riproduzione" }, fxPanel { "Effetti" };

    juce::TextEditor textEditor;
    juce::Label hintLabel;
    Combo* languageCombo = nullptr;

    juce::TextButton neuralButton { "NEURALE" }, robotButton { "ROBOT" }, dictButton { "DIZIONARIO" };
    Combo *voiceACombo, *voiceBCombo, *robotVarCombo, *stretchCombo, *loudCombo;
    int shownEngine = -1;

    Knob *blendK, *pitchK, *intonK, *speedK, *formantK, *robotK, *robotHzK;
    Combo *modeCombo, *divCombo, *bpmSrcCombo, *playModeCombo;
    Knob *stepsK, *swingK, *legatoK, *pcK, *grooveK, *accentK, *bpmK;
    juce::ToggleButton* loopToggle = nullptr;
    juce::ToggleButton* limiterToggle = nullptr;
    juce::Slider startBarSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label startBarLabel;
    std::unique_ptr<SliderAtt> startBarAtt;

    Knob* outGainK = nullptr;

    vocalis::GrooveView grooveView;
    vocalis::SyllableEditor waveform;
    vocalis::DragOutHandle dragHandle;

    juce::TextButton playButton { "ANTEPRIMA" }, renderButton { "RIGENERA" }, exportButton { "ESPORTA WAV" };
    juce::Label statusLabel, bpmLabel, dragHint;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalisEditor)
};
