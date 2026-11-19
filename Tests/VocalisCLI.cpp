// Tool di test: render offline senza DAW.
//   VocalisCLI --phonemize "testo" [--lang it]
//   VocalisCLI "testo" out.wav [--engine neural|formant] [--voice if_sara] [--voiceB im_nicola --blend 0.5]
//              [--lang it] [--speed 1] [--pitch 0] [--inton 1] [--formant 0] [--robot 0] [--variant UniRobot]
//              [--bpm 120] [--mode grid|free] [--div 3] [--steps 1] [--swing 0.5] [--pc 1] [--stretch vowels|whole|off]
//              [--groove loop.wav --groove-amt 1] [--lufs -16] [--fx] [--syllables]
//              [--kokoro-dump dir]   (usa audio.wav + durations.txt + phonemes.txt generati dal modello, senza ONNX)
//              [--edit "idx:shift:len:pitch:gain"]...
#include <juce_audio_formats/juce_audio_formats.h>
#include "Render/Renderer.h"
#include "Engine/Phonemizer.h"
#include "Engine/KokoroAligner.h"
#include "Engine/Syllabifier.h"
#include "DSP/EffectsChain.h"
#include "DSP/Loudness.h"
#include <iostream>

using namespace vocalis;

static std::shared_ptr<SynthResult> loadKokoroDump (const juce::File& dir)
{
    auto r = std::make_shared<SynthResult>();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (dir.getChildFile ("audio.wav")));
    if (rd == nullptr) { r->error = "audio.wav mancante"; return r; }
    r->sampleRate = rd->sampleRate;
    r->audio.setSize (1, (int) rd->lengthInSamples);
    rd->read (&r->audio, 0, (int) rd->lengthInSamples, 0, true, false);

    std::vector<int64_t> dur;
    for (auto t : juce::StringArray::fromTokens (dir.getChildFile ("durations.txt").loadFileAsString(), " \n", ""))
        if (t.isNotEmpty()) dur.push_back (t.getLargeIntValue());
    r->phonemeText = dir.getChildFile ("phonemes.txt").loadFileAsString().trim();
    r->engineName = "Kokoro (dump)";

    // come KokoroEngine: taglia il silenzio iniziale/finale del modello
    int64_t tot = 0; for (auto d : dur) tot += d;
    const double spf = (double) r->audio.getNumSamples() / (double) juce::jmax ((int64_t) 1, tot);
    const int lead = (int) std::round (juce::jmax ((int64_t) 0, dur.front() - 2) * spf);
    const int tail = (int) std::round (juce::jmax ((int64_t) 0, dur.back() - 3) * spf);
    const int n = r->audio.getNumSamples();
    auto ph = KokoroAligner::align (r->phonemeText, dur, n, -lead);
    juce::AudioBuffer<float> trimmed (1, n - lead - tail);
    trimmed.copyFrom (0, 0, r->audio, 0, lead, n - lead - tail);
    r->audio = trimmed;
    for (auto& p : ph) { p.start = juce::jlimit (0, trimmed.getNumSamples(), p.start); p.end = juce::jlimit (p.start, trimmed.getNumSamples(), p.end); }
    r->phonemes = ph;
    r->syllables = Syllabifier::build (r->phonemes, r->audio, r->sampleRate);
    return r;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    auto opt = [&] (const juce::String& name, const juce::String& def) {
        const int i = args.indexOf (name);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : def;
    };

    if (args.contains ("--phonemize"))
    {
        std::cout << Phonemizer::phonemize (opt ("--phonemize", ""), opt ("--lang", "it"), opt ("--dict", "").replace ("\\n", "\n")).toStdString() << "\n";
        return 0;
    }

    if (args.size() < 2)
    {
        std::cout << "Uso: VocalisCLI \"testo\" out.wav [opzioni]\n";
        return 1;
    }

    RenderRequest r;
    r.text = args[0];
    r.targetSampleRate = 44100.0;

    auto& v = r.voice;
    v.engine = opt ("--engine", "neural") == "formant" ? EngineType::Formant : EngineType::Neural;
    v.voiceA = opt ("--voice", "if_sara");
    v.voiceB = opt ("--voiceB", "");
    v.blend = opt ("--blend", "0").getFloatValue();
    v.language = opt ("--lang", "it");
    v.speed = opt ("--speed", "1").getFloatValue();
    v.pitchSemis = opt ("--pitch", "0").getFloatValue();
    v.intonation = opt ("--inton", "1").getFloatValue();
    v.formantSemis = opt ("--formant", "0").getFloatValue();
    v.robotize = opt ("--robot", "0").getFloatValue();
    v.espeakVariant = opt ("--variant", "UniRobot");
    v.dictionary = opt ("--dict", "").replace ("\\n", "\n");

    auto& rs = r.rhythm;
    rs.bpm = opt ("--bpm", "120").getDoubleValue();
    rs.mode = opt ("--mode", "grid") == "free" ? RhythmSettings::Mode::Free : RhythmSettings::Mode::Grid;
    rs.stepBeats = RhythmSettings::divisionToBeats (opt ("--div", "3").getIntValue());
    rs.stepsPerSyllable = opt ("--steps", "1").getIntValue();
    rs.swing = opt ("--swing", "0.5").getDoubleValue();
    rs.pCenter = opt ("--pc", "1").getDoubleValue();
    const auto st = opt ("--stretch", "vowels");
    rs.stretch = st == "off" ? RhythmSettings::Stretch::Off : st == "whole" ? RhythmSettings::Stretch::Whole : RhythmSettings::Stretch::Vowels;
    rs.grooveAmount = opt ("--groove-amt", "1").getDoubleValue();
    rs.accentAmount = opt ("--accent", "0.5").getDoubleValue();
    r.loudnessLufs = opt ("--lufs", "-16").getDoubleValue();

    for (int i = 0; i < args.size(); ++i)
        if (args[i] == "--edit" && i + 1 < args.size())
        {
            auto p = juce::StringArray::fromTokens (args[i + 1], ":", "");
            const int idx = p[0].getIntValue();
            if ((int) r.edits.size() <= idx) r.edits.resize ((size_t) idx + 1);
            auto& e = r.edits[(size_t) idx];
            e.shiftBeats = p[1].getDoubleValue(); e.lengthScale = p.size() > 2 ? p[2].getDoubleValue() : 1.0;
            e.pitchSemis = p.size() > 3 ? p[3].getFloatValue() : 0.0f; e.gainDb = p.size() > 4 ? p[4].getFloatValue() : 0.0f;
        }

    const auto groovePath = opt ("--groove", {});
    if (groovePath.isNotEmpty())
    {
        juce::String err;
        r.groove = GrooveExtractor::extractFromFile (juce::File::getCurrentWorkingDirectory().getChildFile (groovePath), rs.bpm, err);
        std::cout << "Groove: " << r.groove.onsetBeats.size() << " colpi " << err << "\n";
    }

    Renderer renderer;
    if (auto d = opt ("--kokoro-dump", {}); d.isNotEmpty())
    {
        auto dump = loadKokoroDump (juce::File::getCurrentWorkingDirectory().getChildFile (d));
        if (! dump->ok()) { std::cout << "ERRORE dump: " << dump->error << "\n"; return 2; }
        renderer.injectSynth (r.voice.synthKey (r.text), dump);
    }

    auto clip = renderer.renderNow (r);
    if (clip->error.isNotEmpty())
    {
        std::cout << "ERRORE: " << clip->error << "\n";
        return 2;
    }

    std::cout << "Motore: " << clip->engineName << (clip->warning.isNotEmpty() ? "  (" + clip->warning + ")" : juce::String()) << "\n";
    std::cout << "Render in " << clip->renderMs << " ms, " << clip->audio.getNumSamples() / clip->sampleRate
              << " s, " << clip->lengthBeats << " quarti, " << clip->markers.size() << " sillabe\n";
    std::cout << "Sillabe: " << clip->syllableLabels.joinIntoString ("-") << "\n";
    if (args.contains ("--syllables"))
    {
        const double ms = 1000.0 / clip->sampleRate;
        for (auto& m : clip->markers)
            std::cout << "  " << m.syllableIndex << " " << m.label << "  start " << m.start * ms << "  vocale " << m.nucleus * ms
                      << "  fine " << m.end * ms << " ms  griglia " << m.gridBeat << "\n";
    }

    EffectsParams fx;
    if (args.contains ("--fx")) { fx.reverbOn = true; fx.delayOn = true; fx.doublerOn = true; }
    const double tail = EffectsChain::tailSeconds (fx, rs.bpm);
    const int len = clip->audio.getNumSamples() + (int) (tail * clip->sampleRate);
    juce::AudioBuffer<float> stbuf (2, len);
    stbuf.clear();
    for (int c = 0; c < 2; ++c) stbuf.copyFrom (c, 0, clip->audio, 0, 0, clip->audio.getNumSamples());
    EffectsChain chain;
    chain.prepare (clip->sampleRate, 512);
    chain.process (stbuf, fx, rs.bpm);
    std::cout << "Loudness: " << Loudness::integratedLufs (stbuf, clip->sampleRate) << " LUFS\n";

    auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (outFile), clip->sampleRate, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (stbuf, 0, stbuf.getNumSamples());
    std::cout << "Scritto " << outFile.getFullPathName() << "\n";
    return 0;
}
