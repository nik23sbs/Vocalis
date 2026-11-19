#pragma once
#include "VoiceTypes.h"

namespace vocalis
{

/** Motore eSpeak-NG (sintesi a formanti): usato per le voci robot e come riserva se manca il modello neurale. */
class ESpeakEngine : public VoiceEngine
{
public:
    ESpeakEngine();

    SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) override;

    bool isReady() const { return ready; }
    juce::String getInitError() const { return initError; }

    struct VariantInfo { const char* id; const char* displayName; };
    static const std::vector<VariantInfo>& variantsFor (VoiceType type);

    struct LanguageInfo { const char* id; const char* displayName; };
    static const std::vector<LanguageInfo>& languages();

private:
    bool ready = false;
    juce::String initError;
};

} // namespace vocalis
