#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "Render/Renderer.h"
#include "DSP/EffectsChain.h"
#include "Engine/KokoroVoices.h"

namespace IDs
{
    // Voce
    inline constexpr const char* engine = "engine";
    inline constexpr const char* voiceA = "voiceA";
    inline constexpr const char* voiceB = "voiceB";
    inline constexpr const char* voiceC = "voiceC";
    inline constexpr const char* voiceD = "voiceD";
    inline constexpr const char* orbX = "orbX";            // sfera voce: 0 = A/C, 1 = B/D
    inline constexpr const char* orbY = "orbY";            // 0 = A/B, 1 = C/D
    inline constexpr const char* variant = "variant";      // seme delle varianti
    inline constexpr const char* mode = "mode";            // Parlato / Rap / Canto
    inline constexpr const char* attack = "attack";        // Sul battere / Anticipato
    inline constexpr const char* vocalShift = "vocalShift"; // sposta tutto il vocal (quarti, a 1/32)
    inline constexpr const char* robotVariant = "robotVariant";
    inline constexpr const char* language = "language";
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* intonation = "intonation";
    inline constexpr const char* speed = "speed";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* robotize = "robotize";
    inline constexpr const char* robotHz = "robotHz";
    // Ritmo
    inline constexpr const char* division = "division";
    inline constexpr const char* stepsPerSyl = "stepsPerSyl";
    inline constexpr const char* swing = "swing";
    inline constexpr const char* legato = "legato";
    inline constexpr const char* stretch = "stretchMode";
    inline constexpr const char* pCenter = "pCenter";
    inline constexpr const char* loudness = "loudness";
    inline constexpr const char* grooveAmt = "grooveAmt";
    inline constexpr const char* accentAmt = "accentAmt";
    inline constexpr const char* bpmSource = "bpmSource";
    inline constexpr const char* bpmManual = "bpmManual";
    // Canto e armonie
    inline constexpr const char* vibrato = "vibrato", *glide = "glide";
    inline constexpr const char* key = "key", *scale = "scale";
    inline constexpr const char* harm1 = "harm1", *harm2 = "harm2", *harmLevel = "harmLevel", *harmWidth = "harmWidth";
    inline constexpr const char* doubleOn = "doubleOn", *doubleLevel = "doubleLevel";
    // Macro
    inline constexpr const char* mExpr = "mExpr", *mFlow = "mFlow", *mWarmth = "mWarmth";
    inline constexpr const char* mBreath = "mBreath", *mChoir = "mChoir", *mSpace = "mSpace";
    // Riproduzione
    inline constexpr const char* playMode = "playMode";
    inline constexpr const char* startBar = "startBar";
    inline constexpr const char* loop = "loop";
    // Effetti
    inline constexpr const char* ringOn = "ringOn", *ringFreq = "ringFreq", *ringMix = "ringMix";
    inline constexpr const char* distOn = "distOn", *distDrive = "distDrive", *distMix = "distMix";
    inline constexpr const char* crushOn = "crushOn", *crushBits = "crushBits", *crushDown = "crushDown", *crushMix = "crushMix";
    inline constexpr const char* eqOn = "eqOn", *eqLowCut = "eqLowCut", *eqHighCut = "eqHighCut", *eqPresence = "eqPresence";
    inline constexpr const char* compOn = "compOn", *compThresh = "compThresh", *compRatio = "compRatio";
    inline constexpr const char* chorusOn = "chorusOn", *chorusRate = "chorusRate", *chorusDepth = "chorusDepth", *chorusMix = "chorusMix";
    inline constexpr const char* delayOn = "delayOn", *delayDiv = "delayDiv", *delayFb = "delayFb", *delayMix = "delayMix";
    inline constexpr const char* reverbOn = "reverbOn", *reverbSize = "reverbSize", *reverbDamp = "reverbDamp", *reverbMix = "reverbMix";
    inline constexpr const char* outGain = "outGain";
    inline constexpr const char* deessOn = "deessOn", *deessAmount = "deessAmount";
    inline constexpr const char* doublerOn = "doublerOn", *doublerMix = "doublerMix", *doublerWidth = "doublerWidth";
    inline constexpr const char* limiterOn = "limiterOn";
}

class VocalisProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    VocalisProcessor();
    ~VocalisProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocalis"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- API per l'interfaccia (message thread)
    juce::AudioProcessorValueTreeState apvts;

    void setText (const juce::String& t);
    juce::String getText() const { return text; }

    void setDictionary (const juce::String& d);
    juce::String getDictionary() const { return dictionary; }

    // Modifiche manuali delle sillabe (editor)
    void setEdit (int index, const vocalis::SyllableEdit& e);
    vocalis::SyllableEdit getEdit (int index) const;
    void resetEdits();
    bool hasEdits() const;

    // Melodia (Canto): da file MIDI, dalla cattura dalla DAW o dalle note delle sillabe
    bool loadMidiFile (const juce::File& f, juce::String& error);
    void setMelody (std::vector<vocalis::NoteEvent> m);
    std::vector<vocalis::NoteEvent> getMelody() const { return melody; }
    void startMidiCapture();
    void stopMidiCapture();
    bool isCapturingMidi() const { return capturing.load(); }

    // Pronuncia: parola del testo a cui appartiene una sillaba; scrive "parola = /ipa/" nel dizionario (vuoto = rimuove)
    juce::String getTextWordForSyllable (int syllableIndex) const;
    juce::String getPronunciation (const juce::String& word) const;
    void setPronunciation (const juce::String& word, const juce::String& ipa);

    /** Solo test: esegue il modello con un runner esterno. */
    void setModelRunnerForTests (vocalis::Renderer::ModelRunner r) { renderer.setModelRunner (std::move (r)); forceRender(); }

    static juce::StringArray harmonyNames();
    static int harmonyDegrees (int choiceIndex);

    bool loadGrooveFile (const juce::File& f, juce::String& error);
    void clearGroove();
    const vocalis::GrooveTemplate& getGroove() const { return groove; }

    vocalis::RenderedClip::Ptr getClip() const;
    bool isRendering() const { return renderer.isBusy() || pendingSignature.isNotEmpty(); }
    juce::String getStatus() const { return status; }
    double getEffectiveBpm() const;

    void forceRender();
    /** Genera subito la clip nel thread chiamante (test e situazioni senza message loop). */
    void renderBlocking();
    void startPreview();
    void stopPreview();
    bool isPreviewing() const { return voicePos.load() >= 0; }

    /** Picco dell'uscita dell'ultimo blocco (per l'indicatore di livello). */
    float getOutputPeak() const { return outPeak.load(); }

    /** Posizione di riproduzione corrente in campioni della clip (-1 = ferma). */
    double getPlayPosition() const { return displayPos.load(); }

    bool exportWav (const juce::File& file, juce::String& error);
    juce::File createTempExport (juce::String& error);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;
    vocalis::RenderRequest buildRequest() const;
    juce::String requestSignature() const;
    vocalis::EffectsParams readEffects() const;
    void setClip (vocalis::RenderedClip::Ptr c);
    juce::String editsToString() const;
    void editsFromString (const juce::String& s);

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    vocalis::Renderer renderer;
    vocalis::EffectsChain effects;

    juce::String text { "Ciao, sono Vocalis. Scrivi il tuo testo qui e lo trasformo in voce a tempo." };
    std::vector<vocalis::NoteEvent> melody;
    int melodyVersion = 0;

    // cattura MIDI (audio thread -> message thread senza allocazioni)
    struct CapturedEvent { double ppq; int pitch; bool on; };
    std::array<CapturedEvent, 1024> captured {};
    std::atomic<int> capturedCount { 0 };
    std::atomic<bool> capturing { false };
    std::atomic<double> captureStartPpq { -1.0 };
    juce::String dictionary;
    std::vector<vocalis::SyllableEdit> edits;
    vocalis::GrooveTemplate groove;
    int grooveVersion = 0;

    juce::SpinLock clipLock;
    vocalis::RenderedClip::Ptr clip;
    juce::Array<vocalis::RenderedClip::Ptr> graveyard; // tiene vive le clip vecchie finche' l'audio thread puo' usarle
    juce::String status;

    juce::String lastSignature, pendingSignature;
    juce::uint32 pendingSince = 0;

    double sampleRate = 44100.0;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostBeatsPerBar { 4 };
    std::atomic<int> voicePos { -1 };          // posizione del trigger MIDI / anteprima
    std::atomic<bool> previewRequest { false }, stopRequest { false };
    std::atomic<double> displayPos { -1.0 };
    std::atomic<float> outPeak { 0.0f };
    float voiceGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalisProcessor)
};
