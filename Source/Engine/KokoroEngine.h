#pragma once
#include "VoiceTypes.h"
#include "KokoroVoices.h"

namespace vocalis
{

/** Motore neurale Kokoro-82M (ONNX Runtime, 24 kHz).
    Il modello e le voci vengono cercati in:
      $VOCALIS_MODELS, ~/Library/Application Support/Vocalis/models, /Library/Application Support/Vocalis/models,
      <bundle>/Contents/Resources/models, ~/.config/Vocalis/models (Linux). */
/** Controlli del modello "ctrl": durate (frame da 600 campioni, lunghezza = token + 2) e F0 (Hz, 2 per frame). */
struct KokoroPass
{
    std::vector<int64_t> durations;
    std::vector<float> f0;
    bool useDur = false, useF0 = false;
};

struct KokoroChunkResult
{
    std::vector<float> audio;          // 24 kHz, senza tagli
    std::vector<int64_t> durations;    // durate usate (token + 2)
    std::vector<float> f0;             // F0 usata (vuota se il modello non e' controllabile)
};

class KokoroEngine : public VoiceEngine
{
public:
    KokoroEngine();
    ~KokoroEngine() override;

    SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) override;

    /** true se ONNX Runtime e' compilato e i file del modello esistono. */
    static bool isAvailable (juce::String* reason = nullptr);
    static juce::File findModelDir();

    /** true se il modello installato accetta durate/F0 imposte (kokoro-v1.0-ctrl.onnx). */
    static bool isControllable();

    /** Una passata del modello su un blocco di fonemi (gia' filtrati sul vocabolario, <= 480 token). */
    KokoroChunkResult run (const juce::String& chunk, const VoiceMix& mix, float speed, int styleRowOffset,
                           const KokoroPass& pass, juce::String& error);

    static constexpr int samplesPerFrame = 600;

    static constexpr double sampleRate = 24000.0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vocalis
