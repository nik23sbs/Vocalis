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
}

QuantizedClip Quantizer::process (const SynthResult& voice, const RhythmSettings& rs, const GrooveTemplate& groove)
{
    QuantizedClip clip;
    const double sr = voice.sampleRate;
    const double secPerBeat = 60.0 / juce::jmax (20.0, rs.bpm);
    const double samplesPerBeat = secPerBeat * sr;
    const double barBeats = juce::jmax (1, rs.beatsPerBar);
    const float* src = voice.audio.getReadPointer (0);
    const int srcLen = voice.audio.getNumSamples();

    if (rs.mode == RhythmSettings::Mode::Free || voice.syllables.empty())
    {
        const double beats = srcLen / samplesPerBeat;
        clip.lengthBeats = juce::jmax (barBeats, std::ceil (beats / barBeats) * barBeats);
        const int total = (int) std::ceil (clip.lengthBeats * samplesPerBeat);
        clip.audio.setSize (1, juce::jmax (total, srcLen));
        clip.audio.clear();
        clip.audio.copyFrom (0, 0, voice.audio, 0, 0, srcLen);
        for (auto& s : voice.syllables)
            clip.markers.push_back ({ s.start, s.end, s.label });
        return clip;
    }

    const double stepBeats = rs.stepBeats;
    const double stepSamples = stepBeats * samplesPerBeat;
    const int fadeIn = (int) (0.003 * sr), fadeOut = (int) (0.008 * sr);

    std::vector<float> out;
    auto mixAt = [&out] (int pos, const std::vector<float>& v, float gain)
    {
        if (pos + (int) v.size() > (int) out.size())
            out.resize ((size_t) (pos + (int) v.size()), 0.0f);
        for (size_t i = 0; i < v.size(); ++i)
            out[(size_t) pos + i] += v[i] * gain;
    };

    int step = 0;
    for (auto& syl : voice.syllables)
    {
        const double gridBeat = step * stepBeats;

        // Swing: gli step dispari vengono ritardati
        double offsetBeats = (step % 2 == 1) ? (2.0 * rs.swing - 1.0) * stepBeats : 0.0;

        float gain = 1.0f;
        if (! groove.isEmpty())
        {
            double gOff; float vel;
            groove.lookup (gridBeat, gOff, vel);
            offsetBeats = offsetBeats * (1.0 - rs.grooveAmount) + gOff * rs.grooveAmount;
            gain = 1.0f - (float) rs.accentAmount * (1.0f - vel);
        }

        const int startPos = juce::jmax (0, (int) std::round ((gridBeat + offsetBeats) * samplesPerBeat));
        const int slot = (int) std::round (rs.stepsPerSyllable * stepSamples);

        const int s0 = juce::jlimit (0, srcLen, syl.start);
        const int s1 = juce::jlimit (s0, srcLen, syl.end);
        const int natural = s1 - s0;

        if (natural > 16)
        {
            std::vector<float> piece;
            if (rs.stretchToFit)
            {
                int target = (int) std::round (slot * rs.legato);
                target = juce::jlimit ((int) (natural * 0.4), (int) (natural * 2.5), target);
                piece = TimeStretch::process (src + s0, natural, target, sr);
            }
            else
            {
                piece.assign (src + s0, src + s1);
            }

            if ((int) piece.size() > slot)
                piece.resize ((size_t) slot);

            applyFades (piece, fadeIn, fadeOut);
            mixAt (startPos, piece, gain);
            clip.markers.push_back ({ startPos, startPos + (int) piece.size(), syl.label });
        }

        step += rs.stepsPerSyllable;

        if (syl.pauseAfterSec > 0.0)
        {
            const int rests = juce::jlimit (1, juce::jmax (1, rs.restStepsMax),
                                            (int) std::round (syl.pauseAfterSec * sr / stepSamples));
            step += rests;
        }
    }

    const double usedBeats = juce::jmax (step * stepBeats, out.size() / samplesPerBeat);
    clip.lengthBeats = juce::jmax (barBeats, std::ceil (usedBeats / barBeats - 1.0e-6) * barBeats);
    const int total = juce::jmax ((int) out.size(), (int) std::ceil (clip.lengthBeats * samplesPerBeat));

    clip.audio.setSize (1, total);
    clip.audio.clear();
    if (! out.empty())
        clip.audio.copyFrom (0, 0, out.data(), (int) out.size());
    return clip;
}

} // namespace vocalis
