#pragma once
#include "Engine/ESpeakEngine.h"
#include "Engine/KokoroEngine.h"
#include "DSP/Quantizer.h"
#include "DSP/GrooveExtractor.h"
#include "DSP/TimingPlanner.h"
#include "DSP/PitchPlanner.h"
#include <map>
#include <juce_events/juce_events.h>
#include <mutex>

namespace vocalis
{

/** Armonie e doppi neurali (stessi tempi della voce principale, F0 spostata). */
struct HarmonySettings
{
    int key = 0;              // 0 = Do ... 11 = Si
    bool minor = false;
    int degrees1 = 0;         // gradi della scala (2 = terza sopra, 4 = quinta, -7 = ottava sotto, 0 = spenta)
    int degrees2 = 0;
    float level = 0.5f;       // guadagno delle armonie rispetto alla voce
    float width = 0.8f;       // apertura stereo 0..1
    bool doubleOn = false;
    float doubleLevel = 0.5f;

    bool any() const { return degrees1 != 0 || degrees2 != 0 || doubleOn; }
};

struct RenderRequest
{
    VocalMode mode = VocalMode::Rap;
    std::vector<NoteEvent> notes;      // melodia (Canto)
    float vibratoCents = 0.0f, glideMs = 60.0f, expressivity = 0.3f;
    HarmonySettings harmony;
    float breath = 0.0f;               // respiri prima delle frasi (0..1)
    double vocalShiftBeats = 0.0;      // sposta tutta la frase (quarti)
    bool anticipate = false;           // true: le consonanti iniziali partono prima del battere (clip con anticipo)

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
    juce::StringArray syllableWords;    // parola (testo dei fonemi) a cui appartiene ogni sillaba
    std::vector<int> syllableWordIndex; // indice della parola (0, 1, ...) di ogni sillaba
    double renderMs = 0.0;
    double prerollBeats = 0.0;          // la clip parte questo numero di quarti prima della battuta di partenza
    std::vector<float> f0Display;       // F0 finale (Hz, 0 = non intonato), un valore ogni f0Hop secondi dall'inizio
    double f0Hop = 0.0125;
    bool timed = false;                 // generata dal motore v3 (voce gia' a tempo)
    int triggerStart = 0;               // Trigger MIDI: da qui parte la voce (prima consonante, senza l'anticipo muto)
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

    /** Esecuzione del modello (sostituibile nei test). Di default: KokoroEngine::run. */
    using ModelRunner = std::function<KokoroChunkResult (const juce::String& chunk, const VoiceMix& mix, float speed,
                                                         int styleRow, const KokoroPass& pass, juce::String& error)>;
    void setModelRunner (ModelRunner m) { std::lock_guard<std::mutex> l (cacheMutex); runner = std::move (m); passCache.clear(); }

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
    bool renderTimed (const RenderRequest& r, RenderedClip& clip);
    std::shared_ptr<KokoroChunkResult> runCached (const juce::String& chunk, const VoiceMix& mix, float speed, int row,
                                                  const KokoroPass& pass, juce::String& error);
    RenderedClip::Ptr renderLegacy (const RenderRequest& r, RenderedClip::Ptr clip, double t0);

    ModelRunner runner;
    std::map<juce::String, std::shared_ptr<KokoroChunkResult>> passCache;
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
