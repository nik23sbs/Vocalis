#pragma once
#include "Engine/ESpeakEngine.h"
#include "DSP/Quantizer.h"
#include "DSP/GrooveExtractor.h"
#include <juce_events/juce_events.h>
#include <mutex>

namespace vocalis
{

struct RenderRequest
{
    juce::String text;
    VoiceSettings voice;
    RhythmSettings rhythm;
    GrooveTemplate groove;
    double targetSampleRate = 44100.0;
};

/** Clip pronta per la riproduzione: mono al sample rate della DAW. */
struct RenderedClip : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<RenderedClip>;

    juce::AudioBuffer<float> audio;
    std::vector<ClipMarker> markers;
    double lengthBeats = 0.0;
    double bpm = 120.0;
    double sampleRate = 44100.0;
    juce::String error;
    juce::String phonemeText;   // fonemi riconosciuti (per info)
};

/** Esegue la sintesi in un thread separato; la richiesta piu' recente sostituisce quelle in coda. */
class Renderer : private juce::Thread
{
public:
    Renderer();
    ~Renderer() override;

    void request (const RenderRequest& r);
    bool isBusy() const { return busy.load(); }

    /** Chiamata sul message thread quando una clip e' pronta. */
    std::function<void (RenderedClip::Ptr)> onComplete;

    /** Render sincrono (usato dal thread e dal tool CLI). */
    RenderedClip::Ptr renderNow (const RenderRequest& r);

private:
    void run() override;

    ESpeakEngine engine;
    std::mutex cacheMutex;
    juce::String cachedKey;
    std::shared_ptr<SynthResult> cachedSynth;

    std::mutex reqMutex;
    std::unique_ptr<RenderRequest> pending;
    std::atomic<bool> busy { false };
    juce::WaitableEvent wake;
};

} // namespace vocalis
