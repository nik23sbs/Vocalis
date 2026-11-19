#include "EffectsChain.h"
#include <cmath>

namespace vocalis
{

juce::StringArray EffectsParams::delayDivisionNames()
{
    return { "1/4", "1/8", "1/8D", "1/16", "1/4T", "1/8T", "1/2" };
}

double EffectsParams::delayDivisionBeats (int i)
{
    switch (i)
    {
        case 0:  return 1.0;
        case 1:  return 0.5;
        case 2:  return 0.75;
        case 3:  return 0.25;
        case 4:  return 2.0 / 3.0;
        case 5:  return 1.0 / 3.0;
        case 6:  return 2.0;
        default: return 0.5;
    }
}

void EffectsChain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };

    lowCut.prepare (spec);
    highCut.prepare (spec);
    presence.prepare (spec);
    lastLowCut = lastHighCut = -1; lastPresence = -100;

    compressor.prepare (spec);
    compressor.setAttack (5.0f);
    compressor.setRelease (80.0f);

    chorus.prepare (spec);
    chorus.setCentreDelay (7.0f);
    reverb.prepare (spec);

    deessSplit.prepare (spec);
    deessSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    deessSplit.setCutoffFrequency (juce::jmin (5500.0f, (float) (sr * 0.4)));
    deessHigh.setSize (2, maxBlock);
    dblBuf.setSize (2, (int) (sr * 0.05) + 2);

    delayBuf.setSize (2, (int) (sr * 4.5) + 1);
    scratch.setSize (2, maxBlock);
    delaySamples.reset (sr, 0.05);
    reset();
}

void EffectsChain::reset()
{
    ringPhase = 0.0;
    crushHold[0] = crushHold[1] = 0.0f;
    crushCounter = 0;
    lowCut.reset(); highCut.reset(); presence.reset();
    compressor.reset(); chorus.reset(); reverb.reset();
    delayBuf.clear();
    delayWrite = 0;
    deessSplit.reset();
    deessEnv = 0.0f; deessGain = 1.0f;
    limGain = 1.0f;
    dblBuf.clear();
    dblWrite = 0;
}

double EffectsChain::tailSeconds (const EffectsParams& p, double bpm)
{
    double t = 0.1;
    if (p.delayOn)
    {
        const double d = EffectsParams::delayDivisionBeats (p.delayDiv) * 60.0 / juce::jmax (20.0, bpm);
        const double repeats = std::log (0.001) / std::log (juce::jlimit (0.01, 0.95, (double) p.delayFeedback));
        t = juce::jmax (t, juce::jmin (12.0, d * repeats));
    }
    if (p.reverbOn)
        t = juce::jmax (t, 1.0 + 5.0 * p.reverbSize);
    return t;
}

void EffectsChain::process (juce::AudioBuffer<float>& buffer, const EffectsParams& p, double bpm)
{
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0)
        return;

    // Lavoriamo a blocchi di dimensione massima maxBlock (export offline passa buffer lunghi)
    if (n > maxBlock)
    {
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int len = juce::jmin (maxBlock, n - pos);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numCh, pos, len);
            process (view, p, bpm);
        }
        return;
    }

    // --- De-esser (prima di tutto: le sibilanti non devono arrivare a distorsione e compressore)
    if (p.deessOn && p.deessAmount > 0.001f)
    {
        const float thr = juce::Decibels::decibelsToGain (-18.0f - 22.0f * p.deessAmount);   // soglia sulla banda alta
        const float att = std::exp (-1.0f / (float) (0.0008 * sr)), rel = std::exp (-1.0f / (float) (0.06 * sr));
        for (int i = 0; i < n; ++i)
        {
            float hiPeak = 0.0f;
            float lows[2] = {}, highs[2] = {};
            for (int c = 0; c < numCh; ++c)
            {
                const float x = buffer.getSample (c, i);
                lows[c] = deessSplit.processSample (c, x);
                highs[c] = x - lows[c];
                hiPeak = juce::jmax (hiPeak, std::abs (highs[c]));
            }
            deessEnv = hiPeak > deessEnv ? att * deessEnv + (1.0f - att) * hiPeak : rel * deessEnv + (1.0f - rel) * hiPeak;
            const float target = deessEnv > thr ? std::pow (thr / deessEnv, 0.5f + p.deessAmount * 0.4f) : 1.0f;
            deessGain = target < deessGain ? target : deessGain + (target - deessGain) * 0.002f;
            for (int c = 0; c < numCh; ++c)
                buffer.setSample (c, i, lows[c] + highs[c] * deessGain);
        }
    }

    // --- Ring modulator
    if (p.ringOn)
    {
        const double inc = juce::MathConstants<double>::twoPi * p.ringFreq / sr;
        for (int i = 0; i < n; ++i)
        {
            const float m = (float) std::sin (ringPhase);
            ringPhase += inc;
            if (ringPhase > juce::MathConstants<double>::twoPi) ringPhase -= juce::MathConstants<double>::twoPi;
            for (int c = 0; c < numCh; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                d[i] = d[i] * (1.0f - p.ringMix) + d[i] * m * p.ringMix;
            }
        }
    }

    // --- Distorsione (tanh)
    if (p.distOn)
    {
        const float drive = juce::Decibels::decibelsToGain (p.distDrive);
        const float norm = 1.0f / std::tanh (drive);
        for (int c = 0; c < numCh; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                d[i] = d[i] * (1.0f - p.distMix) + std::tanh (d[i] * drive) * norm * 0.7f * p.distMix;
        }
    }

    // --- Bitcrusher
    if (p.crushOn)
    {
        const float levels = std::pow (2.0f, juce::jlimit (2.0f, 16.0f, p.crushBits) - 1.0f);
        const int down = juce::jmax (1, (int) p.crushDown);
        for (int i = 0; i < n; ++i)
        {
            const bool take = (crushCounter++ % down) == 0;
            for (int c = 0; c < numCh; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                if (take) crushHold[c] = std::round (d[i] * levels) / levels;
                d[i] = d[i] * (1.0f - p.crushMix) + crushHold[c] * p.crushMix;
            }
        }
    }

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // --- EQ
    if (p.eqOn)
    {
        if (p.eqLowCut != lastLowCut)
        {
            *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, juce::jlimit (20.0f, 2000.0f, p.eqLowCut), 0.707f);
            lastLowCut = p.eqLowCut;
        }
        if (p.eqHighCut != lastHighCut)
        {
            *highCut.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, juce::jlimit (1000.0f, (float) (sr * 0.45), p.eqHighCut), 0.707f);
            lastHighCut = p.eqHighCut;
        }
        if (p.eqPresence != lastPresence)
        {
            *presence.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 3000.0f, 0.8f, juce::Decibels::decibelsToGain (p.eqPresence));
            lastPresence = p.eqPresence;
        }
        lowCut.process (ctx);
        highCut.process (ctx);
        if (std::abs (p.eqPresence) > 0.05f)
            presence.process (ctx);
    }

    // --- Compressore con make-up automatico
    if (p.compOn)
    {
        compressor.setThreshold (p.compThresh);
        compressor.setRatio (juce::jmax (1.0f, p.compRatio));
        compressor.process (ctx);
        const float makeup = juce::Decibels::decibelsToGain (-p.compThresh * (1.0f - 1.0f / juce::jmax (1.0f, p.compRatio)) * 0.5f);
        buffer.applyGain (0, n, makeup);
    }

    // --- Doubler
    if (p.doublerOn)
    {
        const int len = dblBuf.getNumSamples();
        const double inc1 = juce::MathConstants<double>::twoPi * 0.31 / sr, inc2 = juce::MathConstants<double>::twoPi * 0.43 / sr;
        for (int i = 0; i < n; ++i)
        {
            const float inL = buffer.getSample (0, i), inR = numCh > 1 ? buffer.getSample (1, i) : inL;
            dblBuf.setSample (0, dblWrite, 0.5f * (inL + inR));
            const float d1 = (float) ((0.012 + 0.0015 * std::sin (dblPhase1)) * sr);
            const float d2 = (float) ((0.019 + 0.0015 * std::sin (dblPhase2)) * sr);
            dblPhase1 = std::fmod (dblPhase1 + inc1, juce::MathConstants<double>::twoPi);
            dblPhase2 = std::fmod (dblPhase2 + inc2, juce::MathConstants<double>::twoPi);
            auto readAt = [&] (float delay)
            {
                float rp = (float) dblWrite - delay;
                while (rp < 0) rp += (float) len;
                const int a = (int) rp % len, b = (a + 1) % len;
                const float fr = rp - std::floor (rp);
                return dblBuf.getSample (0, a) * (1.0f - fr) + dblBuf.getSample (0, b) * fr;
            };
            const float v1 = readAt (d1), v2 = readAt (d2);
            dblWrite = (dblWrite + 1) % len;
            const float w = p.doublerWidth;
            buffer.setSample (0, i, inL + p.doublerMix * (v1 * (0.5f + 0.5f * w) + v2 * (0.5f - 0.5f * w)));
            if (numCh > 1)
                buffer.setSample (1, i, inR + p.doublerMix * (v2 * (0.5f + 0.5f * w) + v1 * (0.5f - 0.5f * w)));
        }
    }

    // --- Chorus
    if (p.chorusOn)
    {
        chorus.setRate (p.chorusRate);
        chorus.setDepth (p.chorusDepth);
        chorus.setMix (p.chorusMix);
        chorus.setFeedback (0.1f);
        chorus.process (ctx);
    }

    // --- Delay a tempo (ping-pong leggero)
    {
        const double beats = EffectsParams::delayDivisionBeats (p.delayDiv);
        const float target = (float) juce::jlimit (1.0, (double) delayBuf.getNumSamples() - 2.0,
                                                    beats * 60.0 / juce::jmax (20.0, bpm) * sr);
        delaySamples.setTargetValue (target);
        const int len = delayBuf.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            const float ds = delaySamples.getNextValue();
            float readPos = (float) delayWrite - ds;
            if (readPos < 0) readPos += (float) len;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % len;
            const float frac = readPos - (float) i0;

            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                const auto* db = delayBuf.getReadPointer (c);
                wet[c] = db[i0] + (db[i1] - db[i0]) * frac;
            }

            const float inL = buffer.getSample (0, i);
            const float inR = numCh > 1 ? buffer.getSample (1, i) : inL;
            const float in = p.delayOn ? 0.5f * (inL + inR) : 0.0f;

            // ping-pong: L riceve l'ingresso + il ritorno di R, R riceve il ritorno di L
            delayBuf.setSample (0, delayWrite, in + wet[1] * p.delayFeedback);
            delayBuf.setSample (1, delayWrite, wet[0] * p.delayFeedback);
            delayWrite = (delayWrite + 1) % len;

            if (p.delayOn || wet[0] != 0.0f || wet[1] != 0.0f)
            {
                const float mix = p.delayOn ? p.delayMix : 0.0f;
                buffer.setSample (0, i, inL + wet[0] * mix);
                if (numCh > 1) buffer.setSample (1, i, inR + wet[1] * mix);
            }
        }
    }

    // --- Riverbero
    if (p.reverbOn)
    {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = p.reverbSize;
        rp.damping = p.reverbDamp;
        rp.wetLevel = p.reverbMix;
        rp.dryLevel = 1.0f - p.reverbMix * 0.5f;
        rp.width = 1.0f;
        reverb.setParameters (rp);
        reverb.process (ctx);
    }

    buffer.applyGain (0, n, juce::Decibels::decibelsToGain (p.outGainDb));

    // --- Limiter finale: attacco istantaneo, rilascio 120 ms, tetto regolabile (niente clip in uscita)
    if (p.limiterOn)
    {
        const float ceiling = juce::Decibels::decibelsToGain (p.limiterCeilingDb);
        const float rel = 1.0f - std::exp (-1.0f / (float) (0.12 * sr));
        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, std::abs (buffer.getSample (c, i)));
            const float need = pk * limGain > ceiling ? ceiling / pk : 1.0f;
            limGain = need < limGain ? need : limGain + (juce::jmin (1.0f, need) - limGain) * rel;
            for (int c = 0; c < numCh; ++c) buffer.setSample (c, i, buffer.getSample (c, i) * limGain);
        }
    }
}

// ---------------------------------------------------------------- Robotize

void robotize (juce::AudioBuffer<float>& mono, double sampleRate, float robotHz, float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    const int n = mono.getNumSamples();
    if (amount <= 0.001f || n == 0)
        return;

    const int order = sampleRate > 30000 ? 11 : 10;
    const int N = 1 << order;
    const int hop = juce::jlimit (16, N / 2, (int) std::round (sampleRate / juce::jlimit (30.0f, 600.0f, robotHz)));

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), frame ((size_t) N * 2), tmp ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N);

    std::vector<juce::dsp::Complex<float>> spec ((size_t) N), time ((size_t) N);
    std::vector<float> outAcc ((size_t) (n + N), 0.0f);
    const float* x = mono.getReadPointer (0);

    for (int start = -N / 2; start < n; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;
            frame[(size_t) i] = (idx >= 0 && idx < n) ? x[idx] * window[(size_t) i] : 0.0f;
        }

        // Spettro -> solo magnitudo (fase zero) -> impulso centrato
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        // costruisci uno spettro complesso reale con fase zero nel formato interleaved
        std::fill (spec.begin(), spec.end(), juce::dsp::Complex<float> {});
        for (int b = 0; b <= N / 2; ++b)
        {
            spec[(size_t) b] = { frame[(size_t) b], 0.0f };
            if (b > 0 && b < N / 2) spec[(size_t) (N - b)] = { frame[(size_t) b], 0.0f };
        }
        fft.perform (spec.data(), time.data(), true);

        for (int i = 0; i < N; ++i)
            tmp[(size_t) i] = time[(size_t) ((i + N / 2) % N)].real() * window[(size_t) i];

        for (int i = 0; i < N; ++i)
        {
            const int o = start + i;
            if (o >= 0 && o < (int) outAcc.size())
                outAcc[(size_t) o] += tmp[(size_t) i];
        }
    }

    // normalizza l'energia rispetto al segnale originale
    double eIn = 0, eOut = 0;
    for (int i = 0; i < n; ++i) { eIn += x[i] * x[i]; eOut += outAcc[(size_t) i] * outAcc[(size_t) i]; }
    const float g = eOut > 0 ? (float) std::sqrt (eIn / eOut) : 0.0f;

    auto* w = mono.getWritePointer (0);
    for (int i = 0; i < n; ++i)
        w[i] = w[i] * (1.0f - amount) + outAcc[(size_t) i] * g * amount;
}

} // namespace vocalis
