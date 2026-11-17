#include "Renderer.h"
#include "DSP/EffectsChain.h"
#include <cmath>

namespace vocalis
{

Renderer::Renderer() : juce::Thread ("Vocalis render")
{
    startThread (juce::Thread::Priority::normal);
}

Renderer::~Renderer()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void Renderer::request (const RenderRequest& r)
{
    {
        std::lock_guard<std::mutex> l (reqMutex);
        pending = std::make_unique<RenderRequest> (r);
    }
    busy = true;
    wake.signal();
}

void Renderer::run()
{
    while (! threadShouldExit())
    {
        wake.wait (500);

        std::unique_ptr<RenderRequest> job;
        {
            std::lock_guard<std::mutex> l (reqMutex);
            job = std::move (pending);
        }
        if (job == nullptr)
            continue;

        auto clip = renderNow (*job);

        bool morePending;
        {
            std::lock_guard<std::mutex> l (reqMutex);
            morePending = pending != nullptr;
        }
        if (! morePending)
            busy = false;

        if (! morePending && onComplete != nullptr && ! threadShouldExit())
        {
            juce::MessageManager::callAsync ([cb = onComplete, clip] { cb (clip); });
        }
    }
}

RenderedClip::Ptr Renderer::renderNow (const RenderRequest& r)
{
    RenderedClip::Ptr clip = new RenderedClip();
    clip->bpm = r.rhythm.bpm;
    clip->sampleRate = r.targetSampleRate;

    // 1) Sintesi (con cache: cambiare solo ritmo o effetti non risintetizza)
    std::shared_ptr<SynthResult> synth;
    {
        const auto key = r.voice.cacheKey (r.text);
        std::lock_guard<std::mutex> l (cacheMutex);
        if (cachedSynth != nullptr && key == cachedKey)
        {
            synth = cachedSynth;
        }
        else
        {
            synth = std::make_shared<SynthResult> (engine.synthesize (r.text, r.voice));
            if (synth->ok())
            {
                cachedSynth = synth;
                cachedKey = key;
            }
        }
    }

    if (! synth->ok())
    {
        clip->error = synth->error.isNotEmpty() ? synth->error : "Sintesi fallita";
        return clip;
    }

    // 2) Robotizza (copia, la cache resta pulita)
    SynthResult voice;
    voice.sampleRate = synth->sampleRate;
    voice.syllables = synth->syllables;
    voice.audio.makeCopyOf (synth->audio);
    robotize (voice.audio, voice.sampleRate, r.voice.robotHz, r.voice.robotize);

    juce::StringArray labels;
    for (auto& s : voice.syllables) labels.add (s.label);
    clip->phonemeText = labels.joinIntoString ("-");

    // 3) Quantizzazione ritmica
    auto q = Quantizer::process (voice, r.rhythm, r.groove);
    clip->lengthBeats = q.lengthBeats;

    // 4) Ricampionamento al sample rate della DAW
    const double ratio = voice.sampleRate / r.targetSampleRate;   // campioni in ingresso per campione in uscita
    const int inN = q.audio.getNumSamples();
    const int outN = (int) std::ceil (q.lengthBeats * 60.0 / r.rhythm.bpm * r.targetSampleRate);
    const int outLen = juce::jmax (outN, (int) std::ceil (inN / ratio));

    juce::AudioBuffer<float> padded (1, inN + 64);
    padded.clear();
    padded.copyFrom (0, 0, q.audio, 0, 0, inN);

    clip->audio.setSize (1, outLen);
    clip->audio.clear();
    juce::WindowedSincInterpolator interp;
    const int producible = juce::jmin (outLen, (int) std::floor (inN / ratio));
    interp.process (ratio, padded.getReadPointer (0), clip->audio.getWritePointer (0), producible);

    for (auto& m : q.markers)
        clip->markers.push_back ({ (int) std::round (m.start / ratio), (int) std::round (m.end / ratio), m.label });

    // 5) Normalizza a -3 dBFS
    const float peak = clip->audio.getMagnitude (0, 0, clip->audio.getNumSamples());
    if (peak > 1.0e-4f)
        clip->audio.applyGain (juce::Decibels::decibelsToGain (-3.0f) / peak);

    return clip;
}

} // namespace vocalis
