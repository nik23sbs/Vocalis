#pragma once
#include "PluginProcessor.h"
#include "UI/VocalisLookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Timeline.h"
#include "UI/Inspector.h"

/** Interfaccia "Studio" (opzione A): barra in alto con modalita' e trasporto, voci e sfera a sinistra,
    testo + timeline + macro al centro, inspector a schede a destra. */
class VocalisEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit VocalisEditor (VocalisProcessor&);
    ~VocalisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Solo test/istantanee: seleziona una sillaba e mostra una scheda dell'inspector. */
    vocalis::Timeline& getTimelineForTest() { return timeline; }
    void selectForSnapshot (int syllable, int tab) { timeline.setSelected (syllable); inspector.setSelected (syllable); inspector.showTab (tab); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showDictionary();
    void exportWav();
    void updateVoiceInfo();

    VocalisProcessor& proc;
    vocalis::VocalisLookAndFeel lnf;

    // barra superiore
    vocalis::SegmentedChoice modeSwitch;
    vocalis::PlayButton playButton;
    juce::Label positionLabel, tempoLabel, statusLabel;
    vocalis::PillButton exportButton { "Esporta WAV" };
    vocalis::DragOutHandle dragHandle;

    // colonna voci
    juce::OwnedArray<vocalis::VoiceCard> cards;
    vocalis::VoiceOrb orb;
    juce::Label mixInfo;
    juce::Slider variant { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> variantAtt;
    vocalis::PillButton newVariant { "Nuova" };
    vocalis::SegmentedChoice engineSwitch;
    vocalis::LabelledCombo robotTimbre;
    juce::OwnedArray<vocalis::LabelledKnob> timbre;

    // centro
    juce::TextEditor textEditor;
    vocalis::LabelledCombo language;
    vocalis::PillButton dictButton { "Dizionario" };
    vocalis::Timeline timeline;
    juce::OwnedArray<vocalis::LabelledKnob> macros;

    // destra
    vocalis::Inspector inspector;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    static constexpr int headerH = 60, leftW = 248, rightW = 272;
};
