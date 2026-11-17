// Tool di test: render offline senza DAW.
// Uso: VocalisCLI "testo" out.wav [--voice m|f|r] [--variant m3] [--lang it] [--bpm 120]
//                 [--mode free|grid] [--div 3] [--swing 0.5] [--groove file.wav] [--groove-amt 1]
//                 [--formant 1.0] [--robot 0..1] [--fx] [--phonemes]
#include <juce_audio_formats/juce_audio_formats.h>
#include "Render/Renderer.h"
#include "DSP/EffectsChain.h"
#include <iostream>

using namespace vocalis;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 3)
    {
        std::cout << "Uso: VocalisCLI \"testo\" out.wav [opzioni]\n";
        return 1;
    }

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    auto opt = [&] (const juce::String& name, const juce::String& def) {
        const int i = args.indexOf (name);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : def;
    };

    RenderRequest r;
    r.text = args[0];
    r.targetSampleRate = 44100.0;

    const auto voice = opt ("--voice", "m");
    const auto type = voice == "f" ? VoiceType::Female : voice == "r" ? VoiceType::Robot : VoiceType::Male;
    r.voice.variant = opt ("--variant", ESpeakEngine::variantsFor (type).front().id);
    r.voice.language = opt ("--lang", "it");
    r.voice.formant = opt ("--formant", "1.0").getFloatValue();
    r.voice.robotize = opt ("--robot", type == VoiceType::Robot ? "1" : "0").getFloatValue();
    r.voice.pitch = opt ("--pitch", "50").getIntValue();
    r.voice.rate = opt ("--rate", "160").getIntValue();

    r.rhythm.bpm = opt ("--bpm", "120").getDoubleValue();
    r.rhythm.mode = opt ("--mode", "grid") == "free" ? RhythmSettings::Mode::Free : RhythmSettings::Mode::Grid;
    r.rhythm.stepBeats = RhythmSettings::divisionToBeats (opt ("--div", "3").getIntValue());
    r.rhythm.swing = opt ("--swing", "0.5").getDoubleValue();
    r.rhythm.grooveAmount = opt ("--groove-amt", "1").getDoubleValue();
    r.rhythm.accentAmount = opt ("--accent", "0.5").getDoubleValue();

    const auto groovePath = opt ("--groove", {});
    if (groovePath.isNotEmpty())
    {
        juce::String err;
        r.groove = GrooveExtractor::extractFromFile (juce::File::getCurrentWorkingDirectory().getChildFile (groovePath), r.rhythm.bpm, err);
        std::cout << "Groove: " << r.groove.onsetBeats.size() << " colpi, " << r.groove.lengthBeats
                  << " quarti, bpm sample " << r.groove.sampleBpm << " " << err << "\n";
        for (size_t i = 0; i < r.groove.steps.size(); ++i)
        {
            auto& s = r.groove.steps[i];
            std::cout << "  step " << i << (s.hasHit ? " X " : " . ") << "off=" << s.offsetBeats << " vel=" << s.velocity << "\n";
        }
    }

    if (args.contains ("--phonemes"))
    {
        ESpeakEngine eng;
        auto sres = eng.synthesize (r.text, r.voice);
        for (auto& p : sres.phonemes)
            std::cout << "[" << p.name << "] " << p.start << "-" << p.end << (p.isVowel ? " V" : "") << (p.isPause ? " P" : "") << "\n";
        for (auto w : sres.wordStarts) std::cout << "word@" << w << "\n";
    }

    Renderer renderer;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto clip = renderer.renderNow (r);
    const auto t1 = juce::Time::getMillisecondCounterHiRes();

    if (clip->error.isNotEmpty())
    {
        std::cout << "ERRORE: " << clip->error << "\n";
        return 2;
    }

    std::cout << "Render in " << (t1 - t0) << " ms, " << clip->audio.getNumSamples() / clip->sampleRate
              << " s, " << clip->lengthBeats << " quarti, " << clip->markers.size() << " sillabe\n";
    std::cout << "Sillabe: " << clip->phonemeText << "\n";

    // Stereo + effetti (come l'export del plugin)
    EffectsParams fx;
    if (args.contains ("--fx"))
    {
        fx.reverbOn = true; fx.delayOn = true; fx.chorusOn = true;
    }
    const double tail = EffectsChain::tailSeconds (fx, r.rhythm.bpm);
    const int len = clip->audio.getNumSamples() + (int) (tail * clip->sampleRate);
    juce::AudioBuffer<float> st (2, len);
    st.clear();
    st.copyFrom (0, 0, clip->audio, 0, 0, clip->audio.getNumSamples());
    st.copyFrom (1, 0, clip->audio, 0, 0, clip->audio.getNumSamples());
    EffectsChain chain;
    chain.prepare (clip->sampleRate, 512);
    chain.process (st, fx, r.rhythm.bpm);

    auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (outFile),
                                                                      clip->sampleRate, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (st, 0, st.getNumSamples());
    std::cout << "Scritto " << outFile.getFullPathName() << "\n";
    return 0;
}
