#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vocalis
{

/** Sillaba nella stringa di fonemi di Kokoro: gli indici sono posizioni di carattere (= token, perche' ogni
    carattere del vocabolario e' un token). Le sillabe sono contigue e coprono tutta la stringa. */
struct TokenSyllable
{
    int firstToken = 0;       // primo token (attacco), comprende spazi e punteggiatura che precedono
    int nucleusFirst = 0;     // prima vocale
    int nucleusLast = 0;      // ultima vocale del nucleo
    int lastToken = 0;        // ultimo token (coda, spazi, punteggiatura)
    juce::String label;       // solo fonemi, senza accenti/spazi
    bool wordStart = false;
    bool stressed = false;
    int pauseSteps = 0;       // pausa di punteggiatura dopo la sillaba (step di griglia)
};

class TokenSyllables
{
public:
    static std::vector<TokenSyllable> build (const juce::String& phonemes);
    static bool isVowel (juce::juce_wchar c);
};

} // namespace vocalis
