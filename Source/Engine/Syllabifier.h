#pragma once
#include "VoiceTypes.h"

namespace vocalis
{

/** Raggruppa i fonemi in sillabe attorno ai nuclei vocalici.
    Regole: una consonante tra due vocali va con la seconda; in un gruppo di consonanti
    l'ultima va con la vocale successiva; un inizio di parola e' sempre un confine. */
class Syllabifier
{
public:
    static std::vector<Syllable> build (const std::vector<Phoneme>& phonemes,
                                        const std::vector<int>& wordStarts,
                                        const juce::AudioBuffer<float>& audio,
                                        double sampleRate);

    static bool isVowelName (const juce::String& ipa);
    static bool isPauseName (const juce::String& ipa);
};

} // namespace vocalis
