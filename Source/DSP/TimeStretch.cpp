#include "TimeStretch.h"
#include <cmath>

namespace vocalis
{

std::vector<float> TimeStretch::process (const float* in, int numIn, int targetLength, double sampleRate)
{
    std::vector<float> out ((size_t) juce::jmax (0, targetLength), 0.0f);
    if (numIn <= 0 || targetLength <= 0)
        return out;

    if (std::abs (targetLength - numIn) < 2)
    {
        for (int i = 0; i < targetLength; ++i)
            out[(size_t) i] = in[juce::jmin (i, numIn - 1)];
        return out;
    }

    // Finestra ~ 30 ms, overlap 50 %
    int N = juce::nextPowerOfTwo ((int) (0.03 * sampleRate));
    N = juce::jlimit (128, 4096, N);
    if (numIn < N * 2)
        N = juce::jmax (32, juce::nextPowerOfTwo (numIn / 2) / 2);

    const int Hs = N / 2;
    const double ratio = (double) targetLength / numIn;   // >1 = piu' lungo
    const double Ha = Hs / ratio;
    const int tol = N / 4;

    std::vector<float> window ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0 * juce::MathConstants<double>::pi * i / N);

    std::vector<float> acc ((size_t) targetLength + (size_t) N, 0.0f);
    std::vector<float> norm ((size_t) targetLength + (size_t) N, 0.0f);

    auto sampleAt = [&] (int idx) { return (idx >= 0 && idx < numIn) ? in[idx] : 0.0f; };

    int prevPos = 0;   // inizio del frame analizzato al passo precedente
    bool first = true;

    for (int k = 0; ; ++k)
    {
        const int outPos = k * Hs;
        if (outPos >= targetLength)
            break;

        const int nominal = (int) std::round (k * Ha);
        int best = nominal;

        if (! first)
        {
            // Cerca l'offset che meglio continua la forma d'onda naturale (prevPos + Hs)
            const int natural = prevPos + Hs;
            double bestCorr = -1e30;
            const int lo = juce::jmax (0, nominal - tol);
            const int hi = juce::jmin (numIn - 1, nominal + tol);
            for (int cand = lo; cand <= hi; cand += 2)
            {
                double c = 0.0;
                for (int j = 0; j < Hs; j += 2)
                    c += sampleAt (natural + j) * sampleAt (cand + j);
                if (c > bestCorr) { bestCorr = c; best = cand; }
            }
        }
        first = false;
        prevPos = best;

        for (int j = 0; j < N; ++j)
        {
            const size_t o = (size_t) (outPos + j);
            acc[o] += sampleAt (best + j) * window[(size_t) j];
            norm[o] += window[(size_t) j];
        }
    }

    for (int i = 0; i < targetLength; ++i)
        out[(size_t) i] = norm[(size_t) i] > 1.0e-3f ? acc[(size_t) i] / norm[(size_t) i] : acc[(size_t) i];

    return out;
}

std::vector<float> TimeStretch::warp (const float* in, int numIn, const std::vector<double>& inPts,
                                      const std::vector<double>& outPts, double sampleRate)
{
    const int outLen = outPts.empty() ? numIn : juce::jmax (0, (int) std::lround (outPts.back()));
    std::vector<float> out ((size_t) outLen, 0.0f);
    if (numIn <= 0 || outLen <= 0) return out;

    bool identity = inPts.size() == outPts.size();
    for (size_t i = 0; identity && i < inPts.size(); ++i) identity = std::abs (inPts[i] - outPts[i]) < 0.5;
    if (identity || inPts.size() < 2 || inPts.size() != outPts.size())
    {
        for (int i = 0; i < outLen; ++i) out[(size_t) i] = i < numIn ? in[i] : 0.0f;
        return out;
    }

    // posizione d'ingresso per una posizione d'uscita (inversa della mappa, lineare a tratti)
    auto inputFor = [&] (double o)
    {
        size_t j = 1;
        while (j + 1 < outPts.size() && outPts[j] < o) ++j;
        const double o0 = outPts[j - 1], o1 = outPts[j], i0 = inPts[j - 1], i1 = inPts[j];
        return o1 > o0 ? i0 + (o - o0) * (i1 - i0) / (o1 - o0) : i0;
    };

    int N = juce::jlimit (128, 2048, juce::nextPowerOfTwo ((int) (0.02 * sampleRate)));
    const int Hs = N / 2, tol = N / 4;
    std::vector<float> window ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0 * juce::MathConstants<double>::pi * i / N);
    std::vector<float> acc ((size_t) outLen + (size_t) N, 0.0f), norm ((size_t) outLen + (size_t) N, 0.0f);
    auto at = [&] (int idx) { return (idx >= 0 && idx < numIn) ? in[idx] : 0.0f; };

    int prev = 0;
    for (int k = 0; k * Hs < outLen; ++k)
    {
        const int outPos = k * Hs;
        // il centro del frame deve cadere dove dice la mappa
        const int nominal = (int) std::lround (inputFor (outPos + Hs) - Hs);
        int best = nominal;
        if (k > 0)
        {
            const int natural = prev + Hs;
            double bestCorr = -1.0e30;
            for (int cand = nominal - tol; cand <= nominal + tol; cand += 2)
            {
                double c = 0.0;
                for (int j = 0; j < Hs; j += 2) c += at (natural + j) * at (cand + j);
                if (c > bestCorr) { bestCorr = c; best = cand; }
            }
        }
        prev = best;
        for (int j = 0; j < N; ++j)
        {
            acc[(size_t) (outPos + j)] += at (best + j) * window[(size_t) j];
            norm[(size_t) (outPos + j)] += window[(size_t) j];
        }
    }
    for (int i = 0; i < outLen; ++i)
        out[(size_t) i] = norm[(size_t) i] > 1.0e-3f ? acc[(size_t) i] / norm[(size_t) i] : acc[(size_t) i];
    return out;
}

} // namespace vocalis
