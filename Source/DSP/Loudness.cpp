#include "Loudness.h"
#include <cmath>

namespace vocalis
{

namespace
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Coefficienti del filtro K ricalcolati per qualsiasi sample rate (formule di BS.1770 / libebur128)
    void kFilter (double fs, Biquad& shelf, Biquad& hp)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hp = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
    }
}

double Loudness::integratedLufs (const juce::AudioBuffer<float>& buffer, double fs)
{
    const int n = buffer.getNumSamples(), chs = buffer.getNumChannels();
    if (n == 0 || chs == 0)
        return -100.0;

    // energia filtrata per blocchi di 100 ms; i blocchi di misura sono 400 ms con sovrapposizione 75 %
    const int sub = juce::jmax (1, (int) (0.1 * fs));
    const int numSub = n / sub + 1;
    std::vector<double> subEnergy ((size_t) numSub, 0.0);
    for (int c = 0; c < chs; ++c)
    {
        Biquad shelf, hp;
        kFilter (fs, shelf, hp);
        const float* x = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            const double y = hp.process (shelf.process (x[i]));
            subEnergy[(size_t) (i / sub)] += y * y;
        }
    }

    std::vector<double> blocks;
    for (int b = 0; b + 4 <= numSub || (b == 0 && numSub > 0); ++b)
    {
        double e = 0; int cnt = 0;
        for (int k = b; k < juce::jmin (numSub, b + 4); ++k) { e += subEnergy[(size_t) k]; cnt += sub; }
        blocks.push_back (e / juce::jmax (1, cnt));
        if (numSub < 4) break;
    }

    auto lufsOf = [] (double e) { return -0.691 + 10.0 * std::log10 (e + 1e-12); };

    double sum = 0; int cnt = 0;
    for (auto e : blocks) if (lufsOf (e) > -70.0) { sum += e; ++cnt; }
    if (cnt == 0) return -100.0;
    const double relGate = lufsOf (sum / cnt) - 10.0;

    sum = 0; cnt = 0;
    for (auto e : blocks) if (lufsOf (e) > -70.0 && lufsOf (e) > relGate) { sum += e; ++cnt; }
    return cnt > 0 ? lufsOf (sum / cnt) : -100.0;
}

float Loudness::normalise (juce::AudioBuffer<float>& buffer, double fs, double targetLufs, float peakCeilingDb)
{
    const double current = integratedLufs (buffer, fs);
    if (current <= -90.0)
        return 1.0f;

    float gain = (float) std::pow (10.0, (targetLufs - current) / 20.0);
    float peak = 0.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, buffer.getNumSamples()));
    const float ceiling = juce::Decibels::decibelsToGain (peakCeilingDb);
    if (peak * gain > ceiling && peak > 0.0f)
        gain = ceiling / peak;

    buffer.applyGain (gain);
    return gain;
}

} // namespace vocalis
