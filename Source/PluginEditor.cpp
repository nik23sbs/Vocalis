#include "PluginEditor.h"

using namespace vocalis;

// ============================================================================ Helper

VocalisEditor::Knob* VocalisEditor::makeKnob (const char* id, const juce::String& label, const juce::String& suffix, int decimals)
{
    auto* k = knobs.add (new Knob());
    k->label.setText (label, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k->label.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->slider.setPopupDisplayEnabled (true, false, this);
    k->att = std::make_unique<SliderAtt> (proc.apvts, id, k->slider);
    // testo del valore: decimali e unita' coerenti (dopo l'attachment, che altrimenti usa il testo del parametro)
    k->slider.textFromValueFunction = [decimals] (double v) { return decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v)); };
    k->slider.setTextValueSuffix (suffix);
    k->slider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getDoubleValue(); };
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15); // ricrea la casella con il look&feel del plugin
    return k;
}

VocalisEditor::Combo* VocalisEditor::makeCombo (const char* id, const juce::String& label)
{
    auto* c = combos.add (new Combo());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->box.addItemList (p->choices, 1);
    c->label.setText (label, juce::dontSendNotification);
    c->label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    c->label.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (c->box);
    addAndMakeVisible (c->label);
    c->att = std::make_unique<ComboAtt> (proc.apvts, id, c->box);
    return c;
}

juce::ToggleButton* VocalisEditor::makeToggle (const char* id, const juce::String& label)
{
    auto* t = toggles.add (new juce::ToggleButton (label));
    addAndMakeVisible (t);
    toggleAtts.add (new ButtonAtt (proc.apvts, id, *t));
    return t;
}

VocalisEditor::FxRow* VocalisEditor::makeFx (const char* onId, const juce::String& name, std::initializer_list<Knob*> ks)
{
    auto* row = fxRows.add (new FxRow());
    row->toggle.setButtonText (name);
    addAndMakeVisible (row->toggle);
    row->att = std::make_unique<ButtonAtt> (proc.apvts, onId, row->toggle);
    for (auto* k : ks)
    {
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        row->knobs.add (k);
    }
    return row;
}

// ============================================================================ Costruzione

VocalisEditor::VocalisEditor (VocalisProcessor& p)
    : AudioProcessorEditor (&p), proc (p), grooveView (p), waveform (p), dragHandle (p)
{
    setLookAndFeel (&lnf);

    for (auto* panel : { &textPanel, &voicePanel, &rhythmPanel, &playPanel, &fxPanel })
        addAndMakeVisible (panel);

    // ---- Testo
    textEditor.setMultiLine (true, true);
    textEditor.setReturnKeyStartsNewLine (true);
    textEditor.setFont (juce::FontOptions (15.0f));
    textEditor.setText (proc.getText(), false);
    textEditor.setTextToShowWhenEmpty ("Scrivi qui il testo del vocal...", Colours::textDim);
    textEditor.onTextChange = [this] { proc.setText (textEditor.getText()); };
    addAndMakeVisible (textEditor);

    hintLabel.setText ("Virgole e punti diventano pause a tempo.", juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (hintLabel);
    languageCombo = makeCombo (IDs::language, "LINGUA");

    dictButton.setTooltip ("Pronuncia personalizzata: una riga per parola, \"parola = come si legge\" oppure \"parola = /ipa/\"");
    dictButton.onClick = [this] { showDictionary(); };
    addAndMakeVisible (dictButton);

    // ---- Voce
    for (auto* b : { &neuralButton, &robotButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    neuralButton.setTooltip ("Voce neurale Kokoro: naturale, 24 kHz");
    robotButton.setTooltip ("Sintesi a formanti eSpeak: voci robotiche e sintetiche");
    neuralButton.onClick = [this] { setEngine (0); };
    robotButton.onClick = [this] { setEngine (1); };

    voiceACombo = makeCombo (IDs::voiceA, "VOCE");
    voiceBCombo = makeCombo (IDs::voiceB, "MISCELA CON");
    robotVarCombo = makeCombo (IDs::robotVariant, "TIMBRO");
    blendK = makeKnob (IDs::orbX, "MISCELA", "%");
    speedK = makeKnob (IDs::speed, "VELOCITA'", "x", 2);
    pitchK = makeKnob (IDs::pitch, "PITCH", " st", 1);
    intonK = makeKnob (IDs::intonation, "INTONAZIONE", "%");
    formantK = makeKnob (IDs::formant, "FORMANTI", " st", 1);
    robotK = makeKnob (IDs::robotize, "ROBOTIZZA", "%");
    robotHzK = makeKnob (IDs::robotHz, "ROBOT HZ", " Hz");
    intonK->slider.setTooltip ("0% = monotono (robotico), 100% = naturale, 200% = esagerato");
    formantK->slider.setTooltip ("Timbro piu' scuro/grande (-) o chiaro/piccolo (+) senza cambiare l'intonazione");

    // ---- Ritmo
    modeCombo = makeCombo (IDs::mode, "MODALITA'");
    divCombo = makeCombo (IDs::division, "GRIGLIA");
    bpmSrcCombo = makeCombo (IDs::bpmSource, "BPM");
    stretchCombo = makeCombo (IDs::stretch, "STRETCH");
    stepsK = makeKnob (IDs::stepsPerSyl, "STEP/SILL.");
    swingK = makeKnob (IDs::swing, "SWING", "%");
    legatoK = makeKnob (IDs::legato, "LEGATO", "%");
    pcK = makeKnob (IDs::pCenter, "ATTACCO", "%");
    grooveK = makeKnob (IDs::grooveAmt, "GROOVE", "%");
    accentK = makeKnob (IDs::accentAmt, "ACCENTI", "%");
    bpmK = makeKnob (IDs::bpmManual, "BPM MAN.", "", 1);
    pcK->slider.setTooltip ("100% = la vocale cade esattamente sulla griglia (le consonanti anticipano), 0% = l'inizio della sillaba");
    addAndMakeVisible (grooveView);

    // ---- Riproduzione
    playModeCombo = makeCombo (IDs::playMode, "MODALITA'");
    loudCombo = makeCombo (IDs::loudness, "LIVELLO");
    loopToggle = makeToggle (IDs::loop, "Loop");
    startBarLabel.setText ("DALLA BATTUTA", juce::dontSendNotification);
    startBarLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    startBarLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (startBarSlider);
    startBarSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
    addAndMakeVisible (startBarLabel);
    startBarAtt = std::make_unique<SliderAtt> (proc.apvts, IDs::startBar, startBarSlider);

    // ---- Effetti
    makeFx (IDs::deessOn, "De-esser", { makeKnob (IDs::deessAmount, "QUANTITA'", "", 2) });
    makeFx (IDs::ringOn, "Ring mod", { makeKnob (IDs::ringFreq, "FREQ", " Hz"), makeKnob (IDs::ringMix, "MIX", "", 2) });
    makeFx (IDs::distOn, "Distorsione", { makeKnob (IDs::distDrive, "DRIVE", " dB", 1), makeKnob (IDs::distMix, "MIX", "", 2) });
    makeFx (IDs::crushOn, "Bitcrusher", { makeKnob (IDs::crushBits, "BIT", "", 1), makeKnob (IDs::crushDown, "DOWN", "x"), makeKnob (IDs::crushMix, "MIX", "", 2) });
    makeFx (IDs::eqOn, "Equalizzatore", { makeKnob (IDs::eqLowCut, "LOW CUT", " Hz"), makeKnob (IDs::eqPresence, "PRESENZA", " dB", 1), makeKnob (IDs::eqHighCut, "HIGH CUT", " Hz") });
    makeFx (IDs::compOn, "Compressore", { makeKnob (IDs::compThresh, "SOGLIA", " dB", 1), makeKnob (IDs::compRatio, "RATIO", ":1", 1) });
    makeFx (IDs::doublerOn, "Doubler", { makeKnob (IDs::doublerMix, "MIX", "", 2), makeKnob (IDs::doublerWidth, "WIDTH", "", 2) });
    makeFx (IDs::chorusOn, "Chorus", { makeKnob (IDs::chorusRate, "RATE", " Hz", 2), makeKnob (IDs::chorusDepth, "DEPTH", "", 2), makeKnob (IDs::chorusMix, "MIX", "", 2) });
    makeFx (IDs::delayOn, "Delay", { makeKnob (IDs::delayFb, "FEEDBACK", "", 2), makeKnob (IDs::delayMix, "MIX", "", 2) });
    makeFx (IDs::reverbOn, "Riverbero", { makeKnob (IDs::reverbSize, "SIZE", "", 2), makeKnob (IDs::reverbDamp, "DAMP", "", 2), makeKnob (IDs::reverbMix, "MIX", "", 2) });
    makeCombo (IDs::delayDiv, "TEMPO"); // combo del delay (posizionato nella riga del delay)
    outGainK = makeKnob (IDs::outGain, "VOLUME", " dB", 1);
    limiterToggle = makeToggle (IDs::limiterOn, "Limiter");

    // ---- Barra superiore e waveform
    addAndMakeVisible (waveform);
    addAndMakeVisible (dragHandle);
    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { proc.isPreviewing() ? proc.stopPreview() : proc.startPreview(); };
    renderButton.onClick = [this] { proc.forceRender(); };
    exportButton.onClick = [this] { exportWav(); };
    renderButton.setTooltip ("Rigenera il vocal (avviene comunque in automatico a ogni modifica)");
    exportButton.setTooltip ("Salva il vocal con gli effetti in un file WAV 24 bit");
    for (auto* b : { &playButton, &renderButton, &exportButton })
        addAndMakeVisible (b);

    for (auto* l : { &statusLabel, &bpmLabel, &dragHint })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        addAndMakeVisible (l);
    }
    dragHint.setText ("Sillabe: trascina = sposta  |  bordo = durata  |  Shift = intonazione  |  rotella = volume  |  doppio clic = muta  |  tasto destro = menu",
                      juce::dontSendNotification);
    dragHint.setJustificationType (juce::Justification::centredRight);

    proc.addChangeListener (this);
    waveform.setClip (proc.getClip());
    updateEngineUI();
    startTimerHz (10);

    setSize (1240, 900);
}

VocalisEditor::~VocalisEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

// ============================================================================ Logica

void VocalisEditor::setEngine (int e)
{
    if (auto* p = proc.apvts.getParameter (IDs::engine))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) e));
    updateEngineUI();
}

void VocalisEditor::updateEngineUI()
{
    const int e = (int) proc.apvts.getRawParameterValue (IDs::engine)->load();
    neuralButton.setToggleState (e == 0, juce::dontSendNotification);
    robotButton.setToggleState (e == 1, juce::dontSendNotification);
    if (e == shownEngine) return;
    shownEngine = e;

    const bool neural = e == 0;
    voiceACombo->box.setVisible (neural);
    voiceACombo->label.setVisible (neural);
    robotVarCombo->box.setVisible (! neural);
    robotVarCombo->label.setVisible (! neural);
    voiceBCombo->box.setEnabled (neural);
    blendK->slider.setEnabled (neural);
}

void VocalisEditor::showDictionary()
{
    struct DictPanel : public juce::Component
    {
        DictPanel (VocalisProcessor& p) : proc (p)
        {
            title.setText ("Dizionario di pronuncia", juce::dontSendNotification);
            title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            help.setText ("Una riga per parola. Esempi:\n  plugin = plaghin\n  okay = /ˌoʊkˈeɪ/   (IPA tra barre)",
                          juce::dontSendNotification);
            help.setFont (juce::FontOptions (12.0f));
            help.setColour (juce::Label::textColourId, Colours::textDim);
            editor.setMultiLine (true, false);
            editor.setReturnKeyStartsNewLine (true);
            editor.setFont (juce::FontOptions (14.0f));
            editor.setText (proc.getDictionary(), false);
            editor.onTextChange = [this] { proc.setDictionary (editor.getText()); };
            for (auto* c : std::initializer_list<juce::Component*> { &title, &help, &editor }) addAndMakeVisible (c);
            setSize (380, 260);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            title.setBounds (r.removeFromTop (22));
            help.setBounds (r.removeFromTop (52));
            editor.setBounds (r);
        }
        VocalisProcessor& proc;
        juce::Label title, help;
        juce::TextEditor editor;
    };

    juce::CallOutBox::launchAsynchronously (std::make_unique<DictPanel> (proc), dictButton.getScreenBounds(), nullptr);
}

void VocalisEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    waveform.setClip (proc.getClip());
    statusLabel.setText (proc.getStatus(), juce::dontSendNotification);
    if (! textEditor.hasKeyboardFocus (true) && textEditor.getText() != proc.getText())
        textEditor.setText (proc.getText(), false);
    grooveView.repaint();
}

void VocalisEditor::timerCallback()
{
    updateEngineUI();

    const bool manual = (int) proc.apvts.getRawParameterValue (IDs::bpmSource)->load() == 1;
    bpmK->slider.setEnabled (manual);
    bpmLabel.setText (juce::String (proc.getEffectiveBpm(), 1) + " BPM" + (manual ? " (manuale)" : " (progetto)"),
                      juce::dontSendNotification);

    const bool grid = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() != 0;
    for (auto* k : { stepsK, swingK, legatoK, grooveK, accentK })
        k->slider.setEnabled (grid);
    divCombo->box.setEnabled (grid);
    dictButton.setToggleState (proc.getDictionary().trim().isNotEmpty(), juce::dontSendNotification);

    const bool sync = (int) proc.apvts.getRawParameterValue (IDs::playMode)->load() == 0;
    startBarSlider.setEnabled (sync);
    loopToggle->setEnabled (sync);

    playButton.setButtonText (proc.isPreviewing() ? "STOP" : "ANTEPRIMA");
    playButton.setToggleState (proc.isPreviewing(), juce::dontSendNotification);
    statusLabel.setText (proc.getStatus(), juce::dontSendNotification);
}

void VocalisEditor::exportWav()
{
    auto defaultName = proc.getText().substring (0, 24).retainCharacters (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ").trim();
    if (defaultName.isEmpty()) defaultName = "vocal";

    chooser = std::make_unique<juce::FileChooser> ("Esporta il vocal in WAV",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Vocalis " + defaultName + ".wav"), "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension ("wav");
            juce::String err;
            if (proc.exportWav (f, err))
                statusLabel.setText ("Esportato: " + f.getFileName(), juce::dontSendNotification);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Esporta WAV", err);
        });
}

// ============================================================================ Grafica e layout

void VocalisEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    g.setGradientFill (juce::ColourGradient (Colours::accent, 20, 0, Colours::accent2, 200, 0, false));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("VOCALIS", 20, 14, 200, 34, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("text to vocal  v2", 152, 26, 140, 18, juce::Justification::centredLeft);
}

void VocalisEditor::layoutKnobs (juce::Rectangle<int> area, juce::Array<Knob*> ks, int cols, int rowH)
{
    const int colW = area.getWidth() / cols;
    for (int i = 0; i < ks.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (i % cols) * colW, area.getY() + (i / cols) * rowH, colW, rowH).reduced (2);
        ks[i]->label.setBounds (cell.removeFromTop (15));
        ks[i]->slider.setBounds (cell);
    }
}

void VocalisEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    // Barra superiore
    auto header = bounds.removeFromTop (44);
    header.removeFromLeft (300);
    dragHandle.setBounds (header.removeFromRight (110).reduced (0, 6));
    header.removeFromRight (8);
    exportButton.setBounds (header.removeFromRight (120).reduced (0, 6));
    header.removeFromRight (8);
    renderButton.setBounds (header.removeFromRight (96).reduced (0, 6));
    header.removeFromRight (8);
    playButton.setBounds (header.removeFromRight (104).reduced (0, 6));
    header.removeFromRight (12);
    bpmLabel.setBounds (header.removeFromRight (140));
    statusLabel.setBounds (header);
    bounds.removeFromTop (8);

    // Editor delle sillabe in basso
    auto bottom = bounds.removeFromBottom (206);
    dragHint.setBounds (bottom.removeFromBottom (16));
    waveform.setBounds (bottom.withTrimmedBottom (4));
    bounds.removeFromBottom (10);

    const int gap = 12;
    auto col1 = bounds.removeFromLeft (380);
    bounds.removeFromLeft (gap);
    auto col2 = bounds.removeFromLeft (372);
    bounds.removeFromLeft (gap);
    auto col3 = bounds;

    auto comboRow = [] (juce::Rectangle<int> row, juce::Array<Combo*> cs)
    {
        const int cw = (row.getWidth() - 6 * (cs.size() - 1)) / juce::jmax (1, cs.size());
        for (auto* cb : cs)
        {
            auto cell = row.removeFromLeft (cw);
            cb->label.setBounds (cell.removeFromTop (14));
            cb->box.setBounds (cell.withHeight (24));
            row.removeFromLeft (6);
        }
    };

    // ---- Colonna 1: Testo + Voce
    textPanel.setBounds (col1.removeFromTop (196));
    {
        auto c = textPanel.getBounds().reduced (12).withTrimmedTop (22);
        auto langRow = c.removeFromBottom (26);
        languageCombo->label.setBounds (langRow.removeFromLeft (56));
        languageCombo->box.setBounds (langRow.removeFromLeft (150));
        langRow.removeFromLeft (8);
        dictButton.setBounds (langRow.removeFromLeft (110));
        hintLabel.setBounds (langRow.withTrimmedLeft (6));
        c.removeFromBottom (6);
        textEditor.setBounds (c);
    }
    hintLabel.setVisible (false);
    col1.removeFromTop (gap);
    voicePanel.setBounds (col1);
    {
        auto c = voicePanel.getBounds().reduced (12).withTrimmedTop (24);
        auto typeRow = c.removeFromTop (30);
        const int bw = (typeRow.getWidth() - 6) / 2;
        neuralButton.setBounds (typeRow.removeFromLeft (bw));
        typeRow.removeFromLeft (6);
        robotButton.setBounds (typeRow);
        c.removeFromTop (6);
        auto row1 = c.removeFromTop (40);
        comboRow (row1, { voiceACombo });
        comboRow (row1, { robotVarCombo });
        c.removeFromTop (2);
        comboRow (c.removeFromTop (40), { voiceBCombo });
        c.removeFromTop (4);
        layoutKnobs (c, { blendK, speedK, pitchK, intonK, formantK, robotK, robotHzK }, 4, c.getHeight() / 2);
    }

    // ---- Colonna 2: Ritmo + Riproduzione
    playPanel.setBounds (col2.removeFromBottom (122));
    col2.removeFromBottom (gap);
    rhythmPanel.setBounds (col2);
    {
        auto c = rhythmPanel.getBounds().reduced (12).withTrimmedTop (24);
        comboRow (c.removeFromTop (40), { modeCombo, divCombo, bpmSrcCombo, stretchCombo });
        c.removeFromTop (4);
        layoutKnobs (c.removeFromTop (172), { stepsK, swingK, legatoK, pcK, grooveK, accentK, bpmK }, 4, 86);
        c.removeFromTop (6);
        grooveView.setBounds (c);
    }
    {
        auto c = playPanel.getBounds().reduced (12).withTrimmedTop (24);
        comboRow (c.removeFromTop (40), { playModeCombo, loudCombo });
        c.removeFromTop (6);
        auto r2 = c.removeFromTop (26);
        startBarLabel.setBounds (r2.removeFromLeft (112));
        startBarSlider.setBounds (r2.removeFromLeft (120));
        r2.removeFromLeft (16);
        loopToggle->setBounds (r2);
    }

    // ---- Colonna 3: Effetti
    fxPanel.setBounds (col3);
    {
        auto c = fxPanel.getBounds().reduced (12).withTrimmedTop (22);
        const int rows = fxRows.size() + 1;
        const int rowH = juce::jmin (56, c.getHeight() / rows);
        juce::ComboBox* delayDivBox = nullptr;
        for (auto* cb : combos)
            if (cb->label.getText() == "TEMPO") { delayDivBox = &cb->box; cb->label.setVisible (false); }

        for (int i = 0; i < fxRows.size(); ++i)
        {
            auto row = c.removeFromTop (rowH);
            auto* fx = fxRows[i];
            fx->toggle.setBounds (row.removeFromLeft (124).withSizeKeepingCentre (124, 24));
            if (fx->toggle.getButtonText() == "Delay" && delayDivBox != nullptr)
                delayDivBox->setBounds (row.removeFromLeft (72).withSizeKeepingCentre (66, 24));
            const int kw = 70;
            for (auto* k : fx->knobs)
            {
                auto cell = row.removeFromLeft (kw);
                k->label.setBounds (cell.removeFromTop (14));
                k->slider.setBounds (cell);
            }
        }
        auto last = c.removeFromTop (rowH);
        limiterToggle->setBounds (last.removeFromLeft (124).withSizeKeepingCentre (124, 24));
        auto cell = last.removeFromLeft (70);
        outGainK->label.setBounds (cell.removeFromTop (14));
        outGainK->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        outGainK->slider.setBounds (cell);
    }
}

juce::AudioProcessorEditor* VocalisProcessor::createEditor()
{
    return new VocalisEditor (*this);
}
