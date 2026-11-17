#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace vocalis
{

/** Un fonema prodotto dal motore, con i tempi in campioni (sample rate del motore). */
struct Phoneme
{
    juce::String name;      // IPA
    int start = 0;
    int end = 0;
    bool isVowel = false;
    bool isPause = false;
};

/** Una sillaba: porzione di audio che il quantizzatore sposta sulla griglia. */
struct Syllable
{
    int start = 0;          // in campioni
    int end = 0;
    juce::String label;     // fonemi IPA della sillaba
    bool wordStart = false;
    double pauseAfterSec = 0.0; // silenzio (punteggiatura) che segue la sillaba
};

enum class VoiceType { Male = 0, Female, Robot };

struct VoiceSettings
{
    juce::String language = "it";
    juce::String variant = "m3";
    int pitch = 50;          // 0..99   (eSpeak)
    int range = 50;          // 0..99   intonazione
    int rate = 160;          // parole al minuto
    float formant = 1.0f;    // 0.7..1.4 fattore sulle formanti
    float robotize = 0.0f;   // 0..1
    float robotHz = 110.0f;  // pitch del robot

    juce::String cacheKey (const juce::String& text) const
    {
        return text + "|" + language + "|" + variant + "|" + juce::String (pitch) + "|" + juce::String (range)
             + "|" + juce::String (rate) + "|" + juce::String (formant, 3);
    }
};

/** Risultato della sintesi: audio mono + fonemi + sillabe. */
struct SynthResult
{
    juce::AudioBuffer<float> audio;   // mono
    double sampleRate = 22050.0;
    std::vector<Phoneme> phonemes;
    std::vector<int> wordStarts;      // in campioni
    std::vector<Syllable> syllables;
    juce::String error;

    bool ok() const { return error.isEmpty() && audio.getNumSamples() > 0; }
};

/** Interfaccia comune per i motori vocali (eSpeak ora, Piper nella fase 2). */
class VoiceEngine
{
public:
    virtual ~VoiceEngine() = default;
    virtual SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) = 0;
    virtual juce::String getName() const = 0;
};

} // namespace vocalis
