#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace vocalis
{

/** Time-stretch WSOLA (Waveform Similarity Overlap-Add) per voce monofonica.
    Cambia la durata senza cambiare intonazione. */
class TimeStretch
{
public:
    /** Restituisce `numIn` campioni di `in` stirati a `targetLength` campioni. */
    static std::vector<float> process (const float* in, int numIn, int targetLength, double sampleRate);
};

} // namespace vocalis
