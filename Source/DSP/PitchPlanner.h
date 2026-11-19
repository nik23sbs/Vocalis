#pragma once
#include "Engine/TokenSyllables.h"
#include "Engine/VoiceTypes.h"

namespace vocalis
{

struct PitchSettings
{
    VocalMode mode = VocalMode::Rap;
    float globalSemis = 0.0f;     // trasposizione
    float intonation = 1.0f;      // 0 = monotono, 1 = naturale, 2 = esagerato (parlato/rap)
    float expressivity = 0.3f;    // Canto: quanta micro-intonazione naturale resta sulle note (0..1)
    float vibratoCents = 0.0f;    // vibrato globale sulle vocali lunghe (Canto)
    float vibratoRate = 5.5f;     // Hz
    float glideMs = 60.0f;        // portamento tra due note
};

/** Costruisce la curva F0 (Hz, 2 valori per frame da 25 ms) da imporre a Kokoro. */
class PitchPlanner
{
public:
    /** naturalF0: curva prodotta dal modello con le stesse durate. notes: nota MIDI per sillaba (-1 = nessuna). */
    static std::vector<float> build (const std::vector<float>& naturalF0, const std::vector<int64_t>& durations,
                                     const std::vector<TokenSyllable>& sylls, const std::vector<int>& notes,
                                     const std::vector<SyllableEdit>& edits, const PitchSettings& s);

    /** Canto: energia (N del modello, 2 valori per frame) tenuta sulle vocali, che altrimenti si spengono
        quando sono molto piu' lunghe del parlato. Fuori dal Canto restituisce la curva naturale. */
    static std::vector<float> sustainEnergy (const std::vector<float>& naturalN, const std::vector<int64_t>& durations,
                                             const std::vector<TokenSyllable>& sylls, VocalMode mode);

    /** true se build() restituirebbe la curva naturale invariata (si evita una passata del modello). */
    static bool isIdentity (const std::vector<int>& notes, const std::vector<SyllableEdit>& edits, const PitchSettings& s);

    /** Semitoni per salire/scendere di `degrees` gradi nella scala (key 0 = Do, minor = minore naturale). */
    static int diatonicShift (int midi, int degrees, int key, bool minor);

    /** Nota (MIDI, frazionaria) mediana della curva in [fromSample, toSample). 0 se non intonata. */
    static float medianMidi (const std::vector<float>& f0, int fromSample, int toSample);

    static float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
    static float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    static constexpr int f0PerFrame = 2;
};

} // namespace vocalis
