#pragma once
#include "VoiceTypes.h"
#include "KokoroVoices.h"

namespace vocalis
{

/** Motore neurale Kokoro-82M (ONNX Runtime, 24 kHz).
    Il modello e le voci vengono cercati in:
      $VOCALIS_MODELS, ~/Library/Application Support/Vocalis/models, /Library/Application Support/Vocalis/models,
      <bundle>/Contents/Resources/models, ~/.config/Vocalis/models (Linux). */
class KokoroEngine : public VoiceEngine
{
public:
    KokoroEngine();
    ~KokoroEngine() override;

    SynthResult synthesize (const juce::String& text, const VoiceSettings& settings) override;

    /** true se ONNX Runtime e' compilato e i file del modello esistono. */
    static bool isAvailable (juce::String* reason = nullptr);
    static juce::File findModelDir();

    static constexpr double sampleRate = 24000.0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vocalis
