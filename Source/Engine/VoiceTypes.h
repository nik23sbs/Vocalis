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

/** Miscela di fino a 4 voci neurali (sfera voce). */
struct VoiceMix
{
    juce::String id[4];
    float w[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    juce::String key() const
    {
        juce::String k;
        for (int i = 0; i < 4; ++i) if (id[i].isNotEmpty() && w[i] > 1.0e-4f) k << id[i] << ":" << juce::String (w[i], 3) << ",";
        return k;
    }
};

enum class VocalMode { Parlato = 0, Rap, Canto };

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
    int note = -1;             // nota MIDI della sillaba (Canto), -1 = automatica
    float onsetScale = 1.0f;   // durata delle consonanti iniziali
    bool vibrato = false, glide = false, whisper = false, adlib = false;

    bool isDefault() const
    {
        return std::abs (shiftBeats) < 1e-9 && std::abs (lengthScale - 1.0) < 1e-9
            && std::abs (pitchSemis) < 1e-6f && std::abs (gainDb) < 1e-6f && ! muted
            && note < 0 && std::abs (onsetScale - 1.0f) < 1e-6f && ! vibrato && ! glide && ! whisper && ! adlib;
    }
};

struct VoiceSettings
{
    EngineType engine = EngineType::Neural;
    juce::String language = "it";     // lingua del testo (fonetica)

    // Neurale (Kokoro)
    VoiceMix mix { { "if_sara", {}, {}, {} }, { 1.0f, 0.0f, 0.0f, 0.0f } };
    int variant = 0;                  // seme delle varianti (riga dello stile)
    float speed = 1.0f;               // 0.5 .. 2

    /** Voce con il peso maggiore (per la voce eSpeak di riserva e le etichette). */
    juce::String primaryVoice() const
    {
        int best = 0;
        for (int i = 1; i < 4; ++i) if (mix.id[i].isNotEmpty() && mix.w[i] > mix.w[best]) best = i;
        return mix.id[best];
    }

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
        return text + "|" + dictionary + "|" + juce::String ((int) engine) + "|" + language + "|" + mix.key()
             + "|v" + juce::String (variant) + "|" + juce::String (speed, 3) + "|" + espeakVariant + "|"
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
