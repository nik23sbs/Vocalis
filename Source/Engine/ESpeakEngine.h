#pragma once
#include "VoiceTypes.h"

namespace vocalis
{

/** Motore eSpeak-NG integrato (libreria statica + dati nel bundle del plugin).
    eSpeak ha stato globale: tutte le chiamate sono serializzate da un mutex statico. */
class ESpeakEngine : public VoiceEngine
{
public:
    /** dataParent = cartella che CONTIENE "espeak-ng-data". Se vuota viene cercata automaticamente. */
    explicit ESpeakEngine (juce::File dataParent = {});

    SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) override;
    juce::String getName() const override { return "eSpeak-NG"; }

    bool isReady() const { return ready; }
    juce::String getInitError() const { return initError; }

    /** Cerca la cartella dati vicino al binario del plugin (Contents/Resources) o in Application Support. */
    static juce::File findDataParent();

    struct VariantInfo { const char* id; const char* displayName; };
    static const std::vector<VariantInfo>& variantsFor (VoiceType type);

    struct LanguageInfo { const char* id; const char* displayName; };
    static const std::vector<LanguageInfo>& languages();

private:
    bool ready = false;
    juce::String initError;
    int engineSampleRate = 22050;
};

} // namespace vocalis
