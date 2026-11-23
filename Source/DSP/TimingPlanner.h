#pragma once
#include "Engine/TokenSyllables.h"
#include "Engine/VoiceTypes.h"
#include "Quantizer.h"

namespace vocalis
{

/** Una nota della melodia (modalita' Canto): posizione e durata in quarti dall'inizio della clip. */
struct NoteEvent
{
    double startBeat = 0.0, lengthBeats = 0.25;
    int pitch = 60;
};

/** Dove deve cadere una sillaba: la vocale su anchorBeat, la voce si chiude (legato) su endBeat. */
struct SyllableTarget
{
    double anchorBeat = 0.0;
    double gridBeat = 0.0;     // posizione sulla griglia prima di swing/groove/modifiche
    double endBeat = 0.0;
    int noteMidi = -1;         // nota (Canto), -1 = nessuna
    float gainDb = 0.0f;       // accento del groove + modifica manuale
    bool muted = false;
};

/** Durate imposte a Kokoro per un blocco di fonemi. */
struct TimedDurations
{
    std::vector<int64_t> durations;      // token + 2 (inizio/fine frase)
    std::vector<double> targetFrame;     // frame esatto (non arrotondato) della vocale di ogni sillaba
    std::vector<int> vowelFrame;         // frame realizzato della vocale (inizio del nucleo)
    std::vector<int> endFrame;           // fine della voce della sillaba (frame)
    std::vector<int> onsetFrame;         // inizio della sillaba (prima consonante)
    double padSamples = 0.0;             // silenzio da anteporre all'audio del modello (24 kHz)
};

class TimingPlanner
{
public:
    /** Posizioni in quarti. Parlato: vuoto (tempi naturali). Rap: griglia + swing + groove + pause.
        Canto: le note (una per sillaba, poi la griglia riprende dopo l'ultima nota). */
    static std::vector<SyllableTarget> plan (const std::vector<TokenSyllable>& sylls, const RhythmSettings& rs,
                                             const GrooveTemplate& groove, const std::vector<SyllableEdit>& edits,
                                             VocalMode mode, const std::vector<NoteEvent>& notes);

    /** Parlato: i tempi sono quelli naturali (vocali in naturalAnchor, fine voce in naturalEnd, in quarti) e le
        modifiche li spostano: shiftBeats sposta la sillaba, lengthScale ne cambia la durata spostando le successive. */
    static std::vector<SyllableTarget> fromNatural (const std::vector<double>& naturalAnchor, const std::vector<double>& naturalEnd,
                                                    const std::vector<SyllableEdit>& edits, double bpm);

    /** Converte i target (in quarti, clip che inizia a startBeat) in durate per il modello.
        predicted = durate naturali della prima passata; le consonanti le conservano (x onsetScale), le vocali
        prendono il resto; l'errore di arrotondamento al frame viene diffuso sulle sillabe seguenti. */
    static TimedDurations durations (const juce::String& phonemes, const std::vector<int64_t>& predicted,
                                     const std::vector<TokenSyllable>& sylls, const std::vector<SyllableTarget>& targets,
                                     const std::vector<SyllableEdit>& edits, double bpm, double startBeat,
                                     VocalMode mode = VocalMode::Rap);

    /** Anticipo minimo (in quarti, multiplo di 1) perche' le consonanti iniziali stiano prima della prima vocale. */
    static double prerollBeats (const std::vector<int64_t>& predicted, const std::vector<TokenSyllable>& sylls,
                                const std::vector<SyllableTarget>& targets, double bpm);

    /** Mappa del micro-allineamento (campioni del modello): porta le vocali realizzate sui target, ma senza mai
        comprimere o stirare un tratto oltre il 25% (un target irraggiungibile resta un po' in ritardo). */
    static void warpMap (const std::vector<int>& vowelFrame, const std::vector<double>& targetFrame, int samplesPerFrame,
                         std::vector<double>& in, std::vector<double>& out);

    /** "Sul battere": di quanto (in quarti, multiplo dello step) spostare tutta la frase perche' le consonanti della
        prima sillaba stiano dopo l'inizio della clip. 0 se ci stanno gia'. */
    static double onBeatShift (const std::vector<int64_t>& predicted, const std::vector<TokenSyllable>& sylls,
                               const std::vector<SyllableTarget>& targets, double bpm, double stepBeats);

    static constexpr double framesPerSecond = 40.0;   // 24000 / 600
};

} // namespace vocalis
