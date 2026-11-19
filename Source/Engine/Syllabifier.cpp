#include "Syllabifier.h"
#include <cmath>
#include <algorithm>

namespace vocalis
{

// ============================================================================ EnergyEnvelope

EnergyEnvelope::EnergyEnvelope (const juce::AudioBuffer<float>& audio, double sr)
{
    numSamples = audio.getNumSamples();
    if (numSamples == 0) return;
    data = audio.getReadPointer (0);
    hop = juce::jmax (8, (int) (0.0025 * sr));       // 2.5 ms
    const int win = hop * 2;                          // 5 ms
    const int frames = numSamples / hop + 1;
    db.resize ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        const int a = juce::jmax (0, f * hop - win / 2), b = juce::jmin (numSamples, f * hop + win / 2);
        double e = 0;
        for (int i = a; i < b; ++i) e += data[i] * data[i];
        e /= juce::jmax (1, b - a);
        db[(size_t) f] = (float) (10.0 * std::log10 (e + 1e-10));
    }
}

float EnergyEnvelope::dbAt (int s) const
{
    if (db.empty()) return -100.0f;
    return db[(size_t) juce::jlimit (0, (int) db.size() - 1, s / hop)];
}

int EnergyEnvelope::maxRise (int centre, int radius) const
{
    if (db.size() < 3) return centre;
    const int f0 = juce::jmax (1, (centre - radius) / hop), f1 = juce::jmin ((int) db.size() - 2, (centre + radius) / hop);
    int best = centre; float bestSlope = -1e9f;
    for (int f = f0; f <= f1; ++f)
    {
        const float slope = db[(size_t) f + 1] - db[(size_t) f - 1];
        if (slope > bestSlope) { bestSlope = slope; best = f * hop; }
    }
    return bestSlope > 1.0f ? best : centre;   // nessuna salita evidente: tieni il confine del modello
}

int EnergyEnvelope::minEnergy (int centre, int radius) const
{
    if (db.empty()) return centre;
    const int f0 = juce::jmax (0, (centre - radius) / hop), f1 = juce::jmin ((int) db.size() - 1, (centre + radius) / hop);
    int best = centre; float bestDb = 1e9f;
    for (int f = f0; f <= f1; ++f)
    {
        // a parita' di energia preferisci il punto piu' vicino al confine originale
        const float score = db[(size_t) f] + 0.02f * std::abs (f * hop - centre) / (float) hop;
        if (score < bestDb) { bestDb = score; best = f * hop; }
    }
    return best;
}

int EnergyEnvelope::nearestZeroCrossing (int pos, int radius) const
{
    if (data == nullptr) return pos;
    for (int d = 0; d <= radius; ++d)
    {
        for (int s : { pos - d, pos + d })
            if (s > 0 && s < numSamples && ((data[s - 1] <= 0.0f && data[s] > 0.0f) || (data[s - 1] >= 0.0f && data[s] < 0.0f)))
                return s;
    }
    return pos;
}

// ============================================================================ Nomi dei fonemi

juce::String Syllabifier::stripMarks (const juce::String& s)
{
    juce::String out;
    for (auto c : s)
    {
        // accenti (ˈ ˌ), lunghezza (ː ˑ), spazi, trattini
        if (c == 0x02C8 || c == 0x02CC || c == 0x02D0 || c == 0x02D1 || c == ' ' || c == '-' || c == '\'' || c == ',')
            continue;
        out << juce::String::charToString (c);
    }
    return out;
}

bool Syllabifier::isStressed (const juce::String& ipa)
{
    return ipa.containsChar (0x02C8) || ipa.containsChar (0x02CC);
}

bool Syllabifier::isPauseName (const juce::String& ipa)
{
    const auto s = stripMarks (ipa);
    return s.isEmpty() || s.containsOnly (juce::String::fromUTF8 ("_:|;.!?\xE2\x80\x94\xE2\x80\xA6\"()\xE2\x80\x9C\xE2\x80\x9D"));
}

bool Syllabifier::isVowelName (const juce::String& ipa)
{
    static const juce::String vowels = juce::String::fromUTF8 (
        "aeiouyAEIOUYQW\xC9\x91\xC9\x90\xC9\x92\xC3\xA6\xC9\x94\xC9\x99\xC9\x98\xC9\x9A\xC9\x9B\xC9\x9C\xC9\x9D\xC9\x9E"
        "\xC9\xA4\xC9\xA8\xC9\xAA\xC9\xAF\xC9\xB5\xC3\xB8\xC5\x93\xC9\xB6\xCA\x89\xCA\x8A\xCA\x8C\xCA\x8F"
        "\xC3\xA0\xC3\xA8\xC3\xA9\xC3\xAC\xC3\xB2\xC3\xB9\xE1\xB5\xBB\xE1\xB5\x8A");
    const auto s = stripMarks (ipa);
    if (s.isEmpty())
        return false;
    return vowels.indexOfChar (s[0]) >= 0;
}

// ============================================================================ Sillabe

std::vector<Syllable> Syllabifier::build (const std::vector<Phoneme>& ph, const juce::AudioBuffer<float>& audio, double sr)
{
    std::vector<Syllable> out;
    const int n = (int) ph.size();
    if (n == 0)
        return out;

    const EnergyEnvelope env (audio, sr);
    const int minPause = (int) (0.03 * sr);
    auto isRealPause = [&] (int i) { return ph[(size_t) i].isPause && (ph[(size_t) i].end - ph[(size_t) i].start) >= minPause; };

    // Fine sillaba prima di una pausa: taglia il silenzio in coda
    const float* data = audio.getNumSamples() > 0 ? audio.getReadPointer (0) : nullptr;
    auto trimTail = [&] (int start, int end)
    {
        if (data == nullptr) return end;
        const float thr = 0.003f;
        int e = juce::jmin (end, audio.getNumSamples());
        while (e > start + 64 && std::abs (data[e - 1]) < thr) --e;
        return juce::jmin (end, e + (int) (0.012 * sr));
    };

    int i = 0;
    while (i < n)
    {
        while (i < n && ph[(size_t) i].isPause) ++i;
        if (i >= n) break;

        const int g0 = i;
        while (i < n && ! isRealPause (i)) ++i;
        int g1 = i - 1;
        while (g1 > g0 && ph[(size_t) g1].isPause) --g1;

        double pauseSec = 0.0;
        while (i < n && ph[(size_t) i].isPause)
        {
            pauseSec += (ph[(size_t) i].end - ph[(size_t) i].start) / sr;
            ++i;
        }

        std::vector<int> vowelIdx, geminates;
        for (int k = g0; k <= g1; ++k)
            if (ph[(size_t) k].isVowel) vowelIdx.push_back (k);

        // Confini tra le vocali
        std::vector<int> starts { g0 };
        for (size_t v = 0; v + 1 < vowelIdx.size(); ++v)
        {
            const int a = vowelIdx[v], b = vowelIdx[v + 1];
            int boundary = -1;
            for (int k = a + 1; k <= b; ++k)
                if (ph[(size_t) k].wordStart) { boundary = k; break; }

            if (boundary < 0)
            {
                std::vector<int> cons;
                for (int k = a + 1; k < b; ++k) if (! ph[(size_t) k].isPause) cons.push_back (k);

                if (cons.empty())
                {
                    // dittongo discendente: vocale accentata + atona nella stessa parola -> una sola sillaba
                    if (isStressed (ph[(size_t) a].name) && ! isStressed (ph[(size_t) b].name))
                        continue;
                    boundary = b;
                }
                else if (cons.size() == 1)
                {
                    boundary = cons.front();
                    // doppia (tː, kː...): la consonante lunga si divide tra le due sillabe (tut-ti)
                    if (ph[(size_t) boundary].name.containsChar (0x02D0))
                        geminates.push_back (boundary);
                }
                else
                {
                    const auto c1 = stripMarks (ph[(size_t) cons[cons.size() - 2]].name);
                    const auto c2 = stripMarks (ph[(size_t) cons.back()].name);
                    const bool c2Liquid = c2.isNotEmpty() && juce::String::fromUTF8 ("rljw\xC9\xBE\xCA\x8E\xC9\xB9").containsChar (c2[0]);
                    const bool c1Sonorant = c1.isNotEmpty() && juce::String::fromUTF8 ("rlmn\xC9\xB2\xC5\x8B\xC9\xBEs").containsChar (c1[0]);
                    boundary = (cons.size() == 2 && c2Liquid && ! c1Sonorant) ? cons.front() : cons[1];
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
            if (std::find (geminates.begin(), geminates.end(), first) != geminates.end())
            {
                syl.start = (ph[(size_t) first].start + ph[(size_t) first].end) / 2;
                if (! out.empty() && out.back().end == ph[(size_t) first].start)
                    out.back().end = syl.start;
            }
            syl.end = ph[(size_t) last].end;
            syl.wordStart = (s == 0) || ph[(size_t) first].wordStart;

            int nucFirst = -1, nucLast = -1;
            for (int k = first; k <= last; ++k)
            {
                if (! ph[(size_t) k].isPause) syl.label << stripMarks (ph[(size_t) k].name);
                if (ph[(size_t) k].isVowel) { if (nucFirst < 0) nucFirst = k; nucLast = k; }
            }
            if (nucFirst >= 0)
            {
                syl.nucleusStart = ph[(size_t) nucFirst].start;
                syl.nucleusEnd = ph[(size_t) nucLast].end;
            }
            else
            {
                syl.nucleusStart = syl.start;
                syl.nucleusEnd = syl.end;
            }

            if (s + 1 == starts.size())
            {
                syl.end = trimTail (syl.start, syl.end);
                syl.pauseAfterSec = pauseSec;
            }
            if (syl.end - syl.start > 32)
                out.push_back (syl);
        }
    }

    // ---- Rifinitura sull'audio
    const int cutRadius = (int) (0.008 * sr), zcRadius = (int) (0.0015 * sr), onsetRadius = (int) (0.015 * sr);
    for (size_t k = 0; k < out.size(); ++k)
    {
        auto& s = out[k];
        // confine condiviso con la sillaba precedente: minimo di energia + zero crossing
        if (k > 0 && out[k - 1].end == s.start)
        {
            int cut = env.minEnergy (s.start, cutRadius);
            cut = env.nearestZeroCrossing (cut, zcRadius);
            cut = juce::jlimit (out[k - 1].nucleusEnd, s.nucleusStart, cut);
            out[k - 1].end = cut;
            s.start = cut;
        }
        // attacco della vocale (P-center)
        if (s.nucleusStart > s.start)
        {
            const int r = juce::jmin (onsetRadius, (s.nucleusEnd - s.nucleusStart) / 2);
            s.nucleusStart = juce::jlimit (s.start, s.nucleusEnd - 1, env.maxRise (s.nucleusStart, r));
        }
        s.nucleusStart = juce::jlimit (s.start, s.end, s.nucleusStart);
        s.nucleusEnd = juce::jlimit (s.nucleusStart, s.end, s.nucleusEnd);
    }
    return out;
}

} // namespace vocalis
