#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Render/Renderer.h"
#include "DSP/EffectsChain.h"

namespace IDs
{
    // Voce
    inline constexpr const char* voiceType = "voiceType";
    inline constexpr const char* varMale = "varMale";
    inline constexpr const char* varFemale = "varFemale";
    inline constexpr const char* varRobot = "varRobot";
    inline constexpr const char* language = "language";
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* intonation = "intonation";
    inline constexpr const char* speed = "speed";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* robotize = "robotize";
    inline constexpr const char* robotHz = "robotHz";
    // Ritmo
    inline constexpr const char* rhythmMode = "rhythmMode";
    inline constexpr const char* division = "division";
    inline constexpr const char* stepsPerSyl = "stepsPerSyl";
    inline constexpr const char* swing = "swing";
    inline constexpr const char* legato = "legato";
    inline constexpr const char* stretch = "stretch";
    inline constexpr const char* grooveAmt = "grooveAmt";
    inline constexpr const char* accentAmt = "accentAmt";
    inline constexpr const char* bpmSource = "bpmSource";
    inline constexpr const char* bpmManual = "bpmManual";
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

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    vocalis::Renderer renderer;
    vocalis::EffectsChain effects;

    juce::String text { "Ciao, sono Vocalis. Scrivi il tuo testo qui e lo trasformo in voce a tempo." };
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
    float voiceGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalisProcessor)
};
