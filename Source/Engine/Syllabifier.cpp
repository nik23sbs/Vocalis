#include "Syllabifier.h"
#include <cmath>

namespace vocalis
{

namespace
{
    juce::String stripMarks (const juce::String& s)
    {
        juce::String out;
        for (auto c : s)
        {
            // accenti (ˈ ˌ), lunghezza (ː ˑ), spazi e trattini non contano
            if (c == 0x02C8 || c == 0x02CC || c == 0x02D0 || c == 0x02D1 || c == ' ' || c == '-' || c == '\'' || c == ',')
                continue;
            out << juce::String::charToString (c);
        }
        return out;
    }
}

bool Syllabifier::isPauseName (const juce::String& ipa)
{
    const auto s = stripMarks (ipa);
    return s.isEmpty() || s.containsOnly ("_:|");
}

bool Syllabifier::isVowelName (const juce::String& ipa)
{
    static const juce::String vowels = juce::String::fromUTF8 (
        "aeiouyAEIOUY\xC9\x91\xC9\x90\xC9\x92\xC3\xA6\xC9\x94\xC9\x99\xC9\x98\xC9\x9A\xC9\x9B\xC9\x9C\xC9\x9D\xC9\x9E"
        "\xC9\xA4\xC9\xA8\xC9\xAA\xC9\xAF\xC9\xB5\xC3\xB8\xC5\x93\xC9\xB6\xCA\x89\xCA\x8A\xCA\x8C\xCA\x8F"
        "\xC3\xA0\xC3\xA8\xC3\xA9\xC3\xAC\xC3\xB2\xC3\xB9\xE1\xB5\xBB");
    const auto s = stripMarks (ipa);
    if (s.isEmpty())
        return false;
    return vowels.indexOfChar (s[0]) >= 0;
}

std::vector<Syllable> Syllabifier::build (const std::vector<Phoneme>& ph,
                                          const std::vector<int>& wordStarts,
                                          const juce::AudioBuffer<float>& audio,
                                          double sampleRate)
{
    std::vector<Syllable> out;
    const int n = (int) ph.size();
    if (n == 0)
        return out;

    const int minPause = (int) (0.03 * sampleRate);

    // Inizio parola = primo fonema (non pausa) che parte da quel punto in poi (gli eventi parola di eSpeak arrivano un po' in anticipo)
    std::vector<bool> isWordStart ((size_t) n, false);
    const int tolerance = (int) (0.003 * sampleRate);
    for (auto ws : wordStarts)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ph[(size_t) i].isPause) continue;
            if (ph[(size_t) i].start >= ws - tolerance)
            {
                if (ph[(size_t) i].start - ws < (int) (0.08 * sampleRate))
                    isWordStart[(size_t) i] = true;
                break;
            }
        }
    }

    auto isRealPause = [&] (int i) { return ph[(size_t) i].isPause && (ph[(size_t) i].end - ph[(size_t) i].start) >= minPause; };

    // Trim del silenzio in coda a una sillaba
    const float* data = audio.getNumSamples() > 0 ? audio.getReadPointer (0) : nullptr;
    auto trimTail = [&] (int start, int end)
    {
        if (data == nullptr) return end;
        const float thr = 0.004f; // ~ -48 dB
        int e = juce::jmin (end, audio.getNumSamples());
        while (e > start + 64 && std::abs (data[e - 1]) < thr) --e;
        return juce::jmin (end, e + (int) (0.01 * sampleRate));
    };

    int i = 0;
    while (i < n)
    {
        // salta pause / fonemi vuoti
        while (i < n && ph[(size_t) i].isPause) ++i;
        if (i >= n) break;

        // gruppo = fonemi fino alla prossima pausa reale
        const int g0 = i;
        while (i < n && ! isRealPause (i)) ++i;
        int g1 = i - 1; // ultimo fonema del gruppo
        while (g1 > g0 && ph[(size_t) g1].isPause) --g1;

        double pauseSec = 0.0;
        while (i < n && ph[(size_t) i].isPause)
        {
            pauseSec += (ph[(size_t) i].end - ph[(size_t) i].start) / sampleRate;
            ++i;
        }

        std::vector<int> vowelIdx;
        for (int k = g0; k <= g1; ++k)
            if (ph[(size_t) k].isVowel) vowelIdx.push_back (k);

        std::vector<int> starts { g0 };
        for (size_t v = 0; v + 1 < vowelIdx.size(); ++v)
        {
            const int a = vowelIdx[v], b = vowelIdx[v + 1];
            int boundary = -1;
            for (int k = a + 1; k <= b; ++k)
                if (isWordStart[(size_t) k]) { boundary = k; break; }

            if (boundary < 0)
            {
                std::vector<int> cons;
                for (int k = a + 1; k < b; ++k) if (! ph[(size_t) k].isPause) cons.push_back (k);

                if (cons.size() <= 1)
                    boundary = cons.empty() ? b : cons.front();
                else
                {
                    // Gruppo ostruente + liquida/semivocale (pr, tr, bl, kw, dj...) resta unito alla vocale seguente
                    const auto c1 = stripMarks (ph[(size_t) cons[cons.size() - 2]].name);
                    const auto c2 = stripMarks (ph[(size_t) cons.back()].name);
                    const bool c2Liquid = juce::String::fromUTF8 ("rlljw\xC9\xBE\xCA\x8E\xC9\xB9").containsChar (c2[0]);
                    const bool c1Sonorant = juce::String::fromUTF8 ("rlmn\xC9\xB2\xC5\x8B\xC9\xBEs").containsChar (c1[0]);
                    if (cons.size() == 2 && c2Liquid && ! c1Sonorant)
                        boundary = cons.front();
                    else
                        boundary = cons[1]; // la prima consonante resta con la sillaba precedente
                }
            }
            starts.push_back (boundary);
        }

        for (size_t s = 0; s < starts.size(); ++s)
        {
            const int first = starts[s];
            const int last = (s + 1 < starts.size()) ? starts[s + 1] - 1 : g1;
            if (last < first) continue;

            Syllable syl;
            syl.start = ph[(size_t) first].start;
            syl.end = ph[(size_t) last].end;
            syl.wordStart = (s == 0) || isWordStart[(size_t) first];
            for (int k = first; k <= last; ++k)
                if (! ph[(size_t) k].isPause)
                    syl.label << stripMarks (ph[(size_t) k].name);

            if (s + 1 == starts.size())
            {
                syl.end = trimTail (syl.start, syl.end);
                syl.pauseAfterSec = pauseSec;
            }

            if (syl.end - syl.start > 32)
                out.push_back (syl);
        }
    }

    return out;
}

} // namespace vocalis
