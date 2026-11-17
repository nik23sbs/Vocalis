#include "GrooveExtractor.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <numeric>

namespace vocalis
{

// ---------------------------------------------------------------- GrooveTemplate

void GrooveTemplate::lookup (double gridBeat, double& offsetBeats, float& velocity) const
{
    offsetBeats = 0.0;
    velocity = 1.0f;
    if (steps.empty() || lengthBeats <= 0.0)
        return;

    double pos = std::fmod (gridBeat, lengthBeats);
    if (pos < 0) pos += lengthBeats;

    const int n = (int) steps.size();
    const int idx = ((int) std::round (pos * stepsPerBeat)) % n;
    const auto& s = steps[(size_t) idx];
    offsetBeats = s.offsetBeats;
    velocity = s.hasHit ? 0.4f + 0.6f * s.velocity : 0.4f;
}

juce::ValueTree GrooveTemplate::toValueTree() const
{
    juce::ValueTree v ("GROOVE");
    v.setProperty ("length", lengthBeats, nullptr);
    v.setProperty ("bpm", sampleBpm, nullptr);
    v.setProperty ("name", sourceName, nullptr);

    juce::StringArray st, on;
    for (auto& s : steps)
        st.add (juce::String (s.offsetBeats, 5) + ":" + juce::String (s.velocity, 3) + ":" + (s.hasHit ? "1" : "0"));
    for (size_t i = 0; i < onsetBeats.size(); ++i)
        on.add (juce::String (onsetBeats[i], 4) + ":" + juce::String (onsetStrength[i], 3));
    v.setProperty ("steps", st.joinIntoString (";"), nullptr);
    v.setProperty ("onsets", on.joinIntoString (";"), nullptr);
    return v;
}

GrooveTemplate GrooveTemplate::fromValueTree (const juce::ValueTree& v)
{
    GrooveTemplate g;
    if (! v.isValid())
        return g;

    g.lengthBeats = v.getProperty ("length", 0.0);
    g.sampleBpm = v.getProperty ("bpm", 120.0);
    g.sourceName = v.getProperty ("name").toString();

    for (auto& tok : juce::StringArray::fromTokens (v.getProperty ("steps").toString(), ";", ""))
    {
        auto parts = juce::StringArray::fromTokens (tok, ":", "");
        if (parts.size() < 3) continue;
        Step s;
        s.offsetBeats = parts[0].getDoubleValue();
        s.velocity = parts[1].getFloatValue();
        s.hasHit = parts[2] == "1";
        g.steps.push_back (s);
    }
    for (auto& tok : juce::StringArray::fromTokens (v.getProperty ("onsets").toString(), ";", ""))
    {
        auto parts = juce::StringArray::fromTokens (tok, ":", "");
        if (parts.size() < 2) continue;
        g.onsetBeats.push_back (parts[0].getDoubleValue());
        g.onsetStrength.push_back (parts[1].getFloatValue());
    }
    if (g.steps.empty())
        g.lengthBeats = 0.0;
    return g;
}

// ---------------------------------------------------------------- Onset detection

void GrooveExtractor::detectOnsets (const juce::AudioBuffer<float>& mono, double sr,
                                    std::vector<double>& times, std::vector<float>& strengths)
{
    times.clear();
    strengths.clear();

    const int numSamples = mono.getNumSamples();
    const float* x = mono.getReadPointer (0);

    const int order = sr > 60000.0 ? 11 : 10;
    const int N = 1 << order;
    const int hop = N / 4;
    if (numSamples < N)
        return;

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2), prevMag ((size_t) N / 2 + 1, 0.0f);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N);

    // Spectral flux su magnitudo logaritmica
    std::vector<float> flux;
    for (int start = 0; start + N <= numSamples; start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = x[start + i] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());

        float f = 0.0f;
        for (int b = 1; b <= N / 2; ++b)
        {
            const float m = std::log1p (100.0f * buf[(size_t) b]);
            f += juce::jmax (0.0f, m - prevMag[(size_t) b]);
            prevMag[(size_t) b] = m;
        }
        flux.push_back (f);
    }
    if (flux.size() < 3)
        return;

    const float maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux <= 0.0f)
        return;
    for (auto& f : flux) f /= maxFlux;

    // Inviluppo per rifinire l'istante d'attacco e misurare la forza
    std::vector<float> env ((size_t) numSamples);
    {
        const float a = std::exp (-1.0f / (float) (0.001 * sr));
        float e = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            e = juce::jmax (std::abs (x[i]), e * a);
            env[(size_t) i] = e;
        }
    }

    const int nF = (int) flux.size();
    const int w = 8, localMax = 3;
    const int minGap = (int) (0.06 * sr);
    int lastOnset = -minGap;

    for (int i = 0; i < nF - 1; ++i)
    {
        float mean = 0.0f; int cnt = 0;
        for (int k = juce::jmax (0, i - w); k <= juce::jmin (nF - 1, i + w); ++k) { mean += flux[(size_t) k]; ++cnt; }
        mean /= (float) cnt;

        if (flux[(size_t) i] < mean + 0.08f)
            continue;

        bool isMax = true;
        for (int k = juce::jmax (0, i - localMax); k <= juce::jmin (nF - 1, i + localMax); ++k)
            if (flux[(size_t) k] > flux[(size_t) i]) { isMax = false; break; }
        if (! isMax)
            continue;

        // Raffina: massima pendenza dell'inviluppo dentro il frame (l'attacco entra dal bordo destro della finestra)
        const int lo = juce::jmax (0, i * hop), hi = juce::jmin (numSamples - 1, i * hop + N + hop);
        int best = lo; float bestSlope = -1.0f;
        const int d = juce::jmax (1, (int) (0.002 * sr));
        for (int s = lo; s < hi - d; ++s)
        {
            const float slope = env[(size_t) (s + d)] - env[(size_t) s];
            if (slope > bestSlope) { bestSlope = slope; best = s; }
        }

        if (best - lastOnset < minGap)
            continue;
        lastOnset = best;

        // Forza: picco nei 30 ms successivi
        float peak = 0.0f;
        for (int s = best; s < juce::jmin (numSamples, best + (int) (0.03 * sr)); ++s)
            peak = juce::jmax (peak, env[(size_t) s]);

        times.push_back (best / sr);
        strengths.push_back (peak);
    }

    if (! strengths.empty())
    {
        const float mx = *std::max_element (strengths.begin(), strengths.end());
        for (auto& s : strengths) s = mx > 0 ? std::sqrt (s / mx) : 0.0f;
    }
}

// ---------------------------------------------------------------- Template

GrooveTemplate GrooveExtractor::extract (const juce::AudioBuffer<float>& audio, double sr, double projectBpm)
{
    GrooveTemplate g;
    const int n = audio.getNumSamples();
    if (n == 0 || sr <= 0)
        return g;

    juce::AudioBuffer<float> mono (1, n);
    mono.clear();
    for (int c = 0; c < audio.getNumChannels(); ++c)
        mono.addFrom (0, 0, audio, c, 0, n, 1.0f / (float) audio.getNumChannels());

    std::vector<double> t;
    std::vector<float> st;
    detectOnsets (mono, sr, t, st);

    // Quante battute dura il loop? Si arrotonda alle battute (4/4) piu' vicine al BPM del progetto.
    const double dur = n / sr;
    const double beatsAtProject = dur * projectBpm / 60.0;
    double lengthBeats;
    if (beatsAtProject >= 3.0)
        lengthBeats = juce::jmax (4.0, std::round (beatsAtProject / 4.0) * 4.0);
    else
        lengthBeats = juce::jmax (1.0, std::round (beatsAtProject));

    g.lengthBeats = lengthBeats;
    g.sampleBpm = lengthBeats * 60.0 / dur;

    const int numSteps = juce::jmax (1, (int) std::round (lengthBeats * GrooveTemplate::stepsPerBeat));
    g.steps.assign ((size_t) numSteps, {});

    for (size_t i = 0; i < t.size(); ++i)
    {
        const double beat = t[i] * g.sampleBpm / 60.0;
        g.onsetBeats.push_back (beat);
        g.onsetStrength.push_back (st[i]);

        const int raw = (int) std::round (beat * GrooveTemplate::stepsPerBeat);
        const double offset = beat - raw / (double) GrooveTemplate::stepsPerBeat;
        const int idx = ((raw % numSteps) + numSteps) % numSteps;
        auto& s = g.steps[(size_t) idx];
        if (! s.hasHit || st[i] > s.velocity)
        {
            s.hasHit = true;
            s.velocity = st[i];
            s.offsetBeats = offset;
        }
    }

    // Gli step vuoti ereditano lo spostamento medio della loro posizione nel quarto (cattura lo swing)
    double classSum[GrooveTemplate::stepsPerBeat] = {};
    int classCnt[GrooveTemplate::stepsPerBeat] = {};
    for (int i = 0; i < numSteps; ++i)
        if (g.steps[(size_t) i].hasHit)
        {
            classSum[i % GrooveTemplate::stepsPerBeat] += g.steps[(size_t) i].offsetBeats;
            classCnt[i % GrooveTemplate::stepsPerBeat]++;
        }
    for (int i = 0; i < numSteps; ++i)
    {
        auto& s = g.steps[(size_t) i];
        const int c = i % GrooveTemplate::stepsPerBeat;
        if (! s.hasHit && classCnt[c] > 0)
            s.offsetBeats = classSum[c] / classCnt[c];
    }

    return g;
}

GrooveTemplate GrooveExtractor::extractFromFile (const juce::File& file, double projectBpm, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Formato audio non supportato: " + file.getFileName();
        return {};
    }

    const auto maxLen = (juce::int64) (reader->sampleRate * 60.0); // max 60 s
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);

    auto g = extract (buf, reader->sampleRate, projectBpm);
    g.sourceName = file.getFileName();
    if (g.onsetBeats.empty())
        error = "Nessun colpo rilevato nel sample";
    return g;
}

} // namespace vocalis
