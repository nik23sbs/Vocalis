// Test del plugin "come in una DAW": trasporto finto, render, processBlock, export.
#include "PluginProcessor.h"
#include "PyModelRunner.h"
#include "PluginEditor.h"
#include <iostream>

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 100.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static int fails = 0;
static void check (bool ok, const char* what) { std::cout << (ok ? "OK   " : "FAIL ") << what << "\n"; if (! ok) ++fails; }

static void setParam (VocalisProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<VocalisProcessor&> (*base);
    // VOCALIS_PY_MODEL: test locali con il modello eseguito in Python (vedi Tests/tools/kokoro_server.py)
    const auto py = juce::SystemStats::getEnvironmentVariable ("VOCALIS_PY_MODEL", {});
    if (py.isNotEmpty()) p.setModelRunnerForTests (makePyModelRunner (juce::File (py)));
    const bool v3 = py.isNotEmpty() || vocalis::KokoroEngine::isControllable();
    std::cout << "     motore " << (v3 ? "v3 (a tempo)" : "v2 (riserva)") << "\n";

    const double sr = 48000.0; const int block = 512;
    FakePlayHead ph;
    p.setPlayHead (&ph);
    p.prepareToPlay (sr, block);
    setParam (p, IDs::eqOn, 0); setParam (p, IDs::compOn, 0); setParam (p, IDs::deessOn, 0); setParam (p, IDs::limiterOn, 0);

    // un primo blocco per far leggere BPM al plugin
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
    p.processBlock (buf, midi);

    p.setText ("Ciao, questo e' un test del plugin.");
    p.renderBlocking();
    auto clip = p.getClip();
    check (clip != nullptr && clip->audio.getNumSamples() > 0, "clip generata");
    if (clip == nullptr) { std::cout << "Stato: " << p.getStatus() << "\n"; return 1; }
    check (std::abs (clip->bpm - 100.0) < 0.01, "clip al BPM della DAW (100)");
    check (std::abs (clip->sampleRate - sr) < 1, "clip al sample rate della DAW");
    std::cout << "     " << clip->markers.size() << " sillabe, " << clip->lengthBeats << " quarti\n";

    // --- Sync DAW: l'uscita deve coincidere con la clip
    auto runBars = [&] (int bars, juce::AudioBuffer<float>& out) {
        const int total = (int) (bars * 4 * 60.0 / ph.bpm * sr);
        out.setSize (2, total); out.clear();
        ph.ppq = 0.0;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> b (2, n); juce::MidiBuffer m;
            p.processBlock (b, m);
            out.copyFrom (0, pos, b, 0, 0, n); out.copyFrom (1, pos, b, 1, 0, n);
            ph.ppq += n * ph.bpm / 60.0 / sr;
        }
    };

    juce::AudioBuffer<float> out;
    runBars ((int) (clip->lengthBeats / 4) * 2, out);
    // con l'anticipo (motore v3) alla battuta di partenza si sente il campione P quarti dentro la clip
    const int pre = (int) std::lround (clip->prerollBeats * 60.0 / ph.bpm * sr);
    const int loopLen = (int) std::lround (clip->lengthBeats * 60.0 / ph.bpm * sr);
    double err = 0; const int cmpLen = juce::jmin (clip->audio.getNumSamples() - pre, loopLen - pre);
    for (int i = 0; i < cmpLen; ++i) err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - clip->audio.getSample (0, i + pre)));
    std::cout << "     errore max " << err << "\n";
    check (err < 1.0e-3, "sync DAW: uscita identica alla clip");
    double err2 = 0;
    for (int i = 0; i < cmpLen && loopLen + i < out.getNumSamples(); ++i)
    {
        // al giro successivo: la clip riparte, e nell'ultimo quarto si somma l'anticipo del giro dopo
        const float expect = clip->audio.getSample (0, i + pre) + (i >= loopLen - pre ? clip->audio.getSample (0, i - (loopLen - pre)) : 0.0f);
        err2 = juce::jmax (err2, (double) std::abs (out.getSample (0, loopLen + i) - expect));
    }
    check (err2 < 1.0e-3, "loop: la clip si ripete dopo la sua lunghezza");

    // --- Battuta di partenza 2: la prima battuta e' muta
    setParam (p, IDs::startBar, 2); setParam (p, IDs::loop, 0);
    runBars (2, out);
    const int barLen = (int) (4 * 60.0 / ph.bpm * sr);
    check (out.getMagnitude (0, 0, barLen - pre - 64) < 1.0e-6f, "start bar 2: prima battuta muta (fino all'anticipo)");
    check (out.getMagnitude (0, barLen, barLen) > 0.05f, "start bar 2: suona dalla seconda battuta");
    setParam (p, IDs::startBar, 1); setParam (p, IDs::loop, 1);

    // --- Trigger MIDI
    setParam (p, IDs::playMode, 1);
    ph.playing = false;
    {
        juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 100);
        p.processBlock (b, m);
        check (b.getMagnitude (0, 0, 100) < 1.0e-6f && b.getMagnitude (0, 100, block - 100) >= 0.0f, "trigger MIDI: parte sul campione della nota");
        float peak = 0;
        for (int k = 0; k < 40; ++k) { juce::MidiBuffer m2; p.processBlock (b, m2); peak = juce::jmax (peak, b.getMagnitude (0, 0, block)); }
        check (peak > 0.05f, "trigger MIDI: la clip suona");
    }

    // --- Modifiche per sillaba: muta la prima, sposta la seconda; devono restare nello stato
    {
        vocalis::SyllableEdit e; e.muted = true;
        p.setEdit (0, e);
        vocalis::SyllableEdit e2; e2.shiftBeats = 0.5; e2.pitchSemis = 3.0f;
        p.setEdit (1, e2);
        p.renderBlocking();
        auto c2 = p.getClip();
        check (c2 != nullptr && c2->markers.size() >= 2 && c2->markers[0].muted, "editor: sillaba 1 muta");
        const double spb = 60.0 / c2->bpm * c2->sampleRate;
        const int moved = c2->markers[1].nucleus;
        p.resetEdits();
        p.renderBlocking();
        auto c3 = p.getClip();
        std::cout << "     vocale sillaba 2: con modifica " << moved << ", senza " << c3->markers[1].nucleus << ", mezzo quarto = " << 0.5 * spb << "\n";
        check (std::abs ((moved - c3->markers[1].nucleus) - 0.5 * spb) < 2.0, "editor: sillaba 2 spostata di mezzo quarto");
        p.setEdit (1, e2);
    }

    // --- Dizionario
    p.setDictionary ("test = tast");
    p.renderBlocking();
    check (p.getClip() != nullptr && p.getClip()->error.isEmpty(), "dizionario applicato senza errori");

    // --- Effetti + export
    setParam (p, IDs::reverbOn, 1); setParam (p, IDs::delayOn, 1); setParam (p, IDs::robotize, 0);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vocalis_test/host_export.wav");
    f.getParentDirectory().createDirectory();
    juce::String e;
    check (p.exportWav (f, e) && f.getSize() > 1000, "export WAV");

    // --- Vocalis 3: voce a tempo, anticipo, stereo, armonie, Canto da file MIDI, cattura MIDI, pronuncia
    setParam (p, IDs::reverbOn, 0); setParam (p, IDs::delayOn, 0);
    p.resetEdits();
    if (v3)
    {
        setParam (p, IDs::mode, 1);   // Rap
        p.setText ("Questa voce cade a tempo sulla griglia.");
        p.renderBlocking();
        auto c = p.getClip();
        check (c != nullptr && c->timed, "v3: clip generata dal motore a tempo");
        if (c != nullptr && c->timed)
        {
            const double spb = 60.0 / c->bpm * c->sampleRate;
            double worst = 0;
            for (auto& m : c->markers) worst = juce::jmax (worst, std::abs (m.nucleus - (m.gridBeat + c->prerollBeats) * spb) / c->sampleRate * 1000.0);
            std::cout << "     errore griglia " << worst << " ms, anticipo " << c->prerollBeats << " quarti\n";
            check (worst < 1.0, "v3: vocali sulla griglia (< 1 ms)");
            check (c->audio.getNumChannels() == 2, "v3: clip stereo");

            // sync con anticipo: con partenza alla battuta 2 la clip inizia P quarti prima
            setParam (p, IDs::startBar, 2); setParam (p, IDs::loop, 0); setParam (p, IDs::playMode, 0);
            ph.playing = true;
            runBars (3, out);
            const int barLen2 = (int) (4 * 60.0 / ph.bpm * sr);
            const int startAt = barLen2 - (int) std::lround (c->prerollBeats * 60.0 / ph.bpm * sr);
            double e3 = 0;
            for (int i = 0; i < c->audio.getNumSamples() - 1 && startAt + i < out.getNumSamples(); ++i)
                e3 = juce::jmax (e3, (double) std::abs (out.getSample (1, startAt + i) - c->audio.getSample (1, i)));
            check (e3 < 2.0e-3, "v3: sync DAW con anticipo, uscita = clip (canale destro)");
            setParam (p, IDs::startBar, 1); setParam (p, IDs::loop, 1);
        }

        // armonie: i canali si separano
        setParam (p, IDs::harm1, 2); setParam (p, IDs::harmWidth, 100);
        p.renderBlocking();
        if (auto h = p.getClip(); h != nullptr)
        {
            double diff = 0, tot = 0;
            for (int i = 0; i < h->audio.getNumSamples(); ++i) { const float l = h->audio.getSample (0, i), r = h->audio.getSample (1, i); diff += (l - r) * (l - r); tot += l * l + r * r; }
            check (diff / juce::jmax (1.0e-9, tot) > 0.01, "v3: armonia aperta in stereo");
        }
        setParam (p, IDs::harm1, 0);

        // Canto da file MIDI: 4 note, una per sillaba
        {
            juce::MidiMessageSequence seq;
            const int notes[] = { 60, 64, 67, 72 };
            for (int i = 0; i < 4; ++i)
            {
                seq.addEvent (juce::MidiMessage::noteOn (1, notes[i], (juce::uint8) 100), i * 480.0);
                seq.addEvent (juce::MidiMessage::noteOff (1, notes[i]), i * 480.0 + 400.0);
            }
            seq.updateMatchedPairs();
            juce::MidiFile mf; mf.setTicksPerQuarterNote (480); mf.addTrack (seq);
            auto mid = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vocalis_test/test_melody.mid");
            mid.getParentDirectory().createDirectory();
            mid.deleteFile();
            { juce::FileOutputStream fos (mid); mf.writeTo (fos); }
            juce::String err;
            check (p.loadMidiFile (mid, err) && p.getMelody().size() == 4, "MIDI: file caricato (4 note)");
            setParam (p, IDs::mode, 2);   // Canto
            p.setText ("La luna sul mar");
            p.renderBlocking();
            auto c = p.getClip();
            bool ok = c != nullptr && c->markers.size() >= 4;
            for (int i = 0; ok && i < 4; ++i) ok = c->markers[(size_t) i].noteMidi == notes[i] && std::abs (c->markers[(size_t) i].gridBeat - i) < 1e-6;
            check (ok, "Canto: ogni sillaba sulla sua nota MIDI");
        }

        // pronuncia: la parola della sillaba e la riga nel dizionario
        check (p.getTextWordForSyllable (1) == "luna", "pronuncia: parola della sillaba 2 = luna");
        p.setPronunciation ("luna", juce::CharPointer_UTF8 ("lˈuuna"));
        check (p.getDictionary().contains (juce::CharPointer_UTF8 ("luna = /lˈuuna/")), "pronuncia: scritta nel dizionario");
        p.setPronunciation ("luna", {});
        check (! p.getDictionary().contains ("luna ="), "pronuncia: rimossa");
        setParam (p, IDs::mode, 1);
    }

    // cattura MIDI dalla DAW: note suonate mentre il trasporto va
    {
        p.startMidiCapture();
        ph.playing = true; ph.ppq = 0.0;
        juce::AudioBuffer<float> b (2, block);
        for (int k = 0; k < 20; ++k)
        {
            juce::MidiBuffer m;
            if (k == 2) m.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 10);
            if (k == 8) m.addEvent (juce::MidiMessage::noteOff (1, 62), 10);
            if (k == 10) m.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 100), 10);
            if (k == 16) m.addEvent (juce::MidiMessage::noteOff (1, 65), 10);
            p.processBlock (b, m);
            ph.ppq += block * ph.bpm / 60.0 / sr;
        }
        p.stopMidiCapture();
        auto mel = p.getMelody();
        check (mel.size() == 2 && mel[0].pitch == 62 && mel[1].pitch == 65 && mel[1].startBeat > mel[0].startBeat, "cattura MIDI: 2 note registrate");
    }

    // modifiche v3 nello stato (e ripristino di cio' che il blocco v3 ha cambiato)
    setParam (p, IDs::reverbOn, 1);
    {
        vocalis::SyllableEdit e2; e2.shiftBeats = 0.5; e2.pitchSemis = 3.0f;
        p.setEdit (1, e2);
    }
    {
        vocalis::SyllableEdit e; e.note = 67; e.onsetScale = 0.5f; e.vibrato = true; e.whisper = true; e.adlib = true; e.glide = true;
        p.setEdit (2, e);
    }

    // --- Stato salvato e ricaricato
    juce::MemoryBlock state; p.getStateInformation (state);
    std::unique_ptr<juce::AudioProcessor> base2 (createPluginFilter());
    auto& p2 = dynamic_cast<VocalisProcessor&> (*base2);
    p2.setStateInformation (state.getData(), (int) state.getSize());
    check (p2.getText() == p.getText(), "stato: testo ripristinato");
    check (p2.apvts.getRawParameterValue (IDs::reverbOn)->load() > 0.5f, "stato: parametri ripristinati");
    check (std::abs (p2.getEdit (1).shiftBeats - 0.5) < 1e-6 && std::abs (p2.getEdit (1).pitchSemis - 3.0f) < 1e-4f, "stato: modifiche sillabe ripristinate");
    check (p2.getDictionary() == p.getDictionary(), "stato: dizionario ripristinato");
    {
        auto e = p2.getEdit (2);
        check (e.note == 67 && std::abs (e.onsetScale - 0.5f) < 1e-4f && e.vibrato && e.whisper && e.adlib && e.glide, "stato: modifiche v3 ripristinate");
        check (p2.getMelody().size() == p.getMelody().size(), "stato: melodia ripristinata");
    }

    // istantanea dell'interfaccia (VOCALIS_SNAPSHOT=file.png), con una sillaba selezionata
    if (auto snap = juce::SystemStats::getEnvironmentVariable ("VOCALIS_SNAPSHOT", {}); snap.isNotEmpty())
    {
        setParam (p, IDs::mode, 2); setParam (p, IDs::harm1, 1); setParam (p, IDs::orbX, 35); p.setMelody ({}); setParam (p, IDs::mExpr, 70);
        p.setText (juce::CharPointer_UTF8 ("Questa è la nuova voce di Vocalis, ogni sillaba cade a tempo"));
        p.renderBlocking();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1280, 800);
        for (int tab = 0; tab < 4; ++tab)
        {
            if (auto* e = dynamic_cast<VocalisEditor*> (ed.get())) e->selectForSnapshot (5, tab);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = juce::File (snap).getSiblingFile (juce::File (snap).getFileNameWithoutExtension() + "_" + juce::String (tab) + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "     istantanea " << f.getFullPathName() << "\n";
        }
    }

    std::cout << (fails == 0 ? "TUTTI I TEST OK\n" : "CI SONO ERRORI\n");
    return fails;
}
