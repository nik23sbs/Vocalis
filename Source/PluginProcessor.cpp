#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using namespace vocalis;

namespace
{
    juce::StringArray variantNames (VoiceType t)
    {
        juce::StringArray s;
        for (auto& v : ESpeakEngine::variantsFor (t)) s.add (v.displayName);
        return s;
    }

    juce::StringArray languageNames()
    {
        juce::StringArray s;
        for (auto& l : ESpeakEngine::languages()) s.add (l.displayName);
        return s;
    }
}

// ============================================================================ Parametri

juce::AudioProcessorValueTreeState::ParameterLayout VocalisProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    auto hz = AudioParameterFloatAttributes().withLabel ("Hz");
    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    auto choice = [&] (const char* id, const char* name, StringArray items, int def) {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a = {}) {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, a)); };
    auto integer = [&] (const char* id, const char* name, int lo, int hi, int def) {
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto boolean = [&] (const char* id, const char* name, bool def) {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto skewed = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };

    // Voce
    choice (IDs::voiceType, "Tipo voce", { "Uomo", "Donna", "Robot" }, 0);
    choice (IDs::varMale, "Timbro uomo", variantNames (VoiceType::Male), 0);
    choice (IDs::varFemale, "Timbro donna", variantNames (VoiceType::Female), 0);
    choice (IDs::varRobot, "Timbro robot", variantNames (VoiceType::Robot), 0);
    choice (IDs::language, "Lingua", languageNames(), 0);
    integer (IDs::pitch, "Pitch", 0, 99, 50);
    integer (IDs::intonation, "Intonazione", 0, 99, 50);
    integer (IDs::speed, "Velocita'", 80, 400, 160);
    flt (IDs::formant, "Formanti", { 0.7f, 1.4f, 0.01f }, 1.0f);
    flt (IDs::robotize, "Robotizza", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    flt (IDs::robotHz, "Robot Hz", skewed (40.0f, 400.0f, 120.0f), 110.0f, hz);

    // Ritmo
    choice (IDs::rhythmMode, "Modalita' ritmo", { "Libera", "Griglia" }, 1);
    choice (IDs::division, "Risoluzione", RhythmSettings::divisionNames(), 3);
    integer (IDs::stepsPerSyl, "Step per sillaba", 1, 4, 1);
    flt (IDs::swing, "Swing", { 50.0f, 75.0f, 0.1f }, 50.0f, pct);
    flt (IDs::legato, "Legato", { 30.0f, 100.0f, 1.0f }, 90.0f, pct);
    boolean (IDs::stretch, "Adatta allo step", true);
    flt (IDs::grooveAmt, "Quantita' groove", { 0.0f, 100.0f, 1.0f }, 80.0f, pct);
    flt (IDs::accentAmt, "Segui accenti", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    choice (IDs::bpmSource, "Sorgente BPM", { "Progetto", "Manuale" }, 0);
    flt (IDs::bpmManual, "BPM manuale", { 40.0f, 240.0f, 0.01f }, 120.0f);

    // Riproduzione
    choice (IDs::playMode, "Riproduzione", { "Sync DAW", "Trigger MIDI" }, 0);
    integer (IDs::startBar, "Battuta di partenza", 1, 512, 1);
    boolean (IDs::loop, "Loop", true);

    // Effetti
    boolean (IDs::ringOn, "Ring On", false);
    flt (IDs::ringFreq, "Ring Freq", skewed (20.0f, 2000.0f, 200.0f), 220.0f, hz);
    flt (IDs::ringMix, "Ring Mix", { 0.0f, 1.0f }, 0.5f);

    boolean (IDs::distOn, "Dist On", false);
    flt (IDs::distDrive, "Dist Drive", { 0.0f, 40.0f, 0.1f }, 12.0f, db);
    flt (IDs::distMix, "Dist Mix", { 0.0f, 1.0f }, 0.6f);

    boolean (IDs::crushOn, "Crush On", false);
    flt (IDs::crushBits, "Crush Bits", { 2.0f, 16.0f, 0.1f }, 8.0f);
    flt (IDs::crushDown, "Crush Downsample", { 1.0f, 32.0f, 1.0f }, 4.0f);
    flt (IDs::crushMix, "Crush Mix", { 0.0f, 1.0f }, 0.7f);

    boolean (IDs::eqOn, "EQ On", true);
    flt (IDs::eqLowCut, "Low Cut", skewed (20.0f, 1000.0f, 120.0f), 80.0f, hz);
    flt (IDs::eqHighCut, "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 16000.0f, hz);
    flt (IDs::eqPresence, "Presenza", { -12.0f, 12.0f, 0.1f }, 2.0f, db);

    boolean (IDs::compOn, "Comp On", true);
    flt (IDs::compThresh, "Comp Soglia", { -40.0f, 0.0f, 0.1f }, -18.0f, db);
    flt (IDs::compRatio, "Comp Ratio", skewed (1.0f, 20.0f, 4.0f), 3.0f);

    boolean (IDs::chorusOn, "Chorus On", false);
    flt (IDs::chorusRate, "Chorus Rate", skewed (0.05f, 8.0f, 1.0f), 0.8f, hz);
    flt (IDs::chorusDepth, "Chorus Depth", { 0.0f, 1.0f }, 0.3f);
    flt (IDs::chorusMix, "Chorus Mix", { 0.0f, 1.0f }, 0.4f);

    boolean (IDs::delayOn, "Delay On", false);
    choice (IDs::delayDiv, "Delay Tempo", EffectsParams::delayDivisionNames(), 1);
    flt (IDs::delayFb, "Delay Feedback", { 0.0f, 0.95f }, 0.35f);
    flt (IDs::delayMix, "Delay Mix", { 0.0f, 1.0f }, 0.25f);

    boolean (IDs::reverbOn, "Reverb On", false);
    flt (IDs::reverbSize, "Reverb Size", { 0.0f, 1.0f }, 0.5f);
    flt (IDs::reverbDamp, "Reverb Damp", { 0.0f, 1.0f }, 0.5f);
    flt (IDs::reverbMix, "Reverb Mix", { 0.0f, 1.0f }, 0.25f);

    flt (IDs::outGain, "Volume", { -36.0f, 12.0f, 0.1f }, 0.0f, db);

    return { p.begin(), p.end() };
}

// ============================================================================ Costruzione

VocalisProcessor::VocalisProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOCALIS", createLayout())
{
    renderer.onComplete = [this, alive = alive] (RenderedClip::Ptr c)
    {
        if (alive->load())
            setClip (c);
    };

    startTimer (50);
}

VocalisProcessor::~VocalisProcessor()
{
    stopTimer();
    alive->store (false);
}

bool VocalisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void VocalisProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    effects.prepare (sr, block);
}

// ============================================================================ Richieste di render

double VocalisProcessor::getEffectiveBpm() const
{
    if ((int) apvts.getRawParameterValue (IDs::bpmSource)->load() == 1)
        return apvts.getRawParameterValue (IDs::bpmManual)->load();
    return hostBpm.load();
}

RenderRequest VocalisProcessor::buildRequest() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    RenderRequest r;
    r.text = text;
    r.targetSampleRate = sampleRate;

    const auto type = (VoiceType) (int) v (IDs::voiceType);
    const char* varId = type == VoiceType::Female ? IDs::varFemale : type == VoiceType::Robot ? IDs::varRobot : IDs::varMale;
    const auto& variants = ESpeakEngine::variantsFor (type);
    r.voice.variant = variants[(size_t) juce::jlimit (0, (int) variants.size() - 1, (int) v (varId))].id;
    const auto& langs = ESpeakEngine::languages();
    r.voice.language = langs[(size_t) juce::jlimit (0, (int) langs.size() - 1, (int) v (IDs::language))].id;
    r.voice.pitch = (int) v (IDs::pitch);
    r.voice.range = (int) v (IDs::intonation);
    r.voice.rate = (int) v (IDs::speed);
    r.voice.formant = v (IDs::formant);
    r.voice.robotize = v (IDs::robotize) / 100.0f;
    r.voice.robotHz = v (IDs::robotHz);

    r.rhythm.mode = (int) v (IDs::rhythmMode) == 0 ? RhythmSettings::Mode::Free : RhythmSettings::Mode::Grid;
    r.rhythm.bpm = std::round (getEffectiveBpm() * 100.0) / 100.0;
    r.rhythm.beatsPerBar = hostBeatsPerBar.load();
    r.rhythm.stepBeats = RhythmSettings::divisionToBeats ((int) v (IDs::division));
    r.rhythm.stepsPerSyllable = (int) v (IDs::stepsPerSyl);
    r.rhythm.swing = v (IDs::swing) / 100.0;
    r.rhythm.legato = v (IDs::legato) / 100.0;
    r.rhythm.stretchToFit = v (IDs::stretch) > 0.5f;
    r.rhythm.grooveAmount = v (IDs::grooveAmt) / 100.0;
    r.rhythm.accentAmount = v (IDs::accentAmt) / 100.0;
    r.groove = groove;
    return r;
}

juce::String VocalisProcessor::requestSignature() const
{
    const char* ids[] = { IDs::voiceType, IDs::varMale, IDs::varFemale, IDs::varRobot, IDs::language, IDs::pitch,
                          IDs::intonation, IDs::speed, IDs::formant, IDs::robotize, IDs::robotHz, IDs::rhythmMode,
                          IDs::division, IDs::stepsPerSyl, IDs::swing, IDs::legato, IDs::stretch, IDs::grooveAmt,
                          IDs::accentAmt };
    juce::String s = text + "|" + juce::String (sampleRate) + "|" + juce::String (getEffectiveBpm(), 2)
                   + "|" + juce::String (hostBeatsPerBar.load()) + "|g" + juce::String (grooveVersion);
    for (auto* id : ids)
        s << "|" << juce::String (apvts.getRawParameterValue (id)->load(), 3);
    return s;
}

void VocalisProcessor::timerCallback()
{
    const auto sig = requestSignature();
    const auto now = juce::Time::getMillisecondCounter();

    if (sig != lastSignature)
    {
        if (sig != pendingSignature)
        {
            pendingSignature = sig;
            pendingSince = now;
            status = "In attesa...";
            sendChangeMessage();
        }
        else if (now - pendingSince > 250)
        {
            renderer.request (buildRequest());
            lastSignature = sig;
            pendingSignature.clear();
            status = "Generazione...";
            sendChangeMessage();
        }
    }

    // libera le clip vecchie (l'audio thread lavora sempre sulla clip corrente)
    if (graveyard.size() > 3)
        graveyard.removeRange (0, graveyard.size() - 3);
}

void VocalisProcessor::forceRender()
{
    lastSignature.clear();
    pendingSignature.clear();
}

void VocalisProcessor::renderBlocking()
{
    setClip (renderer.renderNow (buildRequest()));
    lastSignature = requestSignature();
    pendingSignature.clear();
}

void VocalisProcessor::setText (const juce::String& t)
{
    text = t;
}

void VocalisProcessor::setClip (RenderedClip::Ptr c)
{
    if (c == nullptr)
        return;

    if (c->error.isNotEmpty())
    {
        status = "Errore: " + c->error;
    }
    else
    {
        {
            const juce::SpinLock::ScopedLockType l (clipLock);
            if (clip != nullptr) graveyard.add (clip);
            clip = c;
        }
        status = juce::String (c->markers.size()) + " sillabe  -  " + juce::String (c->lengthBeats / 4.0, 1)
               + " battute  -  " + juce::String (c->bpm, 1) + " BPM";
    }
    sendChangeMessage();
}

RenderedClip::Ptr VocalisProcessor::getClip() const
{
    const juce::SpinLock::ScopedLockType l (clipLock);
    return clip;
}

bool VocalisProcessor::loadGrooveFile (const juce::File& f, juce::String& error)
{
    auto g = GrooveExtractor::extractFromFile (f, getEffectiveBpm(), error);
    if (g.isEmpty())
        return false;
    groove = std::move (g);
    ++grooveVersion;
    sendChangeMessage();
    return true;
}

void VocalisProcessor::clearGroove()
{
    groove = {};
    ++grooveVersion;
    sendChangeMessage();
}

void VocalisProcessor::startPreview() { previewRequest = true; }
void VocalisProcessor::stopPreview()  { stopRequest = true; }

// ============================================================================ Audio

EffectsParams VocalisProcessor::readEffects() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    EffectsParams p;
    p.ringOn = v (IDs::ringOn) > 0.5f;     p.ringFreq = v (IDs::ringFreq);    p.ringMix = v (IDs::ringMix);
    p.distOn = v (IDs::distOn) > 0.5f;     p.distDrive = v (IDs::distDrive);  p.distMix = v (IDs::distMix);
    p.crushOn = v (IDs::crushOn) > 0.5f;   p.crushBits = v (IDs::crushBits);  p.crushDown = v (IDs::crushDown); p.crushMix = v (IDs::crushMix);
    p.eqOn = v (IDs::eqOn) > 0.5f;         p.eqLowCut = v (IDs::eqLowCut);    p.eqHighCut = v (IDs::eqHighCut); p.eqPresence = v (IDs::eqPresence);
    p.compOn = v (IDs::compOn) > 0.5f;     p.compThresh = v (IDs::compThresh); p.compRatio = v (IDs::compRatio);
    p.chorusOn = v (IDs::chorusOn) > 0.5f; p.chorusRate = v (IDs::chorusRate); p.chorusDepth = v (IDs::chorusDepth); p.chorusMix = v (IDs::chorusMix);
    p.delayOn = v (IDs::delayOn) > 0.5f;   p.delayDiv = (int) v (IDs::delayDiv); p.delayFeedback = v (IDs::delayFb); p.delayMix = v (IDs::delayMix);
    p.reverbOn = v (IDs::reverbOn) > 0.5f; p.reverbSize = v (IDs::reverbSize); p.reverbDamp = v (IDs::reverbDamp); p.reverbMix = v (IDs::reverbMix);
    p.outGainDb = v (IDs::outGain);
    return p;
}

void VocalisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // --- Trasporto della DAW
    bool hostPlaying = false;
    double ppq = 0.0, bpm = hostBpm.load();
    int beatsPerBar = hostBeatsPerBar.load();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { bpm = *b; hostBpm = *b; }
            if (auto ts = pos->getTimeSignature())
            {
                beatsPerBar = juce::jmax (1, (int) std::round (ts->numerator * 4.0 / juce::jmax (1, ts->denominator)));
                hostBeatsPerBar = beatsPerBar;
            }
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    }

    // --- Clip corrente (senza bloccare l'audio thread)
    RenderedClip::Ptr c;
    {
        const juce::SpinLock::ScopedTryLockType l (clipLock);
        if (l.isLocked()) c = clip;
    }

    if (stopRequest.exchange (false)) voicePos = -1;
    if (previewRequest.exchange (false)) { voicePos = 0; voiceGain = 1.0f; }

    auto* out = buffer.getWritePointer (0);
    const bool syncMode = (int) apvts.getRawParameterValue (IDs::playMode)->load() == 0;
    double shownPos = -1.0;

    if (c != nullptr && c->audio.getNumSamples() > 0)
    {
        const float* src = c->audio.getReadPointer (0);
        const int len = c->audio.getNumSamples();

        if (syncMode && hostPlaying)
        {
            voicePos = -1;
            const double startPpq = (apvts.getRawParameterValue (IDs::startBar)->load() - 1.0) * beatsPerBar;
            const bool looping = apvts.getRawParameterValue (IDs::loop)->load() > 0.5f;
            const double beatsPerSample = bpm / 60.0 / sampleRate;
            const double clipSamplesPerBeat = 60.0 / c->bpm * c->sampleRate;

            for (int i = 0; i < n; ++i)
            {
                double rel = ppq + i * beatsPerSample - startPpq;
                if (rel < 0.0) continue;
                if (looping && c->lengthBeats > 0.0) rel = std::fmod (rel, c->lengthBeats);
                else if (rel >= c->lengthBeats) continue;

                const int idx = (int) std::floor (rel * clipSamplesPerBeat + 0.5);
                if (idx >= 0 && idx < len)
                {
                    out[i] = src[idx];
                    shownPos = idx;
                }
            }
        }
        else
        {
            // Trigger MIDI (qualsiasi nota fa ripartire la clip) e anteprima
            auto it = midi.begin();
            int pos = voicePos.load();
            for (int i = 0; i < n; ++i)
            {
                while (it != midi.end() && (*it).samplePosition <= i)
                {
                    const auto m = (*it).getMessage();
                    if (! syncMode && m.isNoteOn()) { pos = 0; voiceGain = m.getFloatVelocity(); }
                    ++it;
                }
                if (pos >= 0)
                {
                    if (pos < len) { out[i] = src[pos] * voiceGain; shownPos = pos; ++pos; }
                    else pos = -1;
                }
            }
            voicePos = pos;
        }
    }
    displayPos = shownPos;

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    effects.process (buffer, readEffects(), bpm);
}

// ============================================================================ Export

bool VocalisProcessor::exportWav (const juce::File& file, juce::String& error)
{
    auto c = getClip();
    if (c == nullptr || c->audio.getNumSamples() == 0)
    {
        error = "Nessun vocal da esportare: scrivi un testo e attendi la generazione.";
        return false;
    }

    const auto fx = readEffects();
    const double bpm = c->bpm;
    const int len = c->audio.getNumSamples() + (int) (EffectsChain::tailSeconds (fx, bpm) * c->sampleRate);

    juce::AudioBuffer<float> st (2, len);
    st.clear();
    for (int ch = 0; ch < 2; ++ch)
        st.copyFrom (ch, 0, c->audio, 0, 0, c->audio.getNumSamples());

    EffectsChain chain;
    chain.prepare (c->sampleRate, 1024);
    chain.process (st, fx, bpm);

    // taglia il silenzio finale oltre la lunghezza della clip
    int end = len;
    while (end > c->audio.getNumSamples() && st.getMagnitude (0, end - 1, 1) < 1.0e-5f && st.getMagnitude (1, end - 1, 1) < 1.0e-5f)
        --end;

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
    {
        error = "Impossibile scrivere " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), c->sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        error = "Errore nella creazione del WAV";
        return false;
    }
    stream.release();
    writer->writeFromAudioSampleBuffer (st, 0, end);
    return true;
}

juce::File VocalisProcessor::createTempExport (juce::String& error)
{
    auto name = text.substring (0, 24).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ").trim().replaceCharacter (' ', '_');
    if (name.isEmpty()) name = "vocal";
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Vocalis");
    dir.createDirectory();
    auto f = dir.getChildFile ("Vocalis_" + name + "_" + juce::String (juce::Time::getCurrentTime().toMilliseconds() % 100000) + ".wav");
    if (! exportWav (f, error))
        return {};
    return f;
}

// ============================================================================ Stato

void VocalisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("text", text, nullptr);
    state.removeChild (state.getChildWithName ("GROOVE"), nullptr);
    if (! groove.isEmpty())
        state.appendChild (groove.toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalisProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType()))
            return;

        text = state.getProperty ("text", text).toString();
        groove = GrooveTemplate::fromValueTree (state.getChildWithName ("GROOVE"));
        ++grooveVersion;
        state.removeChild (state.getChildWithName ("GROOVE"), nullptr);
        apvts.replaceState (state);
        sendChangeMessage();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalisProcessor();
}
