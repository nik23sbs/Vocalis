#include "PluginEditor.h"

using namespace vocalis;

VocalisEditor::VocalisEditor (VocalisProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      modeSwitch (*p.apvts.getParameter (IDs::mode), { "Parlato", "Rap", "Canto" }),
      dragHandle (p), orb (p.apvts),
      engineSwitch (*p.apvts.getParameter (IDs::engine), { "Neurale", "Robot" }),
      robotTimbre (p.apvts, IDs::robotVariant, "Timbro robot"),
      language (p.apvts, IDs::language, {}),
      timeline (p), inspector (p)
{
    setLookAndFeel (&lnf);

    // ---- barra superiore
    modeSwitch.onChange = [this] (int) { inspector.refresh(); timeline.repaint(); };
    addAndMakeVisible (modeSwitch);
    playButton.setTooltip ("Anteprima (in Sync DAW il vocal suona con il trasporto)");
    playButton.onClick = [this] { proc.isPreviewing() ? proc.stopPreview() : proc.startPreview(); };
    addAndMakeVisible (playButton);
    for (auto* l : { &positionLabel, &tempoLabel, &statusLabel })
    {
        l->setFont (juce::FontOptions (13.0f));
        l->setColour (juce::Label::textColourId, Colours::textDim);
        addAndMakeVisible (l);
    }
    positionLabel.setColour (juce::Label::textColourId, Colours::text);
    positionLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (12.0f));
    exportButton.setTooltip ("Salva il vocal con gli effetti in un file WAV 24 bit");
    exportButton.onClick = [this] { exportWav(); };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (dragHandle);

    // ---- colonna voci
    const char* ids[] = { IDs::voiceA, IDs::voiceB, IDs::voiceC, IDs::voiceD };
    const juce::Colour c1[] = { Colours::pink, Colours::accent2, Colours::warn, juce::Colour (0xff34d399) };
    const juce::Colour c2[] = { Colours::accent, juce::Colour (0xff6366f1), juce::Colour (0xfff97316), juce::Colour (0xff14b8a6) };
    for (int i = 0; i < 4; ++i)
        addAndMakeVisible (cards.add (new VoiceCard (p.apvts, ids[i], juce::String::charToString ((juce::juce_wchar) ('A' + i)), c1[i], c2[i])));
    orb.setTooltip ("Trascina il punto per miscelare le quattro voci agli angoli");
    addAndMakeVisible (orb);
    mixInfo.setFont (juce::FontOptions (12.0f));
    mixInfo.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (mixInfo);
    variant.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 26);
    variant.setTooltip ("Variante: un'altra interpretazione della stessa frase (stessi tempi, prosodia diversa)");
    variantAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, IDs::variant, variant);
    addAndMakeVisible (variant);
    newVariant.setTooltip ("Prova un'interpretazione nuova a caso");
    newVariant.onClick = [this] {
        if (auto* v = proc.apvts.getParameter (IDs::variant))
            v->setValueNotifyingHost (v->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt (25))); };
    addAndMakeVisible (newVariant);
    engineSwitch.onChange = [this] (int e) { robotTimbre.setVisible (e == 1); for (auto* c : cards) c->setEnabled (e == 0); orb.setEnabled (e == 0); resized(); repaint(); };
    addAndMakeVisible (engineSwitch);
    addChildComponent (robotTimbre);
    engineSwitch.onChange ((int) p.apvts.getRawParameterValue (IDs::engine)->load());

    struct K { const char* id; const char* name; const char* suffix; int dec; };
    for (auto k : { K { IDs::speed, "Velocita'", "x", 2 }, K { IDs::pitch, "Pitch", " st", 1 }, K { IDs::intonation, "Intonaz.", "%", 0 },
                    K { IDs::formant, "Formanti", " st", 1 }, K { IDs::robotize, "Robot", "%", 0 }, K { IDs::robotHz, "Robot Hz", " Hz", 0 } })
        addAndMakeVisible (timbre.add (new LabelledKnob (p.apvts, k.id, k.name, Colours::accent, k.suffix, k.dec)));

    // ---- centro
    textEditor.setMultiLine (true, true);
    textEditor.setReturnKeyStartsNewLine (true);
    textEditor.setFont (juce::FontOptions (15.0f));
    textEditor.setText (proc.getText(), false);
    textEditor.setTextToShowWhenEmpty ("Scrivi qui il testo del vocal: virgole e punti diventano pause a tempo.", Colours::textDim);
    textEditor.setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    textEditor.setColour (juce::TextEditor::outlineColourId, Colours::edge2);
    textEditor.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    textEditor.setIndents (12, 8);
    textEditor.onTextChange = [this] { proc.setText (textEditor.getText()); };
    addAndMakeVisible (textEditor);
    addAndMakeVisible (language);
    dictButton.setTooltip ("Pronuncia personalizzata: \"parola = come si legge\" oppure \"parola = /ipa/\"");
    dictButton.setClickingTogglesState (false);
    dictButton.onClick = [this] { showDictionary(); };
    addAndMakeVisible (dictButton);

    timeline.onSelect = [this] (int s) { inspector.setSelected (s); };
    addAndMakeVisible (timeline);

    struct M { const char* id; const char* name; juce::Colour c; };
    for (auto m : { M { IDs::mExpr, "Espressivita'", Colours::accent }, M { IDs::mFlow, "Swing / Flow", Colours::accent },
                    M { IDs::mWarmth, "Calore", Colours::accent2 }, M { IDs::mBreath, "Respiro", Colours::accent2 },
                    M { IDs::mChoir, "Doppio / Coro", Colours::pink }, M { IDs::mSpace, "Spazio", Colours::pink } })
        addAndMakeVisible (macros.add (new LabelledKnob (p.apvts, m.id, m.name, m.c, "%", 0, true)));

    // ---- destra
    addAndMakeVisible (inspector);

    proc.addChangeListener (this);
    timeline.setClip (proc.getClip());
    updateVoiceInfo();
    startTimerHz (15);
    setSize (1280, 800);
}

VocalisEditor::~VocalisEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void VocalisEditor::updateVoiceInfo()
{
    const float x = proc.apvts.getRawParameterValue (IDs::orbX)->load() / 100.0f;
    const float y = proc.apvts.getRawParameterValue (IDs::orbY)->load() / 100.0f;
    float w[4] = { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };
    juce::StringArray names;
    float tot = 0.0f;
    for (int i = 0; i < 4; ++i) { names.add (cards[i]->voiceName()); if (names[i].isEmpty()) w[i] = 0.0f; tot += w[i]; }
    if (tot <= 1.0e-4f) { w[0] = 1.0f; tot = 1.0f; }
    juce::StringArray parts;
    for (int i = 0; i < 4; ++i)
    {
        cards[i]->setWeight (w[i] / tot);
        if (w[i] / tot > 0.01f && names[i].isNotEmpty()) parts.add (juce::String (juce::roundToInt (100.0f * w[i] / tot)) + "% " + names[i]);
    }
    orb.setCornerNames (names);
    mixInfo.setText (parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")), juce::dontSendNotification);
}

void VocalisEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    timeline.setClip (proc.getClip());
    inspector.refresh();
    if (! textEditor.hasKeyboardFocus (true) && textEditor.getText() != proc.getText())
        textEditor.setText (proc.getText(), false);
}

void VocalisEditor::timerCallback()
{
    updateVoiceInfo();

    const bool previewing = proc.isPreviewing();
    playButton.setToggleState (previewing, juce::dontSendNotification);

    auto clip = proc.getClip();
    const double pos = proc.getPlayPosition();
    if (clip != nullptr && pos >= 0)
    {
        const double beats = pos / (60.0 / clip->bpm * clip->sampleRate) - clip->prerollBeats;
        const int startBar = (int) proc.apvts.getRawParameterValue (IDs::startBar)->load();
        if (beats < 0) positionLabel.setText ("anticipo", juce::dontSendNotification);
        else positionLabel.setText (juce::String (startBar + (int) std::floor (beats / 4.0)) + "." + juce::String ((int) std::floor (beats) % 4 + 1)
                                    + "." + juce::String ((int) std::floor ((beats - std::floor (beats)) * 4.0) + 1), juce::dontSendNotification);
    }
    else positionLabel.setText ("-.-.-", juce::dontSendNotification);

    const bool manual = (int) proc.apvts.getRawParameterValue (IDs::bpmSource)->load() == 1;
    const bool sync = (int) proc.apvts.getRawParameterValue (IDs::playMode)->load() == 0;
    tempoLabel.setText (juce::String (proc.getEffectiveBpm(), 1).replaceCharacter ('.', ',') + " BPM" + juce::String::fromUTF8 (" \xc2\xb7 ")
                        + (manual ? "manuale" : "progetto") + juce::String::fromUTF8 (" \xc2\xb7 ") + (sync ? "sync DAW" : "trigger MIDI"),
                        juce::dontSendNotification);

    auto st = proc.getStatus();
    const bool err = st.startsWith ("Errore") || st.contains ("non disponibile");
    statusLabel.setColour (juce::Label::textColourId, err ? Colours::warn : proc.isRendering() ? Colours::textDim : Colours::ok);
    statusLabel.setText (juce::String::fromUTF8 ("\xe2\x97\x8f ") + st, juce::dontSendNotification);
    dictButton.setToggleState (proc.getDictionary().trim().isNotEmpty(), juce::dontSendNotification);
}

void VocalisEditor::showDictionary()
{
    struct DictPanel : public juce::Component
    {
        DictPanel (VocalisProcessor& p) : proc (p)
        {
            title.setText ("Dizionario di pronuncia", juce::dontSendNotification);
            title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            help.setText (juce::String::fromUTF8 ("Una riga per parola. Esempi:\n  plugin = plaghin\n  okay = /\xcb\x8c" "o\xca\x8a" "k\xcb\x88" "e\xc9\xaa/   (IPA tra barre)"),
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
            {
                auto c = proc.getClip();
                juce::String note = c != nullptr && c->prerollBeats > 0
                    ? " (parte " + juce::String ((int) c->prerollBeats) + " quarto prima della battuta: l'attacco delle consonanti)" : juce::String();
                statusLabel.setText ("Esportato: " + f.getFileName() + note, juce::dontSendNotification);
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Esporta WAV", err);
        });
}

void VocalisEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // barra superiore
    g.setColour (Colours::header);
    g.fillRect (0, 0, getWidth(), headerH);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (headerH - 1, 0, (float) getWidth());
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VOCALIS", 20, 0, 120, headerH, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("3", 128, 6, 20, headerH, juce::Justification::centredLeft);

    // colonna voci
    g.setColour (Colours::side);
    g.fillRect (0, headerH, leftW, getHeight() - headerH);
    g.setColour (Colours::panelEdge);
    g.drawVerticalLine (leftW - 1, (float) headerH, (float) getHeight());

    auto section = [&] (juce::Component& below, const juce::String& t, int x, int w) { drawSectionTitle (g, { x, below.getY() - 20, w, 16 }, t); };
    section (*cards[0], "Voci", 16, leftW - 32);
    section (orb, "Miscela A B C D", 16, leftW - 32);
    section (variant, "Variante", 16, leftW - 32);
    section (engineSwitch, "Motore", 16, leftW - 32);
    section (*timbre[0], "Timbro", 16, leftW - 32);
    section (textEditor, "Testo", leftW + 16, 200);
}

void VocalisEditor::resized()
{
    // ---- barra superiore
    auto top = getLocalBounds().removeFromTop (headerH).reduced (16, 12);
    top.removeFromLeft (140);
    modeSwitch.setBounds (top.removeFromLeft (270));
    top.removeFromLeft (16);
    playButton.setBounds (top.removeFromLeft (36));
    top.removeFromLeft (8);
    positionLabel.setBounds (top.removeFromLeft (70));
    tempoLabel.setBounds (top.removeFromLeft (230));
    dragHandle.setBounds (top.removeFromRight (160));
    top.removeFromRight (10);
    exportButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (10);
    statusLabel.setBounds (top);

    // ---- colonna voci
    auto left = juce::Rectangle<int> (0, headerH, leftW, getHeight() - headerH).reduced (16, 0);
    left.removeFromTop (36);
    for (auto* c : cards) { c->setBounds (left.removeFromTop (48)); left.removeFromTop (6); }
    left.removeFromTop (24);
    orb.setBounds (left.removeFromTop (136));
    left.removeFromTop (4);
    mixInfo.setBounds (left.removeFromTop (18).withTrimmedLeft (-4));
    left.removeFromTop (26);
    auto vr = left.removeFromTop (30);
    newVariant.setBounds (vr.removeFromRight (70));
    variant.setBounds (vr.withTrimmedRight (8));
    left.removeFromTop (26);
    engineSwitch.setBounds (left.removeFromTop (34));
    if (robotTimbre.isVisible()) { left.removeFromTop (6); robotTimbre.setBounds (left.removeFromTop (40)); }
    left.removeFromTop (30);
    const int kw = left.getWidth() / 3, kh = juce::jmin (62, (left.getHeight() - 8) / 2);
    for (int i = 0; i < timbre.size(); ++i)
        timbre[i]->setBounds (left.getX() + (i % 3) * kw, left.getY() + (i / 3) * kh, kw, kh);

    // ---- destra
    inspector.setBounds (getWidth() - rightW, headerH, rightW, getHeight() - headerH);

    // ---- centro
    auto mid = juce::Rectangle<int> (leftW, headerH, getWidth() - leftW - rightW, getHeight() - headerH).reduced (16, 16);
    auto head = mid.removeFromTop (24);
    dictButton.setBounds (head.removeFromRight (100).withSizeKeepingCentre (100, 26));
    head.removeFromRight (8);
    language.setBounds (head.removeFromRight (150).withSizeKeepingCentre (150, 26));
    language.box.setTooltip ("Lingua del testo");
    mid.removeFromTop (4);
    textEditor.setBounds (mid.removeFromTop (56));
    mid.removeFromTop (12);
    auto mac = mid.removeFromBottom (104);
    const int mw = (mac.getWidth() - 5 * 10) / 6;
    for (auto* m : macros) { m->setBounds (mac.removeFromLeft (mw)); mac.removeFromLeft (10); }
    mid.removeFromBottom (12);
    timeline.setBounds (mid);
}

juce::AudioProcessorEditor* VocalisProcessor::createEditor()
{
    return new VocalisEditor (*this);
}
