#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/VoiceTypes.h"

namespace vocalis
{

/** Trasformazioni della voce con il vocoder WORLD (analisi F0 + inviluppo spettrale + aperiodicita').
    Permette di cambiare intonazione e formanti in modo indipendente, con qualita' molto superiore
    al ricampionamento. Si usa solo se serve (altrimenti l'audio del motore resta intatto). */
struct WorldTransform
{
    float pitchSemis = 0.0f;      // trasposizione globale
    float intonation = 1.0f;      // scala delle variazioni di F0 attorno alla mediana (0 = monotono)
    float formantSemis = 0.0f;    // spostamento delle formanti
    struct Range { int start, end; float semis; };
    std::vector<Range> syllablePitch; // trasposizioni per sillaba (in campioni)

    bool isIdentity() const
    {
        if (std::abs (pitchSemis) > 0.01f || std::abs (intonation - 1.0f) > 0.01f || std::abs (formantSemis) > 0.01f)
            return false;
        for (auto& r : syllablePitch) if (std::abs (r.semis) > 0.01f) return false;
        return true;
    }
};

class WorldVoice
{
public:
    /** Elabora in place l'audio mono. Ritorna false se l'analisi non e' possibile (audio troppo corto). */
    static bool process (juce::AudioBuffer<float>& mono, double sampleRate, const WorldTransform& t);

    /** F0 mediana della voce (Hz), utile per il cantato e per la grafica. */
    static double medianF0 (const juce::AudioBuffer<float>& mono, double sampleRate);
};

} // namespace vocalis
