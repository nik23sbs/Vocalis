#pragma once
#include "Engine/VoiceTypes.h"
#include "GrooveExtractor.h"

namespace vocalis
{

struct RhythmSettings
{
    enum class Mode { Free = 0, Grid };
    Mode mode = Mode::Grid;

    double bpm = 120.0;
    int beatsPerBar = 4;
    double stepBeats = 0.25;     // 1/16 = 0.25 quarti
    int stepsPerSyllable = 1;
    double swing = 0.5;          // 0.5 = dritto, 0.75 = swing massimo
    double legato = 0.9;         // quanta parte dello step riempie la sillaba
    bool stretchToFit = true;
    double grooveAmount = 0.0;   // 0..1
    double accentAmount = 0.0;   // 0..1
    int restStepsMax = 4;        // pause massime per la punteggiatura

    static double divisionToBeats (int choiceIndex);
    static juce::StringArray divisionNames();
};

/** Marker di una sillaba nella clip finale (per grafica e future funzioni MIDI). */
struct ClipMarker
{
    int start = 0, end = 0;
    juce::String label;
};

struct QuantizedClip
{
    juce::AudioBuffer<float> audio;  // mono, stesso sample rate della sintesi
    std::vector<ClipMarker> markers;
    double lengthBeats = 0.0;
};

class Quantizer
{
public:
    static QuantizedClip process (const SynthResult& voice, const RhythmSettings& rs, const GrooveTemplate& groove);
};

} // namespace vocalis
