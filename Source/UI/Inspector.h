#pragma once
#include "Widgets.h"
#include "GrooveView.h"

class VocalisProcessor;

namespace vocalis
{

/** Scheda "Sillaba": modifica della sillaba selezionata (nota/intonazione, durata, volume, attacco, stile, pronuncia). */
class SyllablePage : public juce::Component
{
public:
    explicit SyllablePage (VocalisProcessor& p);
    void setSelected (int syllable);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void push();
    VocalisProcessor& proc;
    int selected = -1;
    bool updating = false;
    juce::Slider tone, length, gain, onset;
    Chip vib { "Vibrato" }, glide { "Glide" }, whisper { "Sussurrato" }, adlib { "Ad-lib" }, mute { "Muta", Colours::textDim };
    juce::TextEditor ipa;
    PillButton applyIpa { "Applica" }, resetIpa { "Originale" }, resetSyl { "Ripristina sillaba" }, resetAll { "Ripristina tutto" };
    juce::String title, info, word;
};

/** Scheda "Ritmo": griglia, swing, legato, groove da sample, riproduzione. */
class RhythmPage : public juce::Component
{
public:
    explicit RhythmPage (VocalisProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
private:
    VocalisProcessor& proc;
    LabelledCombo division, bpmSource, playMode, loudness, attack;
    LabelledKnob steps, swing, legato, groove, accents, bpm;
    GrooveView grooveView;
    juce::Slider startBar { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ToggleButton loop { "Loop" };
    juce::Slider shift { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> startAtt, shiftAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt;
};

/** Scheda "Canto": melodia (file MIDI, registrazione dalla DAW), vibrato, glide, tonalita', armonie e doppio. */
class SingPage : public juce::Component
{
public:
    explicit SingPage (VocalisProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
private:
    VocalisProcessor& proc;
    PillButton loadMidi { "Carica MIDI" }, capture { "Registra dalla DAW" }, clearMelody { "Cancella" };
    LabelledKnob vibrato, glide, harmLevel, harmWidth, doubleLevel;
    LabelledCombo key, scale, harm1, harm2;
    Chip doubleOn { "Doppio", Colours::pink };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> doubleAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String melodyInfo;
};

/** Scheda "Effetti": catena effetti in una lista scorrevole. */
class FxPage : public juce::Component
{
public:
    explicit FxPage (VocalisProcessor& p);
    void resized() override;
private:
    struct Row : public juce::Component
    {
        Row (VocalisProcessor& p, const char* onId, juce::String name, std::vector<std::tuple<const char*, juce::String, juce::String, int>> knobs);
        void resized() override;
        void paint (juce::Graphics&) override;
        Chip toggle;
        juce::OwnedArray<LabelledKnob> knobs;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Row> rows;
    std::unique_ptr<LabelledCombo> delayDiv;
};

/** Pannello destro: schede + indicatore d'uscita. */
class Inspector : public juce::Component
{
public:
    explicit Inspector (VocalisProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int syllable);
    void refresh();
    void showTab (int i);
private:
    VocalisProcessor& proc;
    SyllablePage sylPage;
    RhythmPage rhythmPage;
    SingPage singPage;
    FxPage fxPage;
    OutputMeter meter;
    int tab = 0;
    juce::StringArray tabs { "Sillaba", "Ritmo", "Canto", "Effetti" };
};

} // namespace vocalis
