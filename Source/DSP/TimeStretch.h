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

    /** Deformazione temporale continua (senza tagli): il campione inPts[i] dell'ingresso finisce in outPts[i]
        dell'uscita, lineare tra un punto e l'altro. Punti crescenti; lunghezza uscita = outPts.back(). */
    static std::vector<float> warp (const float* in, int numIn, const std::vector<double>& inPts,
                                    const std::vector<double>& outPts, double sampleRate);
};

} // namespace vocalis
