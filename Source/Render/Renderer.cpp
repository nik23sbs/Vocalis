#include "Renderer.h"
#include "DSP/EffectsChain.h"
#include "DSP/WorldVoice.h"
#include "DSP/Loudness.h"
#include "Engine/KokoroVoices.h"
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
    stopThread (10000);
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
            juce::MessageManager::callAsync ([cb = onComplete, clip] { cb (clip); });
    }
}

std::shared_ptr<SynthResult> Renderer::synthesise (const RenderRequest& r, juce::String& warning)
{
    auto v = r.voice;
    if (v.engine == EngineType::Neural)
    {
        juce::String why;
        if (KokoroEngine::isAvailable (&why))
        {
            auto res = std::make_shared<SynthResult> (kokoro.synthesize (r.text, v));
            if (res->ok())
                return res;
            warning = "Voce neurale non disponibile (" + res->error + "): uso eSpeak";
        }
        else
            warning = "Voce neurale non disponibile (" + why + "): uso eSpeak";

        // riserva: voce eSpeak dello stesso genere
        bool male = false;
        for (auto& info : KokoroVoices::catalogue())
            if (v.primaryVoice() == info.id) male = info.male;
        v.engine = EngineType::Formant;
        v.espeakVariant = male ? "m3" : "f3";
    }
    return std::make_shared<SynthResult> (espeak.synthesize (r.text, v));
}

RenderedClip::Ptr Renderer::renderNow (const RenderRequest& r)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    RenderedClip::Ptr clip = new RenderedClip();
    clip->bpm = r.rhythm.bpm;
    clip->sampleRate = r.targetSampleRate;
    clip->stepBeats = r.rhythm.stepBeats;

    // ---- 1) sintesi (cache)
    std::shared_ptr<SynthResult> synth;
    juce::String warning;
    const auto key = r.voice.synthKey (r.text);
    {
        std::lock_guard<std::mutex> l (cacheMutex);
        if (cachedSynth != nullptr && key == synthKey)
        {
            synth = cachedSynth;
            warning = cachedWarning;
        }
    }
    if (synth == nullptr)
    {
        synth = synthesise (r, warning);
        if (synth->ok())
        {
            std::lock_guard<std::mutex> l (cacheMutex);
            cachedSynth = synth; synthKey = key; cachedWarning = warning;
            cachedProcessed.reset(); processedKey.clear();
        }
    }
    clip->warning = warning;
    clip->engineName = synth->engineName;

    if (! synth->ok())
    {
        clip->error = synth->error.isNotEmpty() ? synth->error : "Sintesi fallita";
        return clip;
    }
    clip->phonemeText = synth->phonemeText;
    for (auto& s : synth->syllables) clip->syllableLabels.add (s.label);

    // ---- 2) trasformazioni WORLD (cache)
    WorldTransform wt;
    wt.pitchSemis = r.voice.pitchSemis;
    wt.intonation = r.voice.intonation;
    wt.formantSemis = r.voice.formantSemis;
    juce::String pKey = key + "|" + juce::String (wt.pitchSemis, 2) + "|" + juce::String (wt.intonation, 3) + "|" + juce::String (wt.formantSemis, 2);
    for (size_t i = 0; i < synth->syllables.size() && i < r.edits.size(); ++i)
        if (std::abs (r.edits[i].pitchSemis) > 0.01f)
        {
            const auto& s = synth->syllables[i];
            wt.syllablePitch.push_back ({ s.start, s.end, r.edits[i].pitchSemis });
            pKey << "|s" << (int) i << ":" << juce::String (r.edits[i].pitchSemis, 2);
        }

    std::shared_ptr<SynthResult> processed;
    if (wt.isIdentity())
        processed = synth;
    else
    {
        {
            std::lock_guard<std::mutex> l (cacheMutex);
            if (cachedProcessed != nullptr && processedKey == pKey) processed = cachedProcessed;
        }
        if (processed == nullptr)
        {
            auto p = std::make_shared<SynthResult> (*synth);
            p->audio.makeCopyOf (synth->audio);
            WorldVoice::process (p->audio, p->sampleRate, wt);
            std::lock_guard<std::mutex> l (cacheMutex);
            cachedProcessed = p; processedKey = pKey;
            processed = p;
        }
    }

    // ---- 3) robotizza
    SynthResult voice;
    voice.sampleRate = processed->sampleRate;
    voice.syllables = processed->syllables;
    voice.audio.makeCopyOf (processed->audio);
    robotize (voice.audio, voice.sampleRate, r.voice.robotHz, r.voice.robotize);

    // ---- 4) ritmo
    auto q = Quantizer::process (voice, r.rhythm, r.groove, r.edits);
    clip->lengthBeats = q.lengthBeats;

    // ---- 5) ricampionamento al sample rate della DAW
    const double ratio = voice.sampleRate / r.targetSampleRate;
    const int inN = q.audio.getNumSamples();
    const int outN = (int) std::ceil (q.lengthBeats * 60.0 / r.rhythm.bpm * r.targetSampleRate);
    const int outLen = juce::jmax (outN, (int) std::ceil (inN / ratio));

    juce::AudioBuffer<float> padded (1, inN + 64);
    padded.clear();
    padded.copyFrom (0, 0, q.audio, 0, 0, inN);

    clip->audio.setSize (1, outLen);
    clip->audio.clear();
    juce::WindowedSincInterpolator interp;
    interp.process (ratio, padded.getReadPointer (0), clip->audio.getWritePointer (0), juce::jmin (outLen, (int) std::floor (inN / ratio)));

    for (auto m : q.markers)
    {
        m.start = (int) std::round (m.start / ratio);
        m.end = (int) std::round (m.end / ratio);
        m.nucleus = (int) std::round (m.nucleus / ratio);
        clip->markers.push_back (m);
    }

    // ---- 6) livello: loudness (LUFS) o picco a -3 dBFS
    if (r.loudnessLufs < -0.5)
        // la clip e' mono ma suona su due canali: in BS.1770 vale +3 dB rispetto alla misura mono
        Loudness::normalise (clip->audio, r.targetSampleRate, r.loudnessLufs - 3.0103, -1.0f);
    else
    {
        const float peak = clip->audio.getMagnitude (0, 0, clip->audio.getNumSamples());
        if (peak > 1.0e-4f)
            clip->audio.applyGain (juce::Decibels::decibelsToGain (-3.0f) / peak);
    }

    clip->renderMs = juce::Time::getMillisecondCounterHiRes() - t0;
    return clip;
}

} // namespace vocalis
