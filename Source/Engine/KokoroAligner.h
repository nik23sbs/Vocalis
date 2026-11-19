#pragma once
#include "VoiceTypes.h"

namespace vocalis
{

/** Trasforma la stringa di fonemi e le durate predette da Kokoro (in frame) in fonemi con tempi in campioni.
    durations ha lunghezza numChar + 2 (inizio e fine frase), come l'uscita "duration" del modello. */
class KokoroAligner
{
public:
    static std::vector<Phoneme> align (const juce::String& phonemes, const std::vector<int64_t>& durations,
                                       int numSamples, int sampleOffset = 0);
};

} // namespace vocalis
