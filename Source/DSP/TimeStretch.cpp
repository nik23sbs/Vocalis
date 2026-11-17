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

} // namespace vocalis
