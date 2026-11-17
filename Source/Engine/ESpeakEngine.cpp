#include "ESpeakEngine.h"
#include "Syllabifier.h"
#include <espeak-ng/speak_lib.h>
#include <mutex>
#include <cmath>

namespace vocalis
{

namespace
{
    std::mutex& espeakMutex() { static std::mutex m; return m; }
    bool g_initialised = false;
    int  g_sampleRate = 22050;

    // Copia della tabella interna di eSpeak (wavegen.c): moltiplicatore del pitch = tab[p] / 128
    const unsigned char pitchAdjustTab[101] = {
         64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
         80,  81,  82,  83,  84,  86,  87,  88,  89,  91,  92,  93,  94,  96,  97,  98,
        100, 101, 103, 104, 105, 107, 108, 110, 111, 113, 115, 116, 118, 119, 121, 123,
        124, 126, 128, 130, 132, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153,
        155, 158, 160, 162, 164, 167, 169, 171, 174, 176, 179, 181, 184, 186, 189, 191,
        194, 197, 199, 202, 205, 208, 211, 214, 217, 220, 223, 226, 229, 232, 236, 239,
        242, 246, 249, 252, 254 };

    /** Trova il parametro pitch eSpeak che dà (moltiplicatore di p) * ratio. */
    int compensatePitch (int p, double ratio)
    {
        p = juce::jlimit (0, 100, p);
        const double target = pitchAdjustTab[p] * ratio;
        int best = 0;
        double bestErr = 1e9;
        for (int i = 0; i <= 100; ++i)
        {
            const double err = std::abs (pitchAdjustTab[i] - target);
            if (err < bestErr) { bestErr = err; best = i; }
        }
        return best;
    }

    struct Collector
    {
        std::vector<short> samples;
        std::vector<std::pair<int, juce::String>> phonemeEvents; // ms, nome
        std::vector<int> wordEvents;                             // ms
    };

    int synthCallback (short* wav, int numSamples, espeak_EVENT* events)
    {
        if (events == nullptr)
            return 0;

        auto* c = static_cast<Collector*> (events->user_data);
        if (c == nullptr)
            return 0;

        if (wav != nullptr && numSamples > 0)
            c->samples.insert (c->samples.end(), wav, wav + numSamples);

        for (auto* e = events; e->type != espeakEVENT_LIST_TERMINATED; ++e)
        {
            if (e->type == espeakEVENT_PHONEME)
            {
                char name[9] = {};
                std::memcpy (name, e->id.string, 8);
                c->phonemeEvents.emplace_back (e->audio_position, juce::String::fromUTF8 (name));
            }
            else if (e->type == espeakEVENT_WORD)
            {
                c->wordEvents.push_back (e->audio_position);
            }
        }
        return 0;
    }
}

ESpeakEngine::ESpeakEngine (juce::File dataParent)
{
    std::lock_guard<std::mutex> lock (espeakMutex());

    if (g_initialised)
    {
        ready = true;
        engineSampleRate = g_sampleRate;
        return;
    }

    if (dataParent == juce::File())
        dataParent = findDataParent();

    if (! dataParent.getChildFile ("espeak-ng-data").getChildFile ("phontab").existsAsFile())
    {
        initError = "Dati eSpeak non trovati (espeak-ng-data). Cercati in: " + dataParent.getFullPathName();
        return;
    }

    const auto path = dataParent.getFullPathName().toStdString();
    const int sr = espeak_Initialize (AUDIO_OUTPUT_SYNCHRONOUS, 0, path.c_str(),
                                      espeakINITIALIZE_PHONEME_EVENTS | espeakINITIALIZE_PHONEME_IPA
                                          | espeakINITIALIZE_DONT_EXIT);
    if (sr <= 0)
    {
        initError = "Inizializzazione eSpeak fallita";
        return;
    }

    espeak_SetSynthCallback (synthCallback);
    g_sampleRate = engineSampleRate = sr;
    g_initialised = ready = true;
}

juce::File ESpeakEngine::findDataParent()
{
    auto check = [] (const juce::File& f) { return f.getChildFile ("espeak-ng-data").getChildFile ("phontab").existsAsFile(); };

    // Binario del plugin: <bundle>/Contents/MacOS/Vocalis  oppure  <bundle>/Contents/x86_64-linux/Vocalis.so
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> candidates {
        exe.getParentDirectory().getParentDirectory().getChildFile ("Resources"),
        exe.getParentDirectory().getChildFile ("Resources"),
        exe.getParentDirectory(),
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Vocalis"),
        juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Vocalis"),
    };

    if (auto env = juce::SystemStats::getEnvironmentVariable ("VOCALIS_ESPEAK_DATA", {}); env.isNotEmpty())
        candidates.insert (0, juce::File (env));

    for (auto& c : candidates)
        if (check (c))
            return c;

    return candidates.getFirst();
}

SynthResult ESpeakEngine::synthesize (const juce::String& rawText, const VoiceSettings& s)
{
    SynthResult result;
    result.sampleRate = engineSampleRate;

    if (! ready)
    {
        result.error = initError;
        return result;
    }

    auto text = rawText.replaceCharacters ("<>", "  ").trim();
    if (text.isEmpty())
    {
        result.error = "Testo vuoto";
        return result;
    }

    // Formanti: l'audio verra' ricampionato di "f"; pre-compensiamo velocita' e pitch.
    const double f = juce::jlimit (0.6, 1.6, (double) s.formant);
    const bool shiftFormants = std::abs (f - 1.0) > 0.005;

    Collector collector;
    {
        std::lock_guard<std::mutex> lock (espeakMutex());

        const auto voiceName = (s.language + "+" + s.variant).toStdString();
        if (espeak_SetVoiceByName (voiceName.c_str()) != EE_OK)
        {
            if (espeak_SetVoiceByName (s.language.toRawUTF8()) != EE_OK)
            {
                result.error = "Voce non disponibile: " + juce::String (voiceName);
                return result;
            }
        }

        const int rate = juce::jlimit (espeakRATE_MINIMUM, espeakRATE_MAXIMUM, (int) std::round (s.rate / f));
        espeak_SetParameter (espeakRATE, rate, 0);
        espeak_SetParameter (espeakPITCH, shiftFormants ? compensatePitch (s.pitch, 1.0 / f) : juce::jlimit (0, 99, s.pitch), 0);
        espeak_SetParameter (espeakRANGE, juce::jlimit (0, 99, s.range), 0);
        espeak_SetParameter (espeakVOLUME, 100, 0);

        const auto utf8 = text.toStdString();
        const auto err = espeak_Synth (utf8.c_str(), utf8.size() + 1, 0, POS_CHARACTER, 0,
                                       espeakCHARS_UTF8, nullptr, &collector);
        espeak_Synchronize();

        if (err != EE_OK)
        {
            result.error = "Errore di sintesi eSpeak";
            return result;
        }
    }

    if (collector.samples.empty())
    {
        result.error = "eSpeak non ha prodotto audio";
        return result;
    }

    // --- PCM -> float
    const int n = (int) collector.samples.size();
    juce::AudioBuffer<float> raw (1, n);
    for (int i = 0; i < n; ++i)
        raw.setSample (0, i, collector.samples[(size_t) i] / 32768.0f);

    const double msToSamples = engineSampleRate / 1000.0;

    // --- Fonemi
    std::vector<Phoneme> phonemes;
    for (size_t i = 0; i < collector.phonemeEvents.size(); ++i)
    {
        Phoneme p;
        p.name = collector.phonemeEvents[i].second;
        p.start = juce::jlimit (0, n, (int) std::round (collector.phonemeEvents[i].first * msToSamples));
        phonemes.push_back (p);
    }
    std::stable_sort (phonemes.begin(), phonemes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    for (size_t i = 0; i < phonemes.size(); ++i)
    {
        phonemes[i].end = (i + 1 < phonemes.size()) ? phonemes[i + 1].start : n;
        phonemes[i].isPause = Syllabifier::isPauseName (phonemes[i].name);
        phonemes[i].isVowel = ! phonemes[i].isPause && Syllabifier::isVowelName (phonemes[i].name);
    }

    std::vector<int> words;
    for (auto ms : collector.wordEvents)
        words.push_back (juce::jlimit (0, n, (int) std::round (ms * msToSamples)));

    // --- Ricampionamento per le formanti
    if (shiftFormants)
    {
        const int outN = (int) std::floor (n / f);
        juce::AudioBuffer<float> padded (1, n + 64);
        padded.clear();
        padded.copyFrom (0, 0, raw, 0, 0, n);

        result.audio.setSize (1, outN);
        juce::WindowedSincInterpolator interp;
        interp.process (f, padded.getReadPointer (0), result.audio.getWritePointer (0), outN);

        auto scale = [f, outN] (int x) { return juce::jlimit (0, outN, (int) std::round (x / f)); };
        for (auto& p : phonemes) { p.start = scale (p.start); p.end = scale (p.end); }
        for (auto& w : words) w = scale (w);
    }
    else
    {
        result.audio = std::move (raw);
    }

    result.phonemes = std::move (phonemes);
    result.wordStarts = std::move (words);
    result.syllables = Syllabifier::build (result.phonemes, result.wordStarts, result.audio, result.sampleRate);
    return result;
}

const std::vector<ESpeakEngine::VariantInfo>& ESpeakEngine::variantsFor (VoiceType type)
{
    static const std::vector<VariantInfo> male {
        { "m3", "Uomo 1 (m3)" }, { "m1", "Uomo 2 (m1)" }, { "m2", "Uomo 3 (m2)" }, { "m4", "Uomo 4 (m4)" },
        { "m5", "Uomo 5 (m5)" }, { "m6", "Uomo 6 (m6)" }, { "m7", "Uomo 7 (m7)" }, { "m8", "Uomo 8 (m8)" },
        { "Andy", "Andy" }, { "Gene", "Gene" }, { "Lee", "Lee" }, { "Marco", "Marco" }, { "Mario", "Mario" },
        { "Michael", "Michael" }, { "max", "Max" }, { "john", "John" }, { "norbert", "Norbert" },
        { "edward", "Edward" }, { "croak", "Rauco" }, { "whisper", "Sussurro" } };
    static const std::vector<VariantInfo> female {
        { "f3", "Donna 1 (f3)" }, { "f1", "Donna 2 (f1)" }, { "f2", "Donna 3 (f2)" }, { "f4", "Donna 4 (f4)" },
        { "f5", "Donna 5 (f5)" }, { "Alicia", "Alicia" }, { "Andrea", "Andrea" }, { "Annie", "Annie" },
        { "anika", "Anika" }, { "belinda", "Belinda" }, { "linda", "Linda" }, { "aunty", "Zia" },
        { "grandma", "Nonna" }, { "whisperf", "Sussurro" } };
    static const std::vector<VariantInfo> robot {
        { "UniRobot", "UniRobot" }, { "anikaRobot", "Anika Robot" }, { "robosoft", "Robosoft 1" },
        { "robosoft2", "Robosoft 2" }, { "robosoft3", "Robosoft 3" }, { "robosoft4", "Robosoft 4" },
        { "klatt", "Klatt 1" }, { "klatt2", "Klatt 2" }, { "klatt3", "Klatt 3" }, { "klatt4", "Klatt 4" },
        { "Demonic", "Demoniaco" }, { "Storm", "Storm" }, { "Tweaky", "Tweaky" } };

    switch (type)
    {
        case VoiceType::Female: return female;
        case VoiceType::Robot:  return robot;
        case VoiceType::Male:
        default:                return male;
    }
}

const std::vector<ESpeakEngine::LanguageInfo>& ESpeakEngine::languages()
{
    static const std::vector<LanguageInfo> langs {
        { "it", "Italiano" }, { "en-us", "English (US)" }, { "en", "English (UK)" }, { "es", "Espanol" },
        { "fr", "Francais" }, { "de", "Deutsch" }, { "pt", "Portugues" }, { "pt-br", "Portugues (BR)" },
        { "nl", "Nederlands" }, { "pl", "Polski" }, { "ru", "Russkij" }, { "ja", "Nihongo" },
        { "la", "Latino" }, { "eo", "Esperanto" } };
    return langs;
}

} // namespace vocalis
