#include "PitchPlanner.h"
#include <algorithm>
#include <cmath>

namespace vocalis
{

namespace
{
    constexpr float secPerSample = 0.0125f;   // 2 valori per frame da 25 ms

    struct Range { int start, vowel, vowelEnd, end; };   // in campioni F0

    std::vector<Range> syllableRanges (const std::vector<int64_t>& d, const std::vector<TokenSyllable>& sylls, int total)
    {
        std::vector<int> cum (d.size() + 1, 0);
        for (size_t i = 0; i < d.size(); ++i) cum[i + 1] = cum[i] + (int) d[i];
        auto tokenStart = [&] (int token) { return 2 * cum[(size_t) juce::jlimit (0, (int) d.size() - 1, token + 1)]; };
        std::vector<Range> r;
        for (size_t k = 0; k < sylls.size(); ++k)
        {
            Range x;
            x.start = k == 0 ? 0 : tokenStart (sylls[k].firstToken);
            x.vowel = tokenStart (sylls[k].nucleusFirst);
            x.end = k + 1 == sylls.size() ? total : tokenStart (sylls[k + 1].firstToken);
            x.vowelEnd = tokenStart (sylls[k].nucleusLast + 1);
            x.start = juce::jlimit (0, total, x.start); x.vowel = juce::jlimit (x.start, total, x.vowel); x.end = juce::jlimit (x.vowel, total, x.end);
            x.vowelEnd = juce::jlimit (x.vowel, x.end, x.vowelEnd);
            r.push_back (x);
        }
        return r;
    }

    bool voiced (float f) { return f > 40.0f; }   // sotto: consonanti sorde (il modello da' valori minuscoli)
}

std::vector<float> PitchPlanner::sustainEnergy (const std::vector<float>& nat, const std::vector<int64_t>& durations,
                                                const std::vector<TokenSyllable>& sylls, VocalMode mode)
{
    if (mode != VocalMode::Canto || nat.empty() || sylls.empty())
        return nat;
    const int n = (int) nat.size();
    auto ranges = syllableRanges (durations, sylls, n);

    // livello di riferimento: 80o percentile dell'energia sulle vocali
    std::vector<float> vals;
    for (auto& r : ranges) for (int i = r.vowel; i < r.vowelEnd; ++i) vals.push_back (nat[(size_t) i]);
    if (vals.empty()) return nat;
    std::sort (vals.begin(), vals.end());
    const float floorLevel = vals[(size_t) (0.8 * (double) (vals.size() - 1))] - 0.5f;

    auto out = nat;
    for (auto& r : ranges)
    {
        const int len = r.vowelEnd - r.vowel;
        if (len <= 0) continue;
        const int attack = juce::jmin (2, len / 3), release = juce::jmin (4, juce::jmax (1, len / 4));
        for (int j = 0; j < len; ++j)
        {
            const int i = r.vowel + j;
            float w = 1.0f;
            if (j < attack) w = (float) (j + 1) / (float) (attack + 1);
            if (j >= len - release) w = juce::jmin (w, (float) (len - j) / (float) (release + 1));
            const float target = juce::jmax (nat[(size_t) i], floorLevel);
            out[(size_t) i] = nat[(size_t) i] + (target - nat[(size_t) i]) * w;
        }
    }
    return out;
}

bool PitchPlanner::isIdentity (const std::vector<int>& notes, const std::vector<SyllableEdit>& edits, const PitchSettings& s)
{
    if (s.mode == VocalMode::Canto || std::abs (s.globalSemis) > 1.0e-3f || std::abs (s.intonation - 1.0f) > 1.0e-3f)
        return false;
    for (auto n : notes) if (n >= 0) return false;
    for (auto& e : edits)
        if (std::abs (e.pitchSemis) > 1.0e-3f || e.whisper || e.vibrato || e.glide || e.note >= 0) return false;
    return true;
}

float PitchPlanner::medianMidi (const std::vector<float>& f0, int from, int to)
{
    std::vector<float> m;
    for (int i = juce::jmax (0, from); i < juce::jmin ((int) f0.size(), to); ++i)
        if (voiced (f0[(size_t) i])) m.push_back (hzToMidi (f0[(size_t) i]));
    if (m.empty()) return 0.0f;
    std::nth_element (m.begin(), m.begin() + (long) m.size() / 2, m.end());
    return m[m.size() / 2];
}

int PitchPlanner::diatonicShift (int midi, int degrees, int key, bool minor)
{
    static const int major[7] { 0, 2, 4, 5, 7, 9, 11 }, minorS[7] { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = minor ? minorS : major;
    const int pc = ((midi - key) % 12 + 12) % 12;
    int d = 0;
    for (int i = 0; i < 7; ++i) if (sc[i] <= pc) d = i;
    const int base = midi - (pc - sc[d]);
    const int t = d + degrees;
    const int oct = (int) std::floor (t / 7.0);
    const int target = base + sc[((t % 7) + 7) % 7] + 12 * oct - sc[d];
    return target - midi;
}

std::vector<float> PitchPlanner::build (const std::vector<float>& nat, const std::vector<int64_t>& durations,
                                        const std::vector<TokenSyllable>& sylls, const std::vector<int>& notes,
                                        const std::vector<SyllableEdit>& edits, const PitchSettings& s)
{
    if (isIdentity (notes, edits, s) || nat.empty())
        return nat;

    const int n = (int) nat.size();
    auto ranges = syllableRanges (durations, sylls, n);
    auto edit = [&] (size_t i) { return i < edits.size() ? edits[i] : SyllableEdit {}; };

    // micro-intonazione naturale in semitoni (curva naturale meno la sua media mobile su +-200 ms)
    std::vector<float> natMidi ((size_t) n, 0.0f), smooth ((size_t) n, 0.0f);
    double sum = 0; int cnt = 0;
    for (int i = 0; i < n; ++i) if (voiced (nat[(size_t) i])) { natMidi[(size_t) i] = hzToMidi (nat[(size_t) i]); sum += natMidi[(size_t) i]; ++cnt; }
    const float mean = cnt > 0 ? (float) (sum / cnt) : 60.0f;
    for (int i = 0; i < n; ++i)
    {
        double a = 0; int c = 0;
        for (int j = juce::jmax (0, i - 16); j <= juce::jmin (n - 1, i + 16); ++j) if (voiced (nat[(size_t) j])) { a += natMidi[(size_t) j]; ++c; }
        smooth[(size_t) i] = c > 0 ? (float) (a / c) : mean;
    }

    // curva obiettivo in semitoni, per campione
    std::vector<float> midi ((size_t) n, 0.0f);
    const bool sing = s.mode == VocalMode::Canto;
    if (! sing)
    {
        for (int i = 0; i < n; ++i)
            midi[(size_t) i] = mean + (natMidi[(size_t) i] - mean) * s.intonation + s.globalSemis;
    }
    else
    {
        // nota per sillaba: dalle note, poi dalle modifiche, altrimenti la nota naturale piu' vicina
        std::vector<float> noteOf (sylls.size());
        for (size_t k = 0; k < sylls.size(); ++k)
        {
            const auto e = edit (k);
            float m = e.note >= 0 ? (float) e.note
                    : (k < notes.size() && notes[k] >= 0) ? (float) notes[k]
                    : std::round (medianMidi (nat, ranges[k].vowel, ranges[k].end) > 0 ? medianMidi (nat, ranges[k].vowel, ranges[k].end) : mean);
            noteOf[k] = m + s.globalSemis;
        }
        // la nota parte 2 frame (50 ms) prima della vocale; portamento proporzionale all'intervallo
        std::vector<int> bound (sylls.size());
        for (size_t k = 0; k < sylls.size(); ++k) bound[k] = k == 0 ? 0 : juce::jmax (ranges[k].start, ranges[k].vowel - 4);
        for (size_t k = 0; k < sylls.size(); ++k)
        {
            const int a = bound[k], b = k + 1 < sylls.size() ? bound[k + 1] : n;
            for (int i = a; i < b; ++i) midi[(size_t) i] = noteOf[k];
        }
        for (size_t k = 1; k < sylls.size(); ++k)
        {
            const float from = noteOf[k - 1], to = noteOf[k];
            const float ms = (edit (k).glide ? 200.0f : s.glideMs) + 15.0f * std::abs (to - from);
            const int g = juce::jmax (2, (int) std::lround (ms / 1000.0f / secPerSample));
            const int c = bound[k];
            for (int i = c - g / 2; i < c + g - g / 2; ++i)
            {
                if (i < 0 || i >= n) continue;
                const float t = (float) (i - (c - g / 2)) / (float) g;
                midi[(size_t) i] = from + (to - from) * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
            }
        }
        for (int i = 0; i < n; ++i)
            if (voiced (nat[(size_t) i])) midi[(size_t) i] += s.expressivity * (natMidi[(size_t) i] - smooth[(size_t) i]);
    }

    // modifiche per sillaba (trasposizione con raccordi di 50 ms) e vibrato
    std::vector<float> offset ((size_t) n, 0.0f);
    for (size_t k = 0; k < sylls.size(); ++k)
    {
        const auto e = edit (k);
        if (std::abs (e.pitchSemis) > 1.0e-3f)
            for (int i = ranges[k].start; i < ranges[k].end; ++i)
            {
                const float ramp = juce::jmin (1.0f, (float) (i - ranges[k].start + 1) / 4.0f, (float) (ranges[k].end - i) / 4.0f);
                offset[(size_t) i] += e.pitchSemis * (k == 0 ? juce::jmin (1.0f, (float) (ranges[k].end - i) / 4.0f) : ramp);
            }

        const float depth = e.vibrato ? juce::jmax (50.0f, s.vibratoCents) : s.vibratoCents;
        const int len = ranges[k].end - ranges[k].vowel;
        if (depth > 0.5f && (e.vibrato || len * secPerSample > 0.25f))
            for (int i = ranges[k].vowel; i < ranges[k].end; ++i)
            {
                const float t = (float) (i - ranges[k].vowel) * secPerSample;
                const float fade = juce::jlimit (0.0f, 1.0f, (t - 0.15f) / 0.1f);
                offset[(size_t) i] += fade * depth / 100.0f * std::sin (juce::MathConstants<float>::twoPi * s.vibratoRate * t);
            }
    }

    // nel canto le vocali sono sempre intonate (a fine frase il modello farebbe cadere la F0)
    std::vector<bool> forced ((size_t) n, false);
    if (sing)
        for (auto& rg : ranges)
            for (int i = rg.vowel; i < rg.vowelEnd; ++i) forced[(size_t) i] = true;

    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i)
        out[(size_t) i] = voiced (nat[(size_t) i]) || forced[(size_t) i] ? midiToHz (midi[(size_t) i] + offset[(size_t) i]) : nat[(size_t) i];

    for (size_t k = 0; k < sylls.size(); ++k)
        if (edit (k).whisper)
            for (int i = ranges[k].start; i < ranges[k].end; ++i) out[(size_t) i] = 0.0f;
    return out;
}

} // namespace vocalis
