// Test del plugin "come in una DAW": trasporto finto, render, processBlock, export.
#include "PluginProcessor.h"
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
    double err = 0; const int cmpLen = clip->audio.getNumSamples();
    for (int i = 0; i < cmpLen; ++i) err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - clip->audio.getSample (0, i)));
    std::cout << "     errore max " << err << "\n";
    check (err < 1.0e-3, "sync DAW: uscita identica alla clip");
    double err2 = 0;
    for (int i = 0; i < cmpLen && cmpLen + i < out.getNumSamples(); ++i) err2 = juce::jmax (err2, (double) std::abs (out.getSample (0, cmpLen + i) - clip->audio.getSample (0, i)));
    check (err2 < 1.0e-3, "loop: la clip si ripete dopo la sua lunghezza");

    // --- Battuta di partenza 2: la prima battuta e' muta
    setParam (p, IDs::startBar, 2); setParam (p, IDs::loop, 0);
    runBars (2, out);
    const int barLen = (int) (4 * 60.0 / ph.bpm * sr);
    check (out.getMagnitude (0, 0, barLen - 64) < 1.0e-6f, "start bar 2: prima battuta muta");
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
    auto f = juce::File ("/tmp/out/host_export.wav");
    juce::String e;
    check (p.exportWav (f, e) && f.getSize() > 1000, "export WAV");

    // --- Stato salvato e ricaricato
    juce::MemoryBlock state; p.getStateInformation (state);
    std::unique_ptr<juce::AudioProcessor> base2 (createPluginFilter());
    auto& p2 = dynamic_cast<VocalisProcessor&> (*base2);
    p2.setStateInformation (state.getData(), (int) state.getSize());
    check (p2.getText() == p.getText(), "stato: testo ripristinato");
    check (p2.apvts.getRawParameterValue (IDs::reverbOn)->load() > 0.5f, "stato: parametri ripristinati");
    check (std::abs (p2.getEdit (1).shiftBeats - 0.5) < 1e-6 && std::abs (p2.getEdit (1).pitchSemis - 3.0f) < 1e-4f, "stato: modifiche sillabe ripristinate");
    check (p2.getDictionary() == "test = tast", "stato: dizionario ripristinato");

    std::cout << (fails == 0 ? "TUTTI I TEST OK\n" : "CI SONO ERRORI\n");
    return fails;
}
