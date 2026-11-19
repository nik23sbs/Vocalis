#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace vocalis
{

/** Un fonema con i tempi in campioni (sample rate del motore). */
struct Phoneme
{
    juce::String name;      // IPA
    int start = 0;
    int end = 0;
    bool isVowel = false;
    bool isPause = false;
    bool wordStart = false;
};

/** Una sillaba: [start, end) con il nucleo vocalico [nucleusStart, nucleusEnd).
    Il nucleo e' il punto che l'orecchio percepisce come "colpo" (P-center). */
struct Syllable
{
    int start = 0, end = 0;
    int nucleusStart = 0, nucleusEnd = 0;
    juce::String label;          // fonemi IPA
    bool wordStart = false;
    double pauseAfterSec = 0.0;  // silenzio di punteggiatura che segue

    int onsetLength() const { return nucleusStart - start; }
    int codaLength() const  { return end - nucleusEnd; }
};

enum class EngineType { Neural = 0, Formant };
enum class VoiceType { Male = 0, Female, Robot };

/** Modifiche manuali di una sillaba dall'editor. */
struct SyllableEdit
{
    double shiftBeats = 0.0;   // spostamento rispetto alla posizione calcolata
    double lengthScale = 1.0;  // durata relativa allo slot
    float pitchSemis = 0.0f;   // trasposizione della sola sillaba
    float gainDb = 0.0f;
    bool muted = false;
    juce::String label;        // per riconoscere la sillaba quando il testo cambia

    bool isDefault() const
    {
        return std::abs (shiftBeats) < 1e-9 && std::abs (lengthScale - 1.0) < 1e-9
            && std::abs (pitchSemis) < 1e-6f && std::abs (gainDb) < 1e-6f && ! muted;
    }
};

struct VoiceSettings
{
    EngineType engine = EngineType::Neural;
    juce::String language = "it";     // lingua del testo (fonetica)

    // Neurale (Kokoro)
    juce::String voiceA = "if_sara";
    juce::String voiceB;              // vuoto = nessuna miscela
    float blend = 0.0f;               // 0 = solo A, 1 = solo B
    float speed = 1.0f;               // 0.5 .. 2

    // Formant (eSpeak, voci robot)
    juce::String espeakVariant = "UniRobot";
    int espeakPitch = 50;
    int espeakRate = 160;

    // Trasformazioni (WORLD) — valgono per entrambi i motori
    float pitchSemis = 0.0f;          // trasposizione globale
    float intonation = 1.0f;          // 0 = monotono, 1 = naturale, 2 = esagerato
    float formantSemis = 0.0f;        // spostamento delle formanti
    float robotize = 0.0f;            // 0..1
    float robotHz = 110.0f;

    juce::String dictionary;          // "parola = pronuncia" per riga

    juce::String synthKey (const juce::String& text) const
    {
        return text + "|" + dictionary + "|" + juce::String ((int) engine) + "|" + language + "|" + voiceA + "|" + voiceB
             + "|" + juce::String (blend, 3) + "|" + juce::String (speed, 3) + "|" + espeakVariant + "|"
             + juce::String (espeakPitch) + "|" + juce::String (espeakRate);
    }

    bool needsWorld() const
    {
        return std::abs (pitchSemis) > 0.01f || std::abs (intonation - 1.0f) > 0.01f || std::abs (formantSemis) > 0.01f;
    }
};

/** Risultato della sintesi: audio mono + fonemi + sillabe. */
struct SynthResult
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 24000.0;
    std::vector<Phoneme> phonemes;
    std::vector<Syllable> syllables;
    juce::String phonemeText;   // stringa IPA usata
    juce::String engineName;
    juce::String error;
    juce::String warning;       // es. "modello neurale non trovato, uso eSpeak"

    bool ok() const { return error.isEmpty() && audio.getNumSamples() > 0; }
};

class VoiceEngine
{
public:
    virtual ~VoiceEngine() = default;
    virtual SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) = 0;
};

} // namespace vocalis
