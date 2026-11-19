#include "ESpeakEngine.h"
#include "ESpeakCore.h"
#include "Syllabifier.h"
#include <espeak-ng/speak_lib.h>
#include <cmath>

namespace vocalis
{

namespace
{
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
                c->wordEvents.push_back (e->audio_position);
        }
        return 0;
    }
}

ESpeakEngine::ESpeakEngine()
{
    ready = ESpeakCore::ensureInitialised (initError);
}

SynthResult ESpeakEngine::synthesize (const juce::String& rawText, const VoiceSettings& s)
{
    SynthResult result;
    result.sampleRate = ESpeakCore::sampleRate();
    result.engineName = "eSpeak";

    if (! ready)
    {
        result.error = initError;
        return result;
    }

    // il dizionario funziona anche qui (solo le riscritture, l'IPA non si applica a eSpeak)
    auto text = rawText.replaceCharacters ("<>", "  ").trim();
    for (auto line : juce::StringArray::fromLines (s.dictionary))
    {
        const int eq = line.indexOfChar ('=');
        if (eq <= 0) continue;
        const auto key = line.substring (0, eq).trim(), val = line.substring (eq + 1).trim();
        if (key.isNotEmpty() && val.isNotEmpty() && ! val.startsWithChar ('/'))
            text = text.replace (key, val, true);
    }
    if (text.isEmpty())
    {
        result.error = "Testo vuoto";
        return result;
    }

    Collector collector;
    {
        std::lock_guard<std::mutex> lock (ESpeakCore::mutex());
        espeak_SetSynthCallback (synthCallback);

        const auto voiceName = (s.language + "+" + s.espeakVariant).toStdString();
        if (espeak_SetVoiceByName (voiceName.c_str()) != EE_OK && espeak_SetVoiceByName (s.language.toRawUTF8()) != EE_OK)
        {
            result.error = "Voce non disponibile: " + juce::String (voiceName);
            return result;
        }

        espeak_SetParameter (espeakRATE, juce::jlimit (espeakRATE_MINIMUM, espeakRATE_MAXIMUM, s.espeakRate), 0);
        espeak_SetParameter (espeakPITCH, juce::jlimit (0, 99, s.espeakPitch), 0);
        espeak_SetParameter (espeakRANGE, 50, 0);
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

    const int n = (int) collector.samples.size();
    result.audio.setSize (1, n);
    for (int i = 0; i < n; ++i)
        result.audio.setSample (0, i, collector.samples[(size_t) i] / 32768.0f);

    const double msToSamples = result.sampleRate / 1000.0;
    std::vector<Phoneme> phonemes;
    for (auto& [ms, name] : collector.phonemeEvents)
    {
        Phoneme p;
        p.name = name;
        p.start = juce::jlimit (0, n, (int) std::round (ms * msToSamples));
        phonemes.push_back (p);
    }
    std::stable_sort (phonemes.begin(), phonemes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    for (size_t i = 0; i < phonemes.size(); ++i)
    {
        phonemes[i].end = (i + 1 < phonemes.size()) ? phonemes[i + 1].start : n;
        phonemes[i].isPause = Syllabifier::isPauseName (phonemes[i].name);
        phonemes[i].isVowel = ! phonemes[i].isPause && Syllabifier::isVowelName (phonemes[i].name);
    }

    // eventi parola: arrivano un po' in anticipo -> primo fonema che parte da li' in poi
    const int tol = (int) (0.003 * result.sampleRate);
    for (auto ms : collector.wordEvents)
    {
        const int ws = (int) std::round (ms * msToSamples);
        for (auto& p : phonemes)
            if (! p.isPause && p.start >= ws - tol)
            {
                if (p.start - ws < (int) (0.08 * result.sampleRate)) p.wordStart = true;
                break;
            }
    }

    result.phonemes = std::move (phonemes);
    result.syllables = Syllabifier::build (result.phonemes, result.audio, result.sampleRate);
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
