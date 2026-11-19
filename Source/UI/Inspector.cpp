#include "Inspector.h"
#include "Timeline.h"
#include "PluginProcessor.h"

namespace vocalis
{

namespace
{
    void styleTile (juce::Slider& s, double lo, double hi, double step, double def)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, def);
        s.setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.25f));
        s.setColour (juce::Slider::backgroundColourId, Colours::card);
        s.setColour (juce::Slider::textBoxTextColourId, Colours::text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    juce::Rectangle<int> titleRow (juce::Rectangle<int>& r, int h = 18) { auto t = r.removeFromTop (h); r.removeFromTop (4); return t; }
}

// ============================================================================ SyllablePage

SyllablePage::SyllablePage (VocalisProcessor& p) : proc (p)
{
    styleTile (tone, -24.0, 24.0, 0.5, 0.0);
    styleTile (length, 0.25, 4.0, 0.05, 1.0);
    styleTile (gain, -24.0, 12.0, 0.5, 0.0);
    styleTile (onset, 0.3, 2.0, 0.05, 1.0);
    length.textFromValueFunction = [] (double v) { return "x" + juce::String (v, 2); };
    onset.textFromValueFunction = [] (double v) { return "x" + juce::String (v, 2); };
    gain.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    tone.textFromValueFunction = [this] (double v) {
        if ((int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2) return Timeline::noteName ((int) v);
        return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; };
    for (auto* s : { &tone, &length, &gain, &onset })
    {
        s->onValueChange = [this] { if (! updating) push(); };
        addAndMakeVisible (s);
    }
    for (auto* c : { &vib, &glide, &whisper, &adlib, &mute })
    {
        c->onClick = [this] { push(); };
        addAndMakeVisible (c);
    }
    vib.setTooltip ("Vibrato su questa sillaba");
    glide.setTooltip ("Portamento lento dalla nota precedente");
    whisper.setTooltip ("Sillaba sussurrata (senza intonazione)");
    adlib.setTooltip ("Ripete la sillaba un'ottava sopra, ritardata e di lato");

    ipa.setFont (juce::FontOptions (15.0f));
    ipa.setTextToShowWhenEmpty ("pronuncia (IPA)", Colours::textDim);
    ipa.onReturnKey = [this] { applyIpa.triggerClick(); };
    addAndMakeVisible (ipa);
    applyIpa.onClick = [this] { if (word.isNotEmpty()) proc.setPronunciation (word, ipa.getText()); };
    resetIpa.onClick = [this] { if (word.isNotEmpty()) proc.setPronunciation (word, {}); };
    resetSyl.onClick = [this] { if (selected >= 0) proc.setEdit (selected, {}); };
    resetAll.onClick = [this] { proc.resetEdits(); };
    for (auto* b : { &applyIpa, &resetIpa, &resetSyl, &resetAll }) addAndMakeVisible (b);
    setSelected (-1);
}

void SyllablePage::setSelected (int s)
{
    selected = s;
    refresh();
}

void SyllablePage::refresh()
{
    auto clip = proc.getClip();
    const bool has = clip != nullptr && selected >= 0 && selected < (int) clip->markers.size();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &tone, &length, &gain, &onset, &vib, &glide, &whisper, &adlib, &mute, &ipa, &applyIpa, &resetIpa, &resetSyl })
        c->setVisible (has);
    if (! has) { title.clear(); repaint(); return; }

    const auto& m = clip->markers[(size_t) selected];
    const auto e = proc.getEdit (selected);
    const bool canto = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2;
    updating = true;
    if (canto) { tone.setRange (24.0, 96.0, 1.0); tone.setValue (e.note >= 0 ? e.note : juce::jmax (24, m.noteMidi), juce::dontSendNotification); }
    else       { tone.setRange (-24.0, 24.0, 0.5); tone.setValue (e.pitchSemis, juce::dontSendNotification); }
    length.setValue (e.lengthScale, juce::dontSendNotification);
    gain.setValue (e.gainDb, juce::dontSendNotification);
    onset.setValue (e.onsetScale, juce::dontSendNotification);
    vib.setToggleState (e.vibrato, juce::dontSendNotification);
    glide.setToggleState (e.glide, juce::dontSendNotification);
    whisper.setToggleState (e.whisper, juce::dontSendNotification);
    adlib.setToggleState (e.adlib, juce::dontSendNotification);
    mute.setToggleState (e.muted, juce::dontSendNotification);
    tone.updateText();
    updating = false;

    title = m.label;
    word = proc.getTextWordForSyllable (selected);
    const double pos = m.gridBeat;
    const int bar = (int) std::floor (pos / 4.0), beat = (int) std::floor (pos) % 4, sub = (int) std::round ((pos - std::floor (pos)) * 4.0);
    info = (word.isNotEmpty() ? "\"" + word + "\"  -  " : juce::String()) + "battuta " + juce::String (bar + 1) + "." + juce::String (beat + 1) + "." + juce::String (sub + 1);
    if (! ipa.hasKeyboardFocus (false))
    {
        auto custom = word.isNotEmpty() ? proc.getPronunciation (word) : juce::String();
        ipa.setText (custom.isNotEmpty() ? custom : (selected < clip->syllableWords.size() ? clip->syllableWords[selected] : juce::String()), false);
    }
    ipa.setEnabled (word.isNotEmpty());
    applyIpa.setEnabled (word.isNotEmpty());
    resetIpa.setEnabled (word.isNotEmpty() && proc.getPronunciation (word).isNotEmpty());
    repaint();
}

void SyllablePage::push()
{
    if (selected < 0) return;
    auto e = proc.getEdit (selected);
    const bool canto = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2;
    if (canto) e.note = (int) tone.getValue(); else e.pitchSemis = (float) tone.getValue();
    e.lengthScale = length.getValue();
    e.gainDb = (float) gain.getValue();
    e.onsetScale = (float) onset.getValue();
    e.vibrato = vib.getToggleState(); e.glide = glide.getToggleState();
    e.whisper = whisper.getToggleState(); e.adlib = adlib.getToggleState(); e.muted = mute.getToggleState();
    proc.setEdit (selected, e);
}

void SyllablePage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    if (title.isEmpty())
    {
        drawSectionTitle (g, r.removeFromTop (18), "Sillaba selezionata");
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Clicca una sillaba nella timeline per modificarne nota, durata, volume, stile e pronuncia.",
                          r.removeFromTop (60), juce::Justification::topLeft, 4);
        return;
    }
    drawSectionTitle (g, r.removeFromTop (18), "Sillaba selezionata");
    g.setColour (Colours::warn);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText (title, r.removeFromTop (36), juce::Justification::centredLeft, true);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (info, r.removeFromTop (18), juce::Justification::centredLeft, true);

    const bool canto = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2;
    auto label = [&] (juce::Component& c, const juce::String& t) {
        g.setColour (Colours::textDim); g.setFont (juce::FontOptions (11.0f));
        g.drawText (t, c.getX(), c.getY() - 15, c.getWidth(), 14, juce::Justification::centredLeft); };
    label (tone, canto ? "Nota" : "Intonazione");
    label (length, "Durata");
    label (gain, "Volume");
    label (onset, "Consonanti");
    drawSectionTitle (g, { 0, vib.getY() - 22, getWidth(), 16 }, "Stile della sillaba");
    drawSectionTitle (g, { 0, ipa.getY() - 22, getWidth(), 16 }, "Pronuncia" + (word.isNotEmpty() ? " di \"" + word + "\"" : juce::String()));
}

void SyllablePage::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18 + 36 + 18 + 22);
    auto row1 = r.removeFromTop (34);
    tone.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (0, 0).withTrimmedRight (5));
    length.setBounds (row1.withTrimmedLeft (5));
    r.removeFromTop (22);
    auto row2 = r.removeFromTop (34);
    gain.setBounds (row2.removeFromLeft (row2.getWidth() / 2).withTrimmedRight (5));
    onset.setBounds (row2.withTrimmedLeft (5));

    r.removeFromTop (30);
    auto chips = r.removeFromTop (28);
    const int cw = (chips.getWidth() - 8) / 3;
    vib.setBounds (chips.removeFromLeft (cw)); chips.removeFromLeft (4);
    glide.setBounds (chips.removeFromLeft (cw)); chips.removeFromLeft (4);
    whisper.setBounds (chips);
    r.removeFromTop (6);
    chips = r.removeFromTop (28);
    adlib.setBounds (chips.removeFromLeft (cw)); chips.removeFromLeft (4);
    mute.setBounds (chips.removeFromLeft (cw));

    r.removeFromTop (30);
    ipa.setBounds (r.removeFromTop (32));
    r.removeFromTop (6);
    auto b = r.removeFromTop (30);
    applyIpa.setBounds (b.removeFromLeft (b.getWidth() / 2).withTrimmedRight (4));
    resetIpa.setBounds (b.withTrimmedLeft (4));

    auto bottom = r.removeFromBottom (30);
    resetSyl.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).withTrimmedRight (4));
    resetAll.setBounds (bottom.withTrimmedLeft (4));
}

// ============================================================================ RhythmPage

RhythmPage::RhythmPage (VocalisProcessor& p)
    : proc (p),
      division (p.apvts, IDs::division, "Griglia"), bpmSource (p.apvts, IDs::bpmSource, "BPM"),
      playMode (p.apvts, IDs::playMode, "Riproduzione"), loudness (p.apvts, IDs::loudness, "Livello"),
      steps (p.apvts, IDs::stepsPerSyl, "Step/sill.", Colours::accent), swing (p.apvts, IDs::swing, "Swing", Colours::accent, "%"),
      legato (p.apvts, IDs::legato, "Legato", Colours::accent, "%"), groove (p.apvts, IDs::grooveAmt, "Groove", Colours::accent2, "%"),
      accents (p.apvts, IDs::accentAmt, "Accenti", Colours::accent2, "%"), bpm (p.apvts, IDs::bpmManual, "BPM man.", Colours::accent2, "", 1),
      grooveView (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &division, &bpmSource, &playMode, &loudness, &steps, &swing, &legato, &groove, &accents, &bpm, &grooveView, &startBar, &loop })
        addAndMakeVisible (c);
    startBar.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    startAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, IDs::startBar, startBar);
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, IDs::loop, loop);
}

void RhythmPage::refresh()
{
    const bool manual = (int) proc.apvts.getRawParameterValue (IDs::bpmSource)->load() == 1;
    bpm.setEnabled (manual);
    const bool grid = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() != 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &division, &steps, &swing, &legato, &groove, &accents }) c->setEnabled (grid);
    const bool sync = (int) proc.apvts.getRawParameterValue (IDs::playMode)->load() == 0;
    startBar.setEnabled (sync); loop.setEnabled (sync);
    grooveView.repaint();
}

void RhythmPage::paint (juce::Graphics& g)
{
    drawSectionTitle (g, { 0, 0, getWidth(), 16 }, "Griglia e swing");
    drawSectionTitle (g, { 0, grooveView.getY() - 20, getWidth(), 16 }, "Groove da un sample");
    drawSectionTitle (g, { 0, playMode.getY() - 22, getWidth(), 16 }, "Riproduzione");
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Dalla battuta", startBar.getX(), startBar.getY() - 15, 120, 14, juce::Justification::left);
}

void RhythmPage::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    auto row = r.removeFromTop (42);
    division.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (5));
    bpmSource.setBounds (row.withTrimmedLeft (5));
    r.removeFromTop (8);
    auto knobs = r.removeFromTop (72);
    const int kw = knobs.getWidth() / 3;
    steps.setBounds (knobs.removeFromLeft (kw)); swing.setBounds (knobs.removeFromLeft (kw)); legato.setBounds (knobs);
    knobs = r.removeFromTop (72);
    groove.setBounds (knobs.removeFromLeft (kw)); accents.setBounds (knobs.removeFromLeft (kw)); bpm.setBounds (knobs);
    r.removeFromTop (26);
    grooveView.setBounds (r.removeFromTop (78));
    r.removeFromTop (30);
    row = r.removeFromTop (42);
    playMode.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (5));
    loudness.setBounds (row.withTrimmedLeft (5));
    r.removeFromTop (22);
    row = r.removeFromTop (28);
    startBar.setBounds (row.removeFromLeft (150));
    loop.setBounds (row.withTrimmedLeft (12));
}

// ============================================================================ SingPage

SingPage::SingPage (VocalisProcessor& p)
    : proc (p),
      vibrato (p.apvts, IDs::vibrato, "Vibrato", Colours::warn, " cent"), glide (p.apvts, IDs::glide, "Glide", Colours::warn, " ms"),
      harmLevel (p.apvts, IDs::harmLevel, "Livello", Colours::pink, "%"), harmWidth (p.apvts, IDs::harmWidth, "Apertura", Colours::pink, "%"),
      doubleLevel (p.apvts, IDs::doubleLevel, "Doppio", Colours::pink, "%"),
      key (p.apvts, IDs::key, "Tonalita'"), scale (p.apvts, IDs::scale, "Scala"),
      harm1 (p.apvts, IDs::harm1, "Armonia 1"), harm2 (p.apvts, IDs::harm2, "Armonia 2")
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadMidi, &capture, &clearMelody, &vibrato, &glide, &harmLevel, &harmWidth,
                                &doubleLevel, &key, &scale, &harm1, &harm2, &doubleOn })
        addAndMakeVisible (c);
    doubleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, IDs::doubleOn, doubleOn);
    loadMidi.setTooltip ("Melodia da un file MIDI (una nota per sillaba). Puoi anche trascinarlo sulla timeline.");
    capture.setTooltip ("Suona le note sulla traccia della DAW mentre il trasporto va: diventano la melodia");
    loadMidi.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Melodia (file MIDI)", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            juce::String err;
            if (proc.loadMidiFile (fc.getResult(), err))
            {
                if (auto* m = proc.apvts.getParameter (IDs::mode)) m->setValueNotifyingHost (m->convertTo0to1 (2.0f));
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Melodia", err);
        });
    };
    capture.onClick = [this] { proc.isCapturingMidi() ? proc.stopMidiCapture() : proc.startMidiCapture(); refresh(); };
    clearMelody.onClick = [this] { proc.setMelody ({}); };
}

void SingPage::refresh()
{
    const auto n = proc.getMelody().size();
    melodyInfo = n == 0 ? juce::String ("Nessuna melodia: le note seguono l'intonazione naturale (modificale nella timeline).")
                        : juce::String ((int) n) + " note nella melodia";
    capture.setToggleState (proc.isCapturingMidi(), juce::dontSendNotification);
    capture.setButtonText (proc.isCapturingMidi() ? "Stop registrazione" : "Registra dalla DAW");
    clearMelody.setEnabled (n > 0);
    harmLevel.setEnabled (proc.apvts.getRawParameterValue (IDs::harm1)->load() > 0.5f || proc.apvts.getRawParameterValue (IDs::harm2)->load() > 0.5f);
    doubleLevel.setEnabled (proc.apvts.getRawParameterValue (IDs::doubleOn)->load() > 0.5f);
    repaint();
}

void SingPage::paint (juce::Graphics& g)
{
    drawSectionTitle (g, { 0, 0, getWidth(), 16 }, "Melodia");
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (melodyInfo, 0, clearMelody.getBottom() + 4, getWidth(), 30, juce::Justification::topLeft, 2);
    drawSectionTitle (g, { 0, vibrato.getY() - 20, getWidth(), 16 }, "Espressione");
    drawSectionTitle (g, { 0, harm1.getY() - 22, getWidth(), 16 }, "Armonie e doppio");
}

void SingPage::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    auto row = r.removeFromTop (30);
    loadMidi.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (4));
    capture.setBounds (row.withTrimmedLeft (4));
    r.removeFromTop (6);
    clearMelody.setBounds (r.removeFromTop (28).removeFromLeft (100));
    r.removeFromTop (60);
    auto k = r.removeFromTop (72);
    vibrato.setBounds (k.removeFromLeft (k.getWidth() / 2)); glide.setBounds (k);
    r.removeFromTop (6);
    row = r.removeFromTop (42);
    key.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (5));
    scale.setBounds (row.withTrimmedLeft (5));
    r.removeFromTop (30);
    row = r.removeFromTop (42);
    harm1.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (5));
    harm2.setBounds (row.withTrimmedLeft (5));
    r.removeFromTop (8);
    k = r.removeFromTop (72);
    const int kw = k.getWidth() / 3;
    harmLevel.setBounds (k.removeFromLeft (kw)); harmWidth.setBounds (k.removeFromLeft (kw)); doubleLevel.setBounds (k);
    r.removeFromTop (8);
    doubleOn.setBounds (r.removeFromTop (28).removeFromLeft (110));
}

// ============================================================================ FxPage

FxPage::Row::Row (VocalisProcessor& p, const char* onId, juce::String name,
                  std::vector<std::tuple<const char*, juce::String, juce::String, int>> ks)
    : toggle (std::move (name), Colours::ok)
{
    addAndMakeVisible (toggle);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, onId, toggle);
    for (auto& [id, label, suffix, dec] : ks)
        addAndMakeVisible (knobs.add (new LabelledKnob (p.apvts, id, label, Colours::accent2, suffix, dec)));
}

void FxPage::Row::paint (juce::Graphics& g)
{
    g.setColour (Colours::card);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
}

void FxPage::Row::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    toggle.setBounds (r.removeFromLeft (98).withSizeKeepingCentre (98, 28));
    r.removeFromLeft (4);
    const int kw = knobs.isEmpty() ? 0 : juce::jmin (52, r.getWidth() / knobs.size());
    for (auto* k : knobs) k->setBounds (r.removeFromLeft (kw));
}

FxPage::FxPage (VocalisProcessor& p)
{
    using K = std::tuple<const char*, juce::String, juce::String, int>;
    rows.add (new Row (p, IDs::deessOn, "De-esser", { K { IDs::deessAmount, "Quantita'", "", 2 } }));
    rows.add (new Row (p, IDs::eqOn, "EQ", { K { IDs::eqLowCut, "Low cut", " Hz", 0 }, K { IDs::eqPresence, "Presenza", " dB", 1 }, K { IDs::eqHighCut, "High cut", " Hz", 0 } }));
    rows.add (new Row (p, IDs::compOn, "Compressore", { K { IDs::compThresh, "Soglia", " dB", 1 }, K { IDs::compRatio, "Ratio", ":1", 1 } }));
    rows.add (new Row (p, IDs::distOn, "Saturazione", { K { IDs::distDrive, "Drive", " dB", 1 }, K { IDs::distMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::crushOn, "Bitcrusher", { K { IDs::crushBits, "Bit", "", 1 }, K { IDs::crushDown, "Down", "x", 0 }, K { IDs::crushMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::ringOn, "Ring mod", { K { IDs::ringFreq, "Freq", " Hz", 0 }, K { IDs::ringMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::doublerOn, "Doubler", { K { IDs::doublerMix, "Mix", "", 2 }, K { IDs::doublerWidth, "Width", "", 2 } }));
    rows.add (new Row (p, IDs::chorusOn, "Chorus", { K { IDs::chorusRate, "Rate", " Hz", 2 }, K { IDs::chorusDepth, "Depth", "", 2 }, K { IDs::chorusMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::delayOn, "Delay", { K { IDs::delayFb, "Feedback", "", 2 }, K { IDs::delayMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::reverbOn, "Riverbero", { K { IDs::reverbSize, "Size", "", 2 }, K { IDs::reverbDamp, "Damp", "", 2 }, K { IDs::reverbMix, "Mix", "", 2 } }));
    rows.add (new Row (p, IDs::limiterOn, "Limiter", { K { IDs::outGain, "Volume", " dB", 1 } }));
    for (auto* r : rows) content.addAndMakeVisible (r);
    delayDiv = std::make_unique<LabelledCombo> (p.apvts, IDs::delayDiv, "Tempo");
    content.addAndMakeVisible (*delayDiv);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void FxPage::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = getWidth() - 10, rh = 66;
    content.setSize (w, rows.size() * (rh + 6));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * (rh + 6), w, rh);
    // il tempo del delay accanto alle sue manopole
    auto* d = rows[8];
    delayDiv->setBounds (d->getRight() - 66, d->getY() + 8, 60, 44);
}

// ============================================================================ Inspector

Inspector::Inspector (VocalisProcessor& p) : proc (p), sylPage (p), rhythmPage (p), singPage (p), fxPage (p), meter (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &sylPage, &rhythmPage, &singPage, &fxPage, &meter })
        addChildComponent (c);
    meter.setVisible (true);
    showTab (0);
}

void Inspector::showTab (int i)
{
    tab = i;
    sylPage.setVisible (i == 0); rhythmPage.setVisible (i == 1); singPage.setVisible (i == 2); fxPage.setVisible (i == 3);
    refresh();
    repaint();
}

void Inspector::setSelected (int s)
{
    sylPage.setSelected (s);
    if (s >= 0 && tab != 0) showTab (0);
}

void Inspector::refresh()
{
    sylPage.refresh();
    rhythmPage.refresh();
    singPage.refresh();
}

void Inspector::mouseDown (const juce::MouseEvent& e)
{
    auto bar = getLocalBounds().reduced (16, 16).removeFromTop (38).toFloat();
    if (! bar.contains (e.position)) return;
    const int i = juce::jlimit (0, tabs.size() - 1, (int) ((e.position.x - bar.getX()) / (bar.getWidth() / tabs.size())));
    showTab (i);
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (Colours::side);
    g.setColour (Colours::panelEdge);
    g.drawVerticalLine (0, 0, (float) getHeight());

    auto bar = getLocalBounds().reduced (16, 16).removeFromTop (38).toFloat();
    g.setColour (juce::Colour (0xff1b1d26));
    g.fillRoundedRectangle (bar, 10.0f);
    const float w = (bar.getWidth() - 8.0f) / tabs.size();
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (bar.getX() + 4.0f + i * w, bar.getY() + 4.0f, w, bar.getHeight() - 8.0f);
        if (i == tab) { g.setColour (juce::Colour (0xff2a2d3a)); g.fillRoundedRectangle (cell, 7.0f); }
        g.setColour (i == tab ? Colours::text : juce::Colour (0xffb4b7c7));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (tabs[i], cell, juce::Justification::centred);
    }
    drawSectionTitle (g, { 16, meter.getY() - 20, 200, 16 }, "Uscita");
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (16, 16);
    r.removeFromTop (38 + 14);
    meter.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (26);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &sylPage, &rhythmPage, &singPage, &fxPage })
        c->setBounds (r);
}

} // namespace vocalis
