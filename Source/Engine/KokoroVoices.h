#pragma once
#include "VoiceTypes.h"
#include <map>
#include <mutex>
#include <vector>

namespace vocalis
{

/** Legge le voci di Kokoro da voices-v1.0.bin (archivio .npz di array float32 [510,1,256]). */
class KokoroVoices
{
public:
    static constexpr int styleDim = 256;
    static constexpr int maxLen = 510;

    bool open (const juce::File& npzFile, juce::String& error);
    bool isOpen() const { return zip != nullptr; }

    /** Vettore di stile per una frase di numTokens fonemi (riga numTokens-1), con miscela opzionale. */
    bool styleFor (const juce::String& voiceA, const juce::String& voiceB, float blend, int numTokens,
                   std::vector<float>& out, juce::String& error);

    /** Miscela fino a 4 voci (pesi normalizzati, voci senza nome o con peso 0 ignorate).
        rowOffset sposta la riga dello stile: e' il "seme" delle varianti (interpretazioni diverse). */
    bool styleFor (const VoiceMix& mix, int numTokens, int rowOffset, std::vector<float>& out, juce::String& error);

    juce::StringArray voiceNames() const;

    struct VoiceInfo { const char* id; const char* displayName; const char* language; bool male; };
    static const std::vector<VoiceInfo>& catalogue();

private:
    const std::vector<float>* load (const juce::String& name, juce::String& error);

    juce::File file;
    std::unique_ptr<juce::ZipFile> zip;
    std::map<juce::String, std::vector<float>> cache;
    std::mutex lock;
};

} // namespace vocalis
