#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vocalis
{

/** Testo -> fonemi -> token per Kokoro.
    Replica la fonetica usata nell'addestramento (misaki EspeakG2P): eSpeak IPA con stress,
    legature convertite nei simboli di Kokoro (tʃ -> ʧ, ts -> ʦ ...), punteggiatura preservata.
    Supporta un dizionario personale: "parola = pronuncia" (riscrittura) o "parola = /ipa/". */
class Phonemizer
{
public:
    static juce::String phonemize (const juce::String& text, const juce::String& language, const juce::String& dictionary);

    /** Applica le regole misaki a una stringa IPA di eSpeak con legature '^'. */
    static juce::String espeakToKokoro (const juce::String& ipa);

    /** Tiene solo i caratteri del vocabolario di Kokoro. */
    static juce::String filterToVocab (const juce::String& phonemes);
    static std::vector<int64_t> tokenize (const juce::String& phonemes);
    static bool inVocab (juce::juce_wchar c);

    /** Divide in blocchi di al massimo maxTokens, preferendo i confini di frase. */
    static juce::StringArray splitForModel (const juce::String& phonemes, int maxTokens = 480);
};

} // namespace vocalis
