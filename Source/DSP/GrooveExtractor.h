#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace vocalis
{

/** Groove estratto da un sample: per ogni sedicesimo, quanto il colpo e' in anticipo/ritardo e quanto e' forte. */
struct GrooveTemplate
{
    struct Step
    {
        double offsetBeats = 0.0;  // spostamento rispetto alla griglia (in quarti), +/- 0.125
        float velocity = 0.0f;     // 0..1
        bool hasHit = false;
    };

    std::vector<Step> steps;            // risoluzione: 1/16 (4 step per quarto)
    std::vector<double> onsetBeats;     // colpi rilevati (per la grafica)
    std::vector<float> onsetStrength;
    double lengthBeats = 0.0;
    double sampleBpm = 120.0;
    juce::String sourceName;

    static constexpr int stepsPerBeat = 4;

    bool isEmpty() const { return steps.empty(); }

    /** Spostamento e accento per una posizione sulla griglia (in quarti). */
    void lookup (double gridBeat, double& offsetBeats, float& velocity) const;

    juce::ValueTree toValueTree() const;
    static GrooveTemplate fromValueTree (const juce::ValueTree& v);
};

class GrooveExtractor
{
public:
    /** Analizza il sample. `projectBpm` serve a capire quante battute dura il loop. */
    static GrooveTemplate extract (const juce::AudioBuffer<float>& audio, double sampleRate, double projectBpm);

    static GrooveTemplate extractFromFile (const juce::File& file, double projectBpm, juce::String& error);

    /** Rileva gli attacchi (in secondi) e la loro forza 0..1. */
    static void detectOnsets (const juce::AudioBuffer<float>& mono, double sampleRate,
                              std::vector<double>& timesSec, std::vector<float>& strengths);
};

} // namespace vocalis
