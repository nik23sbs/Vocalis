#pragma once
#include "VoiceTypes.h"

namespace vocalis
{

/** Raggruppa i fonemi in sillabe e trova il nucleo vocalico di ognuna.
    - una consonante tra due vocali va con la seconda; in un gruppo l'ultima va con la seguente,
      tranne ostruente + liquida/semivocale (pa-dre, a-cqua) che restano unite;
    - un inizio di parola e' sempre un confine;
    - dittongo discendente (vocale accentata + vocale atona nella stessa parola: "ciao", "mai") = una sillaba.
    I confini vengono poi rifiniti sull'audio: tagli nei minimi di energia e su un passaggio per lo zero,
    attacco della vocale nel punto di massima salita dell'energia. */
class Syllabifier
{
public:
    static std::vector<Syllable> build (const std::vector<Phoneme>& phonemes,
                                        const juce::AudioBuffer<float>& audio,
                                        double sampleRate);

    static bool isVowelName (const juce::String& ipa);
    static bool isPauseName (const juce::String& ipa);
    static bool isStressed (const juce::String& ipa);
    static juce::String stripMarks (const juce::String& ipa);
};

/** Analisi dell'energia a breve termine per rifinire i confini. */
class EnergyEnvelope
{
public:
    EnergyEnvelope (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** Punto (in campioni) di massima salita dell'energia in [centre-radius, centre+radius]. */
    int maxRise (int centre, int radius) const;
    /** Punto di minima energia in [centre-radius, centre+radius]. */
    int minEnergy (int centre, int radius) const;
    /** Passaggio per lo zero piu' vicino entro radius campioni. */
    int nearestZeroCrossing (int pos, int radius) const;
    float dbAt (int sample) const;

private:
    const float* data = nullptr;
    int numSamples = 0;
    int hop = 60;
    std::vector<float> db; // energia in dB per frame
};

} // namespace vocalis
