#include "Quantizer.h"
#include "TimeStretch.h"
#include <cmath>

namespace vocalis
{

juce::StringArray RhythmSettings::divisionNames()
{
    return { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
}

double RhythmSettings::divisionToBeats (int i)
{
    switch (i)
    {
        case 0:  return 1.0;
        case 1:  return 0.5;
        case 2:  return 1.0 / 3.0;
        case 3:  return 0.25;
        case 4:  return 1.0 / 6.0;
        case 5:  return 0.125;
        default: return 0.25;
    }
}

namespace
{
    void applyFades (std::vector<float>& v, int fadeIn, int fadeOut)
    {
        const int n = (int) v.size();
        fadeIn = juce::jmin (fadeIn, n / 2);
        fadeOut = juce::jmin (fadeOut, n / 2);
        for (int i = 0; i < fadeIn; ++i)  v[(size_t) i] *= (float) i / (float) fadeIn;
        for (int i = 0; i < fadeOut; ++i) v[(size_t) (n - 1 - i)] *= (float) i / (float) fadeOut;
    }

    /** Allunga/accorcia solo il nucleo vocalico: le consonanti (attacco e coda) restano intatte. */
    std::vector<float> stretchVowel (const float* src, int srcLen, const Syllable& s, int targetLen, double sr)
    {
        const int start = juce::jlimit (0, srcLen, s.start), end = juce::jlimit (start, srcLen, s.end);
        const int nucS = juce::jlimit (start, end, s.nucleusStart), nucE = juce::jlimit (nucS, end, s.nucleusEnd);
        const int onset = nucS - start, coda = end - nucE, nucleus = nucE - nucS;
        const int xf = juce::jmin ((int) (0.003 * sr), coda, nucleus / 2);   // dissolvenza nucleo -> coda

        const int targetNucleus = targetLen - onset - coda;
        if (nucleus < 16 || targetNucleus < (int) (nucleus * 0.35))
        {
            // non c'e' spazio: comprimi tutta la sillaba
            return TimeStretch::process (src + start, end - start, juce::jmax (16, targetLen), sr);
        }

        std::vector<float> out ((size_t) (onset + targetNucleus + coda), 0.0f);
        std::copy (src + start, src + nucS, out.begin());

        const auto nuc = TimeStretch::process (src + nucS, nucleus + xf, targetNucleus + xf, sr);
        for (size_t i = 0; i < nuc.size() && onset + (int) i < (int) out.size(); ++i)
        {
            float g = 1.0f;
            const int fromEnd = (int) nuc.size() - 1 - (int) i;
            if (fromEnd < xf && xf > 0) g = (float) fromEnd / (float) xf;
            out[(size_t) onset + i] += nuc[i] * g;
        }
        for (int i = 0; i < coda; ++i)
        {
            const size_t o = (size_t) (onset + targetNucleus + i);
            if (o >= out.size()) break;
            const float g = (i < xf && xf > 0) ? (float) i / (float) xf : 1.0f;
            out[o] += src[nucE + i] * g;
        }
        return out;
    }
}

QuantizedClip Quantizer::process (const SynthResult& voice, const RhythmSettings& rs, const GrooveTemplate& groove,
                                  const std::vector<SyllableEdit>& edits)
{
    QuantizedClip clip;
    const double sr = voice.sampleRate;
    const double samplesPerBeat = 60.0 / juce::jmax (20.0, rs.bpm) * sr;
    const double barBeats = juce::jmax (1, rs.beatsPerBar);
    const float* src = voice.audio.getReadPointer (0);
    const int srcLen = voice.audio.getNumSamples();
    const int numSyl = (int) voice.syllables.size();
    const double pc = juce::jlimit (0.0, 1.0, rs.pCenter);
    const bool grid = rs.mode == RhythmSettings::Mode::Grid;

    auto edit = [&] (int i) -> SyllableEdit { return i < (int) edits.size() ? edits[(size_t) i] : SyllableEdit {}; };

    // ---------------------------------------------------------------- 1) dove cade ogni sillaba
    struct Place { double anchorBeat, gridBeat, slotBeats; float gain; bool contiguous; };
    std::vector<Place> places ((size_t) numSyl);

    int step = 0;
    for (int k = 0; k < numSyl; ++k)
    {
        const auto& syl = voice.syllables[(size_t) k];
        const auto e = edit (k);
        auto& p = places[(size_t) k];
        p.gain = juce::Decibels::decibelsToGain (e.gainDb);

        if (grid)
        {
            p.gridBeat = step * rs.stepBeats;
            double off = (step % 2 == 1) ? (2.0 * rs.swing - 1.0) * rs.stepBeats : 0.0;
            if (! groove.isEmpty())
            {
                double gOff; float vel;
                groove.lookup (p.gridBeat, gOff, vel);
                off = off * (1.0 - rs.grooveAmount) + gOff * rs.grooveAmount;
                p.gain *= 1.0f - (float) rs.accentAmount * (1.0f - vel);
            }
            p.anchorBeat = p.gridBeat + off + e.shiftBeats;
            p.slotBeats = rs.stepsPerSyllable * rs.stepBeats * e.lengthScale;

            step += rs.stepsPerSyllable;
            p.contiguous = syl.pauseAfterSec <= 0.0;
            if (syl.pauseAfterSec > 0.0)
                step += juce::jlimit (1, juce::jmax (1, rs.restStepsMax),
                                      (int) std::round (syl.pauseAfterSec * sr / (rs.stepBeats * samplesPerBeat)));
        }
        else
        {
            // libero: tempi naturali; le modifiche manuali spostano e allungano
            const double natAnchor = (syl.start + pc * syl.onsetLength()) / samplesPerBeat;
            p.gridBeat = natAnchor;
            p.anchorBeat = natAnchor + e.shiftBeats;
            p.slotBeats = (syl.end - syl.start) / samplesPerBeat * e.lengthScale;
            p.contiguous = false;
        }
    }

    const int fadeIn = (int) (0.001 * sr), fadeOut = (int) (0.005 * sr);
    auto startOf = [&] (int k)
    {
        const auto& s = voice.syllables[(size_t) k];
        return (int) std::round (places[(size_t) k].anchorBeat * samplesPerBeat - pc * s.onsetLength());
    };

    // ---------------------------------------------------------------- 2) taglio, stretch e mix
    std::vector<float> out;

    if (! grid)
    {
        // Libero: si parte dall'audio originale intatto e si riposizionano solo le sillabe modificate
        out.assign (src, src + srcLen);
        const int xf = (int) (0.003 * sr);
        for (int k = 0; k < numSyl; ++k)
        {
            const auto& syl = voice.syllables[(size_t) k];
            const auto e = edit (k);
            ClipMarker m;
            m.label = syl.label; m.syllableIndex = k; m.gridBeat = places[(size_t) k].gridBeat; m.muted = e.muted;
            m.start = syl.start; m.end = syl.end; m.nucleus = syl.nucleusStart;

            if (! e.isDefault())
            {
                // toglie la sillaba originale con brevi dissolvenze
                for (int i = juce::jmax (0, syl.start); i < juce::jmin (srcLen, syl.end); ++i)
                {
                    const int a = i - syl.start, b = syl.end - 1 - i;
                    const float keep = a < xf ? 1.0f - (float) a / xf : (b < xf ? 1.0f - (float) b / xf : 0.0f);
                    out[(size_t) i] *= keep;
                }
                if (! e.muted)
                {
                    const int len = (int) std::round (places[(size_t) k].slotBeats * samplesPerBeat);
                    auto piece = rs.stretch == RhythmSettings::Stretch::Off
                                   ? std::vector<float> (src + syl.start, src + syl.end)
                                   : stretchVowel (src, srcLen, syl, juce::jmax (16, len), sr);
                    applyFades (piece, xf, xf);
                    const int startPos = juce::jmax (0, startOf (k));
                    if (startPos + (int) piece.size() > (int) out.size())
                        out.resize ((size_t) (startPos + (int) piece.size()), 0.0f);
                    const float g = juce::Decibels::decibelsToGain (e.gainDb);
                    for (size_t i = 0; i < piece.size(); ++i) out[(size_t) startPos + i] += piece[i] * g;
                    m.start = startPos; m.end = startPos + (int) piece.size(); m.nucleus = startPos + syl.onsetLength();
                }
            }
            clip.markers.push_back (m);
        }
        const double usedBeats = out.size() / samplesPerBeat;
        clip.lengthBeats = juce::jmax (barBeats, std::ceil (usedBeats / barBeats - 1.0e-6) * barBeats);
        const int total = juce::jmax ((int) out.size(), (int) std::ceil (clip.lengthBeats * samplesPerBeat));
        clip.audio.setSize (1, total);
        clip.audio.clear();
        if (! out.empty()) clip.audio.copyFrom (0, 0, out.data(), (int) out.size());
        return clip;
    }

    auto mixAt = [&out] (int pos, const std::vector<float>& v, float gain)
    {
        if (pos + (int) v.size() > (int) out.size())
            out.resize ((size_t) (pos + (int) v.size()), 0.0f);
        for (size_t i = 0; i < v.size(); ++i)
            out[(size_t) pos + i] += v[i] * gain;
    };


    for (int k = 0; k < numSyl; ++k)
    {
        const auto& syl = voice.syllables[(size_t) k];
        const auto& p = places[(size_t) k];
        const auto e = edit (k);

        int startPos = startOf (k);
        const bool clamped = startPos < 0;
        startPos = juce::jmax (0, startPos);

        const int natural = juce::jlimit (0, srcLen, syl.end) - juce::jlimit (0, srcLen, syl.start);
        if (natural <= 16) continue;

        ClipMarker m;
        m.label = syl.label;
        m.syllableIndex = k;
        m.gridBeat = p.gridBeat;
        m.muted = e.muted;

        // lunghezza disponibile: fino all'ancora + slot*legato, senza invadere la sillaba seguente
        int len;
        if (grid)
        {
            const int anchor = (int) std::round (p.anchorBeat * samplesPerBeat);
            int endPos = anchor + (int) std::round (p.slotBeats * rs.legato * samplesPerBeat);
            if (p.contiguous && k + 1 < numSyl)
            {
                // non invadere la sillaba seguente, ma solo se viene davvero dopo (l'utente puo' averle scambiate)
                const int next = startOf (k + 1);
                if (next > anchor + (int) (0.03 * sr))
                    endPos = juce::jmin (endPos, next);
            }
            len = juce::jmax ((int) (0.02 * sr), endPos - startPos);
        }
        else
            len = (int) std::round (p.slotBeats * samplesPerBeat);

        std::vector<float> piece;
        const bool needsStretch = grid || std::abs (e.lengthScale - 1.0) > 1e-6;
        if (! needsStretch || rs.stretch == RhythmSettings::Stretch::Off)
        {
            const int s0 = juce::jlimit (0, srcLen, syl.start);
            piece.assign (src + s0, src + s0 + natural);
            if (grid && (int) piece.size() > len) piece.resize ((size_t) len);
        }
        else if (rs.stretch == RhythmSettings::Stretch::Vowels)
        {
            // il nucleo si allunga fino a 6x, oltre si lascia una pausa
            const int maxLen = syl.onsetLength() + syl.codaLength() + (syl.nucleusEnd - syl.nucleusStart) * 6;
            piece = stretchVowel (src, srcLen, syl, juce::jmin (len, juce::jmax (maxLen, 16)), sr);
        }
        else
        {
            const int target = juce::jlimit ((int) (natural * 0.4), (int) (natural * 2.5), len);
            const int s0 = juce::jlimit (0, srcLen, syl.start);
            piece = TimeStretch::process (src + s0, natural, target, sr);
            if ((int) piece.size() > len) piece.resize ((size_t) len);
        }

        applyFades (piece, clamped ? fadeIn * 4 : fadeIn, fadeOut);

        m.start = startPos;
        m.end = startPos + (int) piece.size();
        m.nucleus = juce::jlimit (m.start, m.end, startPos + (needsStretch && rs.stretch == RhythmSettings::Stretch::Whole
                                                                   ? (int) (syl.onsetLength() * (double) piece.size() / natural)
                                                                   : syl.onsetLength()));
        if (! e.muted)
            mixAt (startPos, piece, p.gain);
        clip.markers.push_back (m);
    }

    // ---------------------------------------------------------------- 3) lunghezza in battute intere
    double usedBeats = out.size() / samplesPerBeat;
    if (grid) usedBeats = juce::jmax (usedBeats, step * rs.stepBeats);
    clip.lengthBeats = juce::jmax (barBeats, std::ceil (usedBeats / barBeats - 1.0e-6) * barBeats);
    const int total = juce::jmax ((int) out.size(), (int) std::ceil (clip.lengthBeats * samplesPerBeat));

    clip.audio.setSize (1, total);
    clip.audio.clear();
    if (! out.empty())
        clip.audio.copyFrom (0, 0, out.data(), (int) out.size());
    return clip;
}

} // namespace vocalis
