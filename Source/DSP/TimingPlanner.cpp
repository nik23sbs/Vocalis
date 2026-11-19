#include "TimingPlanner.h"
#include <cmath>
#include <numeric>

namespace vocalis
{

namespace
{
    bool isPunct (juce::juce_wchar c) { return juce::String (juce::CharPointer_UTF8 (";:,.!?—…\"()“”«»")).containsChar (c); }
    bool isStress (juce::juce_wchar c) { return c == 0x02C8 || c == 0x02CC; }
    int naturalFrames (int64_t predicted, float scale) { return (int) std::lround ((double) predicted * scale); }
}

std::vector<SyllableTarget> TimingPlanner::plan (const std::vector<TokenSyllable>& sylls, const RhythmSettings& rs,
                                                 const GrooveTemplate& groove, const std::vector<SyllableEdit>& edits,
                                                 VocalMode mode, const std::vector<NoteEvent>& notes)
{
    std::vector<SyllableTarget> out;
    if (mode == VocalMode::Parlato || sylls.empty())
        return out;

    const double step = juce::jmax (1.0 / 64.0, rs.stepBeats);
    const double slot = step * juce::jmax (1, rs.stepsPerSyllable);
    const double minGap = 0.05 * juce::jmax (20.0, rs.bpm) / 60.0;      // 50 ms tra due vocali
    auto edit = [&] (size_t i) { return i < edits.size() ? edits[i] : SyllableEdit {}; };

    out.resize (sylls.size());
    double cursor = 0.0;
    const bool useNotes = mode == VocalMode::Canto && ! notes.empty();
    if (useNotes) cursor = notes.back().startBeat + notes.back().lengthBeats;

    for (size_t k = 0; k < sylls.size(); ++k)
    {
        auto& t = out[k];
        const auto e = edit (k);
        double off = 0.0;
        if (useNotes && k < notes.size())
        {
            t.gridBeat = notes[k].startBeat;
            t.endBeat = notes[k].startBeat + juce::jmax (step * 0.5, notes[k].lengthBeats) * e.lengthScale;
            t.noteMidi = notes[k].pitch;
        }
        else
        {
            t.gridBeat = cursor;
            const int idx = (int) std::lround (cursor / step);
            if (idx % 2 == 1) off = (2.0 * rs.swing - 1.0) * step;
            if (! groove.isEmpty())
            {
                double gOff; float vel;
                groove.lookup (cursor, gOff, vel);
                off = off * (1.0 - rs.grooveAmount) + gOff * rs.grooveAmount;
                t.gainDb = juce::Decibels::gainToDecibels (1.0f - (float) rs.accentAmount * (1.0f - vel), -24.0f);
            }
            const double mySlot = k + 1 == sylls.size() ? juce::jmax (slot, 2.0 * step) : slot;
            const double legato = mode == VocalMode::Canto ? 1.0 : juce::jlimit (0.1, 1.0, rs.legato);
            t.endBeat = t.gridBeat + off + mySlot * legato * e.lengthScale;
            t.noteMidi = mode == VocalMode::Canto && useNotes ? notes.back().pitch : -1;
            cursor += slot + step * juce::jmin (sylls[k].pauseSteps, juce::jmax (0, rs.restStepsMax));
        }
        if (e.note >= 0) t.noteMidi = e.note;
        t.anchorBeat = t.gridBeat + off + e.shiftBeats;
        t.endBeat += e.shiftBeats;
        t.gainDb += e.gainDb;
        t.muted = e.muted;

        if (k > 0 && t.anchorBeat < out[k - 1].anchorBeat + minGap)
        {
            const double shift = out[k - 1].anchorBeat + minGap - t.anchorBeat;
            t.anchorBeat += shift;
            t.endBeat += shift;
        }
        t.endBeat = juce::jmax (t.endBeat, t.anchorBeat + minGap);
    }
    return out;
}

double TimingPlanner::prerollBeats (const std::vector<int64_t>& predicted, const std::vector<TokenSyllable>& sylls,
                                    const std::vector<SyllableTarget>& targets, double bpm)
{
    if (sylls.empty() || targets.empty() || predicted.empty()) return 0.0;
    const double F = framesPerSecond * 60.0 / juce::jmax (20.0, bpm);
    double lead = 2.0;
    for (int i = 0; i < sylls[0].nucleusFirst && i + 1 < (int) predicted.size(); ++i)
        lead += juce::jmax (1, naturalFrames (predicted[(size_t) i + 1], 1.0f));
    const double need = lead / F - targets[0].anchorBeat;
    return need <= 1.0e-9 ? 0.0 : std::ceil (need - 1.0e-9);
}

TimedDurations TimingPlanner::durations (const juce::String& ps, const std::vector<int64_t>& pred,
                                         const std::vector<TokenSyllable>& sylls, const std::vector<SyllableTarget>& targets,
                                         const std::vector<SyllableEdit>& edits, double bpm, double startBeat)
{
    TimedDurations r;
    const int n = ps.length();
    if ((int) pred.size() != n + 2 || sylls.empty() || targets.size() != sylls.size())
    {
        r.durations = pred;
        return r;
    }
    const double F = framesPerSecond * 60.0 / juce::jmax (20.0, bpm);
    auto edit = [&] (size_t i) { return i < edits.size() ? edits[i] : SyllableEdit {}; };
    auto& d = r.durations;
    d.assign ((size_t) n + 2, 0);
    auto P = [&] (int token) { return pred[(size_t) token + 1]; };
    auto& D = d;   // D[token + 1]

    auto consonantFrames = [&] (int token, float scale) -> int
    {
        const auto c = ps[token];
        if (isPunct (c)) return 0;
        if (isStress (c)) return juce::jmax (0, naturalFrames (P (token), 1.0f));
        return juce::jmax (1, naturalFrames (P (token), scale));
    };

    // ---- inizio: silenzio del modello (2 frame) + attacco naturale della prima sillaba
    D[0] = 2;
    int pos = 2;
    const float scale0 = edit (0).onsetScale;
    for (int i = 0; i < sylls[0].nucleusFirst; ++i) { D[(size_t) i + 1] = consonantFrames (i, scale0); pos += (int) D[(size_t) i + 1]; }

    const double first = (targets[0].anchorBeat - startBeat) * F;
    r.padSamples = juce::jmax (0.0, (first - pos) * 600.0);
    const double padFrames = r.padSamples / 600.0;

    r.targetFrame.resize (sylls.size());
    r.vowelFrame.resize (sylls.size());
    r.endFrame.resize (sylls.size());
    for (size_t k = 0; k < sylls.size(); ++k)
        r.targetFrame[k] = (targets[k].anchorBeat - startBeat) * F - padFrames;

    for (size_t k = 0; k < sylls.size(); ++k)
    {
        const auto& s = sylls[k];
        r.vowelFrame[k] = pos;
        const bool last = k + 1 == sylls.size();
        const int consFrom = s.nucleusLast + 1;
        const int consTo = last ? n : sylls[k + 1].nucleusFirst;       // [consFrom, consTo)
        const float nextScale = last ? 1.0f : edit (k + 1).onsetScale;

        std::vector<int> cons;
        std::vector<int> punct;
        for (int i = consFrom; i < consTo; ++i)
        {
            const bool inNextOnset = ! last && i >= sylls[k + 1].firstToken;
            cons.push_back (consonantFrames (i, inNextOnset ? nextScale : 1.0f));
            if (isPunct (ps[i])) punct.push_back (i);
        }
        std::vector<int> vowels;
        for (int i = s.nucleusFirst; i <= s.nucleusLast; ++i) vowels.push_back (i);
        int vmin = 0;
        for (int v : vowels) if (TokenSyllables::isVowel (ps[v])) ++vmin;

        const int voiceFrames = juce::jmax (vmin, (int) std::lround ((targets[k].endBeat - targets[k].anchorBeat) * F));
        int want;
        if (! last)
            want = juce::jmax (vmin, (int) std::lround (r.targetFrame[k + 1]) - pos);
        else
        {
            int c = 0; for (auto x : cons) c += x;
            want = voiceFrames + c + (punct.empty() ? 0 : 4);
        }

        int sumCons = std::accumulate (cons.begin(), cons.end(), 0);
        while (sumCons + vmin > want)
        {
            auto it = std::max_element (cons.begin(), cons.end());
            if (it == cons.end() || *it <= 1) break;
            --*it; --sumCons;
        }
        const int rest = juce::jmax (vmin, want - sumCons);
        int vowelTotal = rest, pauseTotal = 0;
        if (! punct.empty())
        {
            vowelTotal = juce::jlimit (vmin, rest, voiceFrames);
            pauseTotal = rest - vowelTotal;
        }

        // vocali: ripartite in proporzione alle durate naturali (le lunghezze ː incluse)
        double wsum = 0.0;
        for (int v : vowels) wsum += (double) P (v) + 1.0e-3;
        int assigned = 0;
        for (size_t j = 0; j < vowels.size(); ++j)
        {
            const int v = vowels[j];
            int f = (int) std::floor (vowelTotal * ((double) P (v) + 1.0e-3) / wsum);
            if (TokenSyllables::isVowel (ps[v])) f = juce::jmax (1, f);
            D[(size_t) v + 1] = f;
            assigned += f;
        }
        // il resto dell'arrotondamento va alla prima vocale
        for (int v : vowels) if (TokenSyllables::isVowel (ps[v])) { D[(size_t) v + 1] += juce::jmax (0, vowelTotal - assigned); break; }

        for (int i = consFrom; i < consTo; ++i) D[(size_t) i + 1] = cons[(size_t) (i - consFrom)];
        for (int i : punct) D[(size_t) i + 1] = pauseTotal / (int) punct.size();
        if (! punct.empty()) D[(size_t) punct.back() + 1] += pauseTotal % (int) punct.size();

        int span = 0;
        for (int i = s.nucleusFirst; i < consTo; ++i) span += (int) D[(size_t) i + 1];
        r.endFrame[k] = pos + juce::jlimit (vmin, juce::jmax (vmin, span - pauseTotal), voiceFrames);
        pos += span;
    }
    D[(size_t) n + 1] = 3;

    r.onsetFrame.resize (sylls.size());
    int acc = (int) D[0];
    for (int i = 0, k = 0; i < n && k < (int) sylls.size(); ++i)
    {
        if (i == sylls[(size_t) k].firstToken) r.onsetFrame[(size_t) k++] = acc;
        acc += (int) D[(size_t) i + 1];
    }
    return r;
}

} // namespace vocalis
