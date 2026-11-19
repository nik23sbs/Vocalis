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
#include "Engine/TokenSyllables.h"
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

    if (args.contains ("--token-syllables"))
    {
        const auto ps = Phonemizer::phonemize (opt ("--token-syllables", ""), opt ("--lang", "it"), {});
        for (auto& t : TokenSyllables::build (ps))
            std::cout << t.label << (t.stressed ? "'" : "") << (t.pauseSteps > 0 ? " +" + juce::String (t.pauseSteps) : juce::String()) << "\n";
        return 0;
    }

    if (args.contains ("--kokoro-run"))
    {
        // prova del modello controllabile: passata naturale, poi durate imposte (+1 frame su ogni vocale) e F0 +4 st
        KokoroEngine k;
        std::cout << "Controllabile: " << (KokoroEngine::isControllable() ? "si" : "no") << "\n";
        const auto ps = Phonemizer::filterToVocab (Phonemizer::phonemize (opt ("--kokoro-run", "Ciao a tutti."), "it", {}));
        VoiceMix mix; mix.id[0] = opt ("--voice", "if_sara");
        juce::String err;
        auto a = k.run (ps, mix, 1.0f, 0, {}, err);
        if (err.isNotEmpty()) { std::cout << "ERRORE: " << err << "\n"; return 2; }
        KokoroPass p; p.useDur = true; p.durations = a.durations;
        for (int i = 0; i < ps.length(); ++i)
            if (TokenSyllables::isVowel (ps[i])) p.durations[(size_t) i + 1] = juce::jmax ((int64_t) 1, p.durations[(size_t) i + 1]) + 1;
        int64_t tot = 0; for (auto d : p.durations) tot += d;
        auto b = k.run (ps, mix, 1.0f, 0, p, err);
        if (err.isNotEmpty()) { std::cout << "ERRORE: " << err << "\n"; return 2; }
        const bool durOk = std::abs ((int64_t) b.audio.size() - tot * KokoroEngine::samplesPerFrame) <= KokoroEngine::samplesPerFrame;
        const bool f0Ok = (int64_t) b.f0.size() == 2 * tot;
        p.useF0 = true; p.f0 = b.f0;
        for (auto& f : p.f0) f *= std::pow (2.0f, 4.0f / 12.0f);
        auto c = k.run (ps, mix, 1.0f, 3, p, err);
        std::cout << "Durate imposte: " << (durOk ? "ok" : "ERRATE") << "  F0: " << (f0Ok ? "ok" : "ERRATA")
                  << "  armonia: " << (c.audio.size() == b.audio.size() ? "ok" : "ERRATA") << " " << err << "\n";
        return durOk && f0Ok && err.isEmpty() ? 0 : 3;
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
    {
        const float blend = juce::jlimit (0.0f, 1.0f, opt ("--blend", "0").getFloatValue());
        v.mix.id[0] = opt ("--voice", "if_sara"); v.mix.w[0] = 1.0f - blend;
        v.mix.id[1] = opt ("--voiceB", ""); v.mix.w[1] = blend;
        if (v.mix.id[1].isEmpty()) v.mix.w[0] = 1.0f;
        v.variant = opt ("--seed", "0").getIntValue();
    }
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

    // ---- v3
    const auto vm = opt ("--vmode", "rap");
    r.mode = vm == "parlato" ? VocalMode::Parlato : vm == "canto" ? VocalMode::Canto : VocalMode::Rap;
    if (rs.mode == RhythmSettings::Mode::Free) r.mode = VocalMode::Parlato;
    for (auto n : juce::StringArray::fromTokens (opt ("--notes", ""), ",", ""))
    {
        auto q = juce::StringArray::fromTokens (n, ":", "");
        if (q.size() >= 3) r.notes.push_back ({ q[1].getDoubleValue(), q[2].getDoubleValue(), q[0].getIntValue() });
    }
    r.vibratoCents = opt ("--vibrato", "0").getFloatValue();
    r.expressivity = opt ("--expr", "0.3").getFloatValue();
    r.harmony.degrees1 = opt ("--harm1", "0").getIntValue();
    r.harmony.degrees2 = opt ("--harm2", "0").getIntValue();
    r.harmony.key = opt ("--key", "0").getIntValue();
    r.harmony.minor = args.contains ("--minor");
    r.harmony.doubleOn = args.contains ("--double");
    r.breath = opt ("--breath", "0").getFloatValue();
    rs.legato = opt ("--legato", "0.9").getDoubleValue();
    auto flag = [&] (const char* name, std::function<void (SyllableEdit&)> f) {
        for (int i = 0; i < args.size(); ++i)
            if (args[i] == name && i + 1 < args.size())
            {
                const int idx = args[i + 1].getIntValue();
                if ((int) r.edits.size() <= idx) r.edits.resize ((size_t) idx + 1);
                f (r.edits[(size_t) idx]);
            }
    };
    flag ("--whisper", [] (SyllableEdit& e) { e.whisper = true; });
    flag ("--adlib", [] (SyllableEdit& e) { e.adlib = true; });
    flag ("--mute", [] (SyllableEdit& e) { e.muted = true; });

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
    if (auto x = opt ("--py-model", {}); x.isNotEmpty())
    {
        // modello eseguito da Tests/tools/kokoro_server.py (test locali senza ONNX Runtime C++)
        const juce::File dir (x);
        renderer.setModelRunner ([dir] (const juce::String& chunk, const VoiceMix& mix, float speed, int row, const KokoroPass& p, juce::String& err)
        {
            juce::StringArray l;
            l.add (chunk);
            juce::String m; for (int i = 0; i < 4; ++i) if (mix.id[i].isNotEmpty() && mix.w[i] > 1e-4f) m << mix.id[i] << ":" << mix.w[i] << ",";
            l.add (m); l.add (juce::String (row)); l.add (juce::String (speed));
            juce::String d (p.useDur ? "1" : "0"); for (auto x : p.durations) d << " " << (int) x; l.add (p.useDur ? d : juce::String ("0"));
            juce::String f (p.useF0 ? "1" : "0"); if (p.useF0) for (auto x : p.f0) f << " " << x; l.add (f);
            juce::String en (p.useN ? "1" : "0"); if (p.useN) for (auto x : p.n) en << " " << x; l.add (en);
            dir.getChildFile ("done.txt").deleteFile();
            dir.getChildFile ("req.tmp").replaceWithText (l.joinIntoString ("\n"), false, false, "\n");
            dir.getChildFile ("req.tmp").moveFileTo (dir.getChildFile ("req.txt"));
            KokoroChunkResult r;
            for (int w = 0; w < 120000 && ! dir.getChildFile ("done.txt").existsAsFile(); w += 2) juce::Thread::sleep (2);
            if (! dir.getChildFile ("done.txt").existsAsFile()) { err = "server python non risponde"; return r; }
            juce::MemoryBlock mb; dir.getChildFile ("out.bin").loadFileAsData (mb);
            r.audio.resize (mb.getSize() / sizeof (float));
            std::memcpy (r.audio.data(), mb.getData(), r.audio.size() * sizeof (float));
            auto lines = juce::StringArray::fromLines (dir.getChildFile ("out.txt").loadFileAsString());
            for (auto t : juce::StringArray::fromTokens (lines[0], " ", "")) r.durations.push_back (t.getLargeIntValue());
            for (auto t : juce::StringArray::fromTokens (lines[1], " ", "")) r.f0.push_back (t.getFloatValue());
            for (auto t : juce::StringArray::fromTokens (lines[2], " ", "")) r.n.push_back (t.getFloatValue());
            return r;
        });
    }
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
    if (clip->timed)
    {
        // vocali sulla griglia: errore rispetto a (posizione + anticipo) * campioni per quarto
        const double spb = 60.0 / clip->bpm * clip->sampleRate;
        double worst = 0.0;
        for (auto& m : clip->markers)
            worst = juce::jmax (worst, std::abs (m.nucleus - (m.gridBeat + clip->prerollBeats) * spb) / clip->sampleRate * 1000.0);
        std::cout << "Anticipo: " << clip->prerollBeats << " quarti  Canali: " << clip->audio.getNumChannels()
                  << "  Errore griglia max: " << worst << " ms\n";
        juce::StringArray nn; for (auto& m : clip->markers) nn.add (juce::String (m.noteMidi));
        std::cout << "Note: " << nn.joinIntoString (" ") << "\n";
    }
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
    for (int c = 0; c < 2; ++c) stbuf.copyFrom (c, 0, clip->audio, juce::jmin (c, clip->audio.getNumChannels() - 1), 0, clip->audio.getNumSamples());
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
