#pragma once
#include "Engine/ESpeakEngine.h"
#include "Engine/KokoroEngine.h"
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
    std::vector<SyllableEdit> edits;
    double targetSampleRate = 44100.0;
    double loudnessLufs = -16.0;   // 0 = disattivato (solo normalizzazione di picco)
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
    double stepBeats = 0.25;
    juce::String error, warning;
    juce::String engineName;
    juce::String phonemeText;
    juce::StringArray syllableLabels;   // etichette delle sillabe sintetizzate (per riconoscere le modifiche)
    double renderMs = 0.0;
};

/** Esegue la sintesi in un thread separato; la richiesta piu' recente sostituisce quelle in coda.
    Tre livelli di cache: sintesi (testo + voce), trasformazioni WORLD, poi ritmo/effetti sempre ricalcolati. */
class Renderer : private juce::Thread
{
public:
    Renderer();
    ~Renderer() override;

    void request (const RenderRequest& r);
    bool isBusy() const { return busy.load(); }

    std::function<void (RenderedClip::Ptr)> onComplete;

    RenderedClip::Ptr renderNow (const RenderRequest& r);

    /** Solo per i test: usa questa sintesi al posto del motore per la richiesta con questa chiave. */
    void injectSynth (const juce::String& key, std::shared_ptr<SynthResult> s)
    {
        std::lock_guard<std::mutex> l (cacheMutex);
        cachedSynth = std::move (s); synthKey = key; cachedWarning.clear();
        cachedProcessed.reset(); processedKey.clear();
    }

private:
    void run() override;
    std::shared_ptr<SynthResult> synthesise (const RenderRequest& r, juce::String& warning);

    ESpeakEngine espeak;
    KokoroEngine kokoro;

    std::mutex cacheMutex;
    juce::String synthKey, processedKey;
    std::shared_ptr<SynthResult> cachedSynth, cachedProcessed;
    juce::String cachedWarning;

    std::mutex reqMutex;
    std::unique_ptr<RenderRequest> pending;
    std::atomic<bool> busy { false };
    juce::WaitableEvent wake;
};

} // namespace vocalis
