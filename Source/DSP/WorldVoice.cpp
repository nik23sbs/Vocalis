#include "WorldVoice.h"
#include <world/harvest.h>
#include <world/cheaptrick.h>
#include <world/d4c.h>
#include <world/synthesis.h>
#include <algorithm>
#include <cmath>

namespace vocalis
{

namespace
{
    constexpr double framePeriodMs = 5.0;

    struct Analysis
    {
        int fs = 24000;
        std::vector<double> x, time, f0;
        int fftSize = 0;
        std::vector<std::vector<double>> sp, ap;
    };

    bool analyse (const juce::AudioBuffer<float>& mono, double sampleRate, Analysis& a, bool envelopes)
    {
        const int n = mono.getNumSamples();
        if (n < (int) (0.05 * sampleRate))
            return false;

        a.fs = (int) std::round (sampleRate);
        a.x.resize ((size_t) n);
        const float* d = mono.getReadPointer (0);
        for (int i = 0; i < n; ++i) a.x[(size_t) i] = d[i];

        HarvestOption ho;
        InitializeHarvestOption (&ho);
        ho.frame_period = framePeriodMs;
        ho.f0_floor = 55.0;
        ho.f0_ceil = 900.0;
        const int frames = GetSamplesForHarvest (a.fs, n, framePeriodMs);
        a.time.assign ((size_t) frames, 0.0);
        a.f0.assign ((size_t) frames, 0.0);
        Harvest (a.x.data(), n, a.fs, &ho, a.time.data(), a.f0.data());

        if (! envelopes)
            return true;

        CheapTrickOption co;
        InitializeCheapTrickOption (a.fs, &co);
        co.f0_floor = 55.0;
        a.fftSize = GetFFTSizeForCheapTrick (a.fs, &co);
        co.fft_size = a.fftSize;
        const int bins = a.fftSize / 2 + 1;

        a.sp.assign ((size_t) frames, std::vector<double> ((size_t) bins));
        a.ap.assign ((size_t) frames, std::vector<double> ((size_t) bins));
        std::vector<double*> spPtr ((size_t) frames), apPtr ((size_t) frames);
        for (int f = 0; f < frames; ++f) { spPtr[(size_t) f] = a.sp[(size_t) f].data(); apPtr[(size_t) f] = a.ap[(size_t) f].data(); }

        CheapTrick (a.x.data(), n, a.fs, a.time.data(), a.f0.data(), frames, &co, spPtr.data());

        D4COption dop;
        InitializeD4COption (&dop);
        D4C (a.x.data(), n, a.fs, a.time.data(), a.f0.data(), frames, a.fftSize, &dop, apPtr.data());
        return true;
    }

    double median (std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    }
}

double WorldVoice::medianF0 (const juce::AudioBuffer<float>& mono, double sampleRate)
{
    Analysis a;
    if (! analyse (mono, sampleRate, a, false))
        return 0.0;
    std::vector<double> voiced;
    for (auto f : a.f0) if (f > 0) voiced.push_back (f);
    return median (voiced);
}

bool WorldVoice::process (juce::AudioBuffer<float>& mono, double sampleRate, const WorldTransform& t)
{
    if (t.isIdentity())
        return true;

    Analysis a;
    if (! analyse (mono, sampleRate, a, true))
        return false;

    const int frames = (int) a.f0.size();
    const int n = mono.getNumSamples();

    // --- Curva di trasposizione per sillaba (in semitoni per frame), ammorbidita su ~20 ms
    std::vector<double> sylSemis ((size_t) frames, 0.0);
    for (auto& r : t.syllablePitch)
    {
        const int f0i = juce::jlimit (0, frames - 1, (int) (r.start / sampleRate * 1000.0 / framePeriodMs));
        const int f1i = juce::jlimit (0, frames - 1, (int) (r.end / sampleRate * 1000.0 / framePeriodMs));
        for (int f = f0i; f <= f1i; ++f) sylSemis[(size_t) f] = r.semis;
    }
    if (! t.syllablePitch.empty())
    {
        std::vector<double> sm ((size_t) frames, 0.0);
        const int w = 2;
        for (int f = 0; f < frames; ++f)
        {
            double s = 0; int c = 0;
            for (int k = juce::jmax (0, f - w); k <= juce::jmin (frames - 1, f + w); ++k) { s += sylSemis[(size_t) k]; ++c; }
            sm[(size_t) f] = s / c;
        }
        sylSemis = std::move (sm);
    }

    // --- F0: intonazione attorno alla mediana + trasposizione
    std::vector<double> voicedLog;
    for (auto f : a.f0) if (f > 0) voicedLog.push_back (std::log2 (f));
    const double centre = median (voicedLog);
    for (int f = 0; f < frames; ++f)
    {
        auto& v = a.f0[(size_t) f];
        if (v <= 0) continue;
        double lf = std::log2 (v);
        lf = centre + (lf - centre) * juce::jlimit (0.0, 3.0, (double) t.intonation);
        lf += (t.pitchSemis + sylSemis[(size_t) f]) / 12.0;
        v = juce::jlimit (40.0, 1500.0, std::pow (2.0, lf));
    }

    // --- Formanti: deformazione dell'asse delle frequenze dell'inviluppo spettrale
    if (std::abs (t.formantSemis) > 0.01f)
    {
        const double ratio = std::pow (2.0, t.formantSemis / 12.0);
        const int bins = a.fftSize / 2 + 1;
        std::vector<double> tmp ((size_t) bins);
        for (int f = 0; f < frames; ++f)
        {
            auto& sp = a.sp[(size_t) f];
            for (int k = 0; k < bins; ++k)
            {
                const double src = k / ratio;
                const int i0 = (int) src;
                const double fr = src - i0;
                if (i0 >= bins - 1) tmp[(size_t) k] = sp[(size_t) bins - 1];
                else tmp[(size_t) k] = sp[(size_t) i0] * (1.0 - fr) + sp[(size_t) i0 + 1] * fr;
            }
            sp = tmp;
        }
    }

    // --- Sintesi
    std::vector<const double*> spPtr ((size_t) frames), apPtr ((size_t) frames);
    for (int f = 0; f < frames; ++f) { spPtr[(size_t) f] = a.sp[(size_t) f].data(); apPtr[(size_t) f] = a.ap[(size_t) f].data(); }
    std::vector<double> y ((size_t) n, 0.0);
    Synthesis (a.f0.data(), frames, spPtr.data(), apPtr.data(), a.fftSize, framePeriodMs, a.fs, n, y.data());

    // stesso livello RMS dell'originale (WORLD puo' cambiare leggermente il volume)
    double eIn = 0, eOut = 0;
    for (int i = 0; i < n; ++i) { eIn += a.x[(size_t) i] * a.x[(size_t) i]; eOut += y[(size_t) i] * y[(size_t) i]; }
    const double g = eOut > 0 ? std::sqrt (eIn / eOut) : 1.0;

    auto* w = mono.getWritePointer (0);
    for (int i = 0; i < n; ++i) w[i] = (float) (y[(size_t) i] * g);
    return true;
}

} // namespace vocalis
