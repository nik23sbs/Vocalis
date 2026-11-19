#pragma once
#include "Engine/VoiceTypes.h"
#include "GrooveExtractor.h"

namespace vocalis
{

struct RhythmSettings
{
    enum class Mode { Free = 0, Grid };
    enum class Stretch { Off = 0, Vowels, Whole };

    Mode mode = Mode::Grid;
    Stretch stretch = Stretch::Vowels;

    double bpm = 120.0;
    int beatsPerBar = 4;
    double stepBeats = 0.25;     // 1/16 = 0.25 quarti
    int stepsPerSyllable = 1;
    double swing = 0.5;          // 0.5 = dritto, 0.75 = swing massimo
    double legato = 0.9;         // quanta parte dello step riempie la sillaba
    double pCenter = 1.0;        // 1 = la vocale cade sulla griglia (consonanti in anticipo), 0 = inizio sillaba sulla griglia
    double grooveAmount = 0.0;   // 0..1
    double accentAmount = 0.0;   // 0..1
    int restStepsMax = 4;        // pause massime per la punteggiatura

    static double divisionToBeats (int choiceIndex);
    static juce::StringArray divisionNames();
};

/** Posizione di una sillaba nella clip finale. */
struct ClipMarker
{
    int start = 0, end = 0, nucleus = 0;   // campioni nella clip
    juce::String label;
    int syllableIndex = -1;
    double gridBeat = 0.0;                 // posizione sulla griglia prima delle modifiche manuali
    bool muted = false;
    int noteMidi = -1;                     // nota (Canto) o nota piu' vicina all'intonazione
};

struct QuantizedClip
{
    juce::AudioBuffer<float> audio;  // mono, sample rate della sintesi
    std::vector<ClipMarker> markers;
    double lengthBeats = 0.0;
};

class Quantizer
{
public:
    static QuantizedClip process (const SynthResult& voice, const RhythmSettings& rs, const GrooveTemplate& groove,
                                  const std::vector<SyllableEdit>& edits = {});
};

} // namespace vocalis
