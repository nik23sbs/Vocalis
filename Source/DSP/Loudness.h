#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace vocalis
{

/** Loudness integrata secondo ITU-R BS.1770-4 (filtro K, gating assoluto -70 LUFS e relativo -10 LU). */
class Loudness
{
public:
    static double integratedLufs (const juce::AudioBuffer<float>& buffer, double sampleRate);

    /** Porta il buffer a targetLufs, senza superare peakCeilingDb di picco (riduce il guadagno se serve). */
    static float normalise (juce::AudioBuffer<float>& buffer, double sampleRate, double targetLufs, float peakCeilingDb = -1.0f);
};

} // namespace vocalis
