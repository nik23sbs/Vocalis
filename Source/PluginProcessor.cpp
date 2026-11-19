#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <map>

using namespace vocalis;

namespace
{
    juce::StringArray languageNames()
    {
        juce::StringArray s;
        for (auto& l : ESpeakEngine::languages()) s.add (l.displayName);
        return s;
    }

    juce::StringArray neuralVoiceNames()
    {
        juce::StringArray s;
        for (auto& v : KokoroVoices::catalogue()) s.add (v.displayName);
        return s;
    }

    juce::StringArray robotVariantNames()
    {
        juce::StringArray s;
        for (auto& v : ESpeakEngine::variantsFor (VoiceType::Robot)) s.add (v.displayName);
        for (auto& v : ESpeakEngine::variantsFor (VoiceType::Male)) s.add (juce::String ("Formant: ") + v.displayName);
        for (auto& v : ESpeakEngine::variantsFor (VoiceType::Female)) s.add (juce::String ("Formant: ") + v.displayName);
        return s;
    }

    juce::String robotVariantId (int index)
    {
        std::vector<const char*> ids;
        for (auto t : { VoiceType::Robot, VoiceType::Male, VoiceType::Female })
            for (auto& v : ESpeakEngine::variantsFor (t)) ids.push_back (v.id);
        return ids[(size_t) juce::jlimit (0, (int) ids.size() - 1, index)];
    }

    const double loudnessValues[] = { 0.0, -20.0, -18.0, -16.0, -14.0, -12.0 };
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
    choice (IDs::engine, "Motore", { "Neurale", "Robot / Formant" }, 0);
    choice (IDs::mode, "Modalita'", { "Parlato", "Rap", "Canto" }, 1);
    choice (IDs::voiceA, "Voce A", neuralVoiceNames(), 0);
    { auto names = neuralVoiceNames(); names.insert (0, "Nessuna");
      choice (IDs::voiceB, "Voce B", names, 2);     // Nicola
      choice (IDs::voiceC, "Voce C", names, 0);
      choice (IDs::voiceD, "Voce D", names, 0); }
    flt (IDs::orbX, "Sfera X", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    flt (IDs::orbY, "Sfera Y", { 0.0f, 100.0f, 0.1f }, 0.0f, pct);
    integer (IDs::variant, "Variante", 0, 24, 0);
    choice (IDs::robotVariant, "Timbro robot", robotVariantNames(), 0);
    choice (IDs::language, "Lingua", languageNames(), 0);
    flt (IDs::speed, "Velocita'", { 0.5f, 2.0f, 0.01f }, 1.0f);
    flt (IDs::pitch, "Pitch", { -12.0f, 12.0f, 0.01f }, 0.0f, AudioParameterFloatAttributes().withLabel ("st"));
    flt (IDs::intonation, "Intonazione", { 0.0f, 200.0f, 1.0f }, 100.0f, pct);
    flt (IDs::formant, "Formanti", { -6.0f, 6.0f, 0.01f }, 0.0f, AudioParameterFloatAttributes().withLabel ("st"));
    flt (IDs::robotize, "Robotizza", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    flt (IDs::robotHz, "Robot Hz", skewed (40.0f, 400.0f, 120.0f), 110.0f, hz);

    // Ritmo
    choice (IDs::division, "Risoluzione", RhythmSettings::divisionNames(), 3);
    integer (IDs::stepsPerSyl, "Step per sillaba", 1, 4, 1);
    flt (IDs::swing, "Swing", { 50.0f, 75.0f, 0.1f }, 50.0f, pct);
    flt (IDs::legato, "Legato", { 30.0f, 100.0f, 1.0f }, 90.0f, pct);
    choice (IDs::stretch, "Stretch", { "Off", "Solo vocali", "Sillaba intera" }, 1);
    flt (IDs::pCenter, "Attacco vocale", { 0.0f, 100.0f, 1.0f }, 100.0f, pct);
    flt (IDs::grooveAmt, "Quantita' groove", { 0.0f, 100.0f, 1.0f }, 80.0f, pct);
    flt (IDs::accentAmt, "Segui accenti", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    choice (IDs::bpmSource, "Sorgente BPM", { "Progetto", "Manuale" }, 0);
    flt (IDs::bpmManual, "BPM manuale", { 40.0f, 240.0f, 0.01f }, 120.0f);

    // Canto e armonie
    flt (IDs::vibrato, "Vibrato", { 0.0f, 100.0f, 1.0f }, 25.0f, AudioParameterFloatAttributes().withLabel ("cent"));
    flt (IDs::glide, "Glide", { 10.0f, 300.0f, 1.0f }, 60.0f, AudioParameterFloatAttributes().withLabel ("ms"));
    choice (IDs::key, "Tonalita'", { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }, 0);
    choice (IDs::scale, "Scala", { "Maggiore", "Minore" }, 0);
    choice (IDs::harm1, "Armonia 1", harmonyNames(), 0);
    choice (IDs::harm2, "Armonia 2", harmonyNames(), 0);
    flt (IDs::harmLevel, "Livello armonie", { 0.0f, 100.0f, 1.0f }, 55.0f, pct);
    flt (IDs::harmWidth, "Apertura armonie", { 0.0f, 100.0f, 1.0f }, 80.0f, pct);
    boolean (IDs::doubleOn, "Doppio", false);
    flt (IDs::doubleLevel, "Livello doppio", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);

    // Macro
    flt (IDs::mExpr, "Espressivita'", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    flt (IDs::mFlow, "Flow", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    flt (IDs::mWarmth, "Calore", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    flt (IDs::mBreath, "Respiro", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);
    flt (IDs::mChoir, "Coro", { 0.0f, 100.0f, 1.0f }, 50.0f, pct);
    flt (IDs::mSpace, "Spazio", { 0.0f, 100.0f, 1.0f }, 0.0f, pct);

    // Riproduzione
    choice (IDs::playMode, "Riproduzione", { "Sync DAW", "Trigger MIDI" }, 0);
    integer (IDs::startBar, "Battuta di partenza", 1, 512, 1);
    boolean (IDs::loop, "Loop", true);
    choice (IDs::loudness, "Loudness", { "Off (picco)", "-20 LUFS", "-18 LUFS", "-16 LUFS", "-14 LUFS", "-12 LUFS" }, 3);

    // Effetti
    boolean (IDs::deessOn, "De-esser On", true);
    flt (IDs::deessAmount, "De-esser", { 0.0f, 1.0f }, 0.4f);
    boolean (IDs::doublerOn, "Doubler On", false);
    flt (IDs::doublerMix, "Doubler Mix", { 0.0f, 1.0f }, 0.35f);
    flt (IDs::doublerWidth, "Doubler Width", { 0.0f, 1.0f }, 0.8f);
    boolean (IDs::limiterOn, "Limiter On", true);

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

    const auto& cat = KokoroVoices::catalogue();
    r.voice.engine = (int) v (IDs::engine) == 0 ? EngineType::Neural : EngineType::Formant;
    // sfera voce: miscela bilineare dei quattro angoli (A in alto a sinistra, B a destra, C in basso, D in basso a destra)
    r.voice.mix.id[0] = cat[(size_t) juce::jlimit (0, (int) cat.size() - 1, (int) v (IDs::voiceA))].id;
    const char* others[] = { IDs::voiceB, IDs::voiceC, IDs::voiceD };
    for (int i = 0; i < 3; ++i)
    {
        const int idx = (int) v (others[i]);
        r.voice.mix.id[i + 1] = idx > 0 ? juce::String (cat[(size_t) juce::jlimit (0, (int) cat.size() - 1, idx - 1)].id) : juce::String();
    }
    const float x = v (IDs::orbX) / 100.0f, y = v (IDs::orbY) / 100.0f;
    r.voice.mix.w[0] = (1 - x) * (1 - y); r.voice.mix.w[1] = x * (1 - y);
    r.voice.mix.w[2] = (1 - x) * y;       r.voice.mix.w[3] = x * y;
    bool anyWeight = false;
    for (int i = 0; i < 4; ++i) anyWeight |= r.voice.mix.id[i].isNotEmpty() && r.voice.mix.w[i] > 1.0e-4f;
    if (! anyWeight) { r.voice.mix.w[0] = 1.0f; for (int i = 1; i < 4; ++i) r.voice.mix.w[i] = 0.0f; }
    r.voice.variant = (int) v (IDs::variant);
    r.voice.espeakVariant = robotVariantId ((int) v (IDs::robotVariant));
    const auto& langs = ESpeakEngine::languages();
    r.voice.language = langs[(size_t) juce::jlimit (0, (int) langs.size() - 1, (int) v (IDs::language))].id;
    r.voice.speed = v (IDs::speed);
    r.voice.espeakRate = (int) std::round (160.0f * v (IDs::speed));
    r.voice.pitchSemis = v (IDs::pitch);
    const float expr = v (IDs::mExpr) / 100.0f;                       // 0.5 = neutro
    r.voice.intonation = v (IDs::intonation) / 100.0f * (0.4f + 1.2f * expr);
    r.voice.formantSemis = v (IDs::formant);
    r.voice.robotize = v (IDs::robotize) / 100.0f;
    r.voice.robotHz = v (IDs::robotHz);
    r.voice.dictionary = dictionary;
    r.edits = edits;
    r.loudnessLufs = loudnessValues[juce::jlimit (0, 5, (int) v (IDs::loudness))];

    r.mode = (VocalMode) juce::jlimit (0, 2, (int) v (IDs::mode));
    r.rhythm.mode = r.mode == VocalMode::Parlato ? RhythmSettings::Mode::Free : RhythmSettings::Mode::Grid;
    r.notes = melody;
    r.vibratoCents = juce::jmax (0.0f, v (IDs::vibrato) * (0.5f + expr));
    r.glideMs = v (IDs::glide);
    r.expressivity = 0.6f * expr;
    r.breath = v (IDs::mBreath) / 100.0f;
    const float choir = v (IDs::mChoir) / 50.0f;                      // 1 = neutro
    r.harmony.key = (int) v (IDs::key);
    r.harmony.minor = (int) v (IDs::scale) == 1;
    r.harmony.degrees1 = harmonyDegrees ((int) v (IDs::harm1));
    r.harmony.degrees2 = harmonyDegrees ((int) v (IDs::harm2));
    r.harmony.level = v (IDs::harmLevel) / 100.0f * choir;
    r.harmony.width = v (IDs::harmWidth) / 100.0f;
    r.harmony.doubleOn = v (IDs::doubleOn) > 0.5f;
    r.harmony.doubleLevel = v (IDs::doubleLevel) / 100.0f * choir;
    r.rhythm.bpm = std::round (getEffectiveBpm() * 100.0) / 100.0;
    r.rhythm.beatsPerBar = hostBeatsPerBar.load();
    r.rhythm.stepBeats = RhythmSettings::divisionToBeats ((int) v (IDs::division));
    r.rhythm.stepsPerSyllable = (int) v (IDs::stepsPerSyl);
    r.rhythm.swing = juce::jmin (0.75, v (IDs::swing) / 100.0 + 0.15 * v (IDs::mFlow) / 100.0);
    r.rhythm.legato = v (IDs::legato) / 100.0;
    r.rhythm.stretch = (RhythmSettings::Stretch) juce::jlimit (0, 2, (int) v (IDs::stretch));
    r.rhythm.pCenter = v (IDs::pCenter) / 100.0;
    r.rhythm.grooveAmount = juce::jmin (1.0, v (IDs::grooveAmt) / 100.0 + 0.5 * v (IDs::mFlow) / 100.0);
    r.rhythm.accentAmount = v (IDs::accentAmt) / 100.0;
    r.groove = groove;
    return r;
}

juce::String VocalisProcessor::requestSignature() const
{
    const char* ids[] = { IDs::engine, IDs::voiceA, IDs::voiceB, IDs::voiceC, IDs::voiceD, IDs::orbX, IDs::orbY, IDs::variant,
                          IDs::robotVariant, IDs::language, IDs::speed, IDs::mode,
                          IDs::pitch, IDs::intonation, IDs::formant, IDs::robotize, IDs::robotHz,
                          IDs::division, IDs::stepsPerSyl, IDs::swing, IDs::legato, IDs::stretch, IDs::pCenter,
                          IDs::grooveAmt, IDs::accentAmt, IDs::loudness, IDs::vibrato, IDs::glide, IDs::key, IDs::scale,
                          IDs::harm1, IDs::harm2, IDs::harmLevel, IDs::harmWidth, IDs::doubleOn, IDs::doubleLevel,
                          IDs::mExpr, IDs::mFlow, IDs::mBreath, IDs::mChoir };
    juce::String s = text + "|" + juce::String (sampleRate) + "|" + juce::String (getEffectiveBpm(), 2)
                   + "|" + juce::String (hostBeatsPerBar.load()) + "|g" + juce::String (grooveVersion)
                   + "|d" + dictionary + "|e" + editsToString() + "|m" + juce::String (melodyVersion);
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
        status = c->engineName + "  -  " + juce::String (c->syllableLabels.size()) + " sillabe  -  "
               + juce::String (c->lengthBeats / juce::jmax (1, hostBeatsPerBar.load()), 1) + " battute  -  "
               + juce::String (c->renderMs / 1000.0, 2) + " s";
        if (c->warning.isNotEmpty())
            status = c->warning;

        // il testo e' cambiato: le modifiche manuali valgono solo per le sillabe che sono rimaste uguali
        bool changed = false;
        for (size_t i = 0; i < edits.size(); ++i)
        {
            if (edits[i].isDefault()) continue;
            if ((int) i >= c->syllableLabels.size() || (edits[i].label.isNotEmpty() && edits[i].label != c->syllableLabels[(int) i]))
            {
                edits[i] = {};
                changed = true;
            }
        }
        if (changed)
            forceRender();
    }
    sendChangeMessage();
}

RenderedClip::Ptr VocalisProcessor::getClip() const
{
    const juce::SpinLock::ScopedLockType l (clipLock);
    return clip;
}

void VocalisProcessor::setEdit (int index, const SyllableEdit& e)
{
    if (index < 0) return;
    if ((int) edits.size() <= index) edits.resize ((size_t) index + 1);
    edits[(size_t) index] = e;
    if (auto c = getClip(); c != nullptr && index < c->syllableLabels.size())
        edits[(size_t) index].label = c->syllableLabels[index];
    sendChangeMessage();
}

SyllableEdit VocalisProcessor::getEdit (int index) const
{
    return (index >= 0 && index < (int) edits.size()) ? edits[(size_t) index] : SyllableEdit {};
}

void VocalisProcessor::resetEdits()
{
    edits.clear();
    sendChangeMessage();
}

bool VocalisProcessor::hasEdits() const
{
    for (auto& e : edits) if (! e.isDefault()) return true;
    return false;
}

juce::String VocalisProcessor::editsToString() const
{
    juce::StringArray parts;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const auto& e = edits[i];
        if (e.isDefault()) continue;
        parts.add (juce::String ((int) i) + ":" + juce::String (e.shiftBeats, 5) + ":" + juce::String (e.lengthScale, 4) + ":"
                   + juce::String (e.pitchSemis, 3) + ":" + juce::String (e.gainDb, 2) + ":" + (e.muted ? "1" : "0") + ":"
                   + juce::Base64::toBase64 (e.label) + ":" + juce::String (e.note) + ":" + juce::String (e.onsetScale, 3) + ":"
                   + juce::String ((e.vibrato ? 1 : 0) | (e.glide ? 2 : 0) | (e.whisper ? 4 : 0) | (e.adlib ? 8 : 0)));
    }
    return parts.joinIntoString (";");
}

void VocalisProcessor::editsFromString (const juce::String& str)
{
    edits.clear();
    for (auto tok : juce::StringArray::fromTokens (str, ";", ""))
    {
        auto p = juce::StringArray::fromTokens (tok, ":", "");
        if (p.size() < 6) continue;
        const int i = p[0].getIntValue();
        if (i < 0 || i > 4096) continue;
        if ((int) edits.size() <= i) edits.resize ((size_t) i + 1);
        auto& e = edits[(size_t) i];
        e.shiftBeats = p[1].getDoubleValue(); e.lengthScale = p[2].getDoubleValue();
        e.pitchSemis = p[3].getFloatValue(); e.gainDb = p[4].getFloatValue(); e.muted = p[5] == "1";
        if (p.size() > 6) { juce::MemoryOutputStream mo; juce::Base64::convertFromBase64 (mo, p[6]); e.label = mo.toUTF8(); }
        if (p.size() > 9)
        {
            e.note = p[7].getIntValue(); e.onsetScale = p[8].getFloatValue();
            const int f = p[9].getIntValue();
            e.vibrato = (f & 1) != 0; e.glide = (f & 2) != 0; e.whisper = (f & 4) != 0; e.adlib = (f & 8) != 0;
        }
    }
}

void VocalisProcessor::setDictionary (const juce::String& d)
{
    dictionary = d;
    sendChangeMessage();
}

juce::StringArray VocalisProcessor::harmonyNames()
{
    return { "Spenta", "+3a", "+4a", "+5a", "+6a", "+8a", "-3a", "-4a", "-5a", "-6a", "-8a" };
}

int VocalisProcessor::harmonyDegrees (int i)
{
    static const int d[] = { 0, 2, 3, 4, 5, 7, -2, -3, -4, -5, -7 };
    return d[juce::jlimit (0, 10, i)];
}

void VocalisProcessor::setMelody (std::vector<NoteEvent> m)
{
    std::sort (m.begin(), m.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.startBeat < b.startBeat; });
    melody = std::move (m);
    ++melodyVersion;
    sendChangeMessage();
}

bool VocalisProcessor::loadMidiFile (const juce::File& f, juce::String& error)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
    {
        error = "File MIDI non leggibile";
        return false;
    }
    const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 480.0;
    std::vector<NoteEvent> notes;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (! ev->message.isNoteOn()) continue;
            const double start = ev->message.getTimeStamp() / tpq;
            const double end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / tpq : start + 0.5;
            notes.push_back ({ start, juce::jmax (1.0 / 32.0, end - start), ev->message.getNoteNumber() });
        }
    }
    if (notes.empty())
    {
        error = "Il file MIDI non contiene note";
        return false;
    }
    // accordi: si tiene la nota piu' alta di quelle che partono insieme (una nota per sillaba)
    std::sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
               { return a.startBeat < b.startBeat || (std::abs (a.startBeat - b.startBeat) < 1e-6 && a.pitch > b.pitch); });
    std::vector<NoteEvent> mono;
    for (auto& n : notes)
        if (mono.empty() || n.startBeat - mono.back().startBeat > 1.0 / 64.0) mono.push_back (n);
    // la prima nota fissa l'inizio
    const double first = std::floor (mono.front().startBeat / juce::jmax (1, hostBeatsPerBar.load())) * juce::jmax (1, hostBeatsPerBar.load());
    for (auto& n : mono) n.startBeat -= first;
    setMelody (std::move (mono));
    return true;
}

void VocalisProcessor::startMidiCapture()
{
    capturedCount = 0;
    captureStartPpq = -1.0;
    capturing = true;
    sendChangeMessage();
}

void VocalisProcessor::stopMidiCapture()
{
    capturing = false;
    const int n = juce::jmin ((int) captured.size(), capturedCount.load());
    std::vector<NoteEvent> notes;
    std::map<int, size_t> open;
    const double barLen = juce::jmax (1, hostBeatsPerBar.load());
    const double startBar = (apvts.getRawParameterValue (IDs::startBar)->load() - 1.0) * barLen;
    double origin = startBar;
    for (int i = 0; i < n; ++i)
        if (captured[(size_t) i].on) { origin = std::floor ((captured[(size_t) i].ppq - startBar) / barLen) * barLen + startBar; break; }
    for (int i = 0; i < n; ++i)
    {
        const auto& e = captured[(size_t) i];
        if (e.on)
        {
            open[e.pitch] = notes.size();
            notes.push_back ({ e.ppq - origin, 0.25, e.pitch });
        }
        else if (auto it = open.find (e.pitch); it != open.end())
        {
            notes[it->second].lengthBeats = juce::jmax (1.0 / 32.0, e.ppq - origin - notes[it->second].startBeat);
            open.erase (it);
        }
    }
    if (! notes.empty())
        setMelody (std::move (notes));
    else
        sendChangeMessage();
}

juce::String VocalisProcessor::getTextWordForSyllable (int k) const
{
    auto c = getClip();
    if (c == nullptr || k < 0 || k >= (int) c->syllableWordIndex.size()) return {};
    juce::StringArray words;
    for (auto w : juce::StringArray::fromTokens (text, " \n\t", "\""))
    {
        w = w.trimCharactersAtStart (",.;:!?\"()«»“”-").trimCharactersAtEnd (",.;:!?\"()«»“”-");
        if (w.isNotEmpty()) words.add (w);
    }
    const int wi = c->syllableWordIndex[(size_t) k];
    // solo se le parole del testo corrispondono una a una a quelle dei fonemi
    int numWords = 0; for (auto i : c->syllableWordIndex) numWords = juce::jmax (numWords, i + 1);
    if (numWords != words.size() || wi < 0 || wi >= words.size()) return {};
    return words[wi].toLowerCase();
}

juce::String VocalisProcessor::getPronunciation (const juce::String& word) const
{
    for (auto line : juce::StringArray::fromLines (dictionary))
        if (line.upToFirstOccurrenceOf ("=", false, false).trim().equalsIgnoreCase (word))
            return line.fromFirstOccurrenceOf ("=", false, false).trim().trimCharactersAtStart ("/").trimCharactersAtEnd ("/");
    return {};
}

void VocalisProcessor::setPronunciation (const juce::String& word, const juce::String& ipa)
{
    if (word.trim().isEmpty()) return;
    juce::StringArray lines;
    for (auto line : juce::StringArray::fromLines (dictionary))
        if (line.trim().isNotEmpty() && ! line.upToFirstOccurrenceOf ("=", false, false).trim().equalsIgnoreCase (word.trim()))
            lines.add (line);
    if (ipa.trim().isNotEmpty())
        lines.add (word.trim().toLowerCase() + " = /" + ipa.trim().trimCharactersAtStart ("/").trimCharactersAtEnd ("/") + "/");
    setDictionary (lines.joinIntoString ("\n"));
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
    p.deessOn = v (IDs::deessOn) > 0.5f;   p.deessAmount = v (IDs::deessAmount);
    p.doublerOn = v (IDs::doublerOn) > 0.5f; p.doublerMix = v (IDs::doublerMix); p.doublerWidth = v (IDs::doublerWidth);
    p.limiterOn = v (IDs::limiterOn) > 0.5f;
    p.outGainDb = v (IDs::outGain);
    p.warmth = v (IDs::mWarmth) / 100.0f;
    p.space = v (IDs::mSpace) / 100.0f;
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
    if (previewRequest.exchange (false)) { voicePos = c != nullptr ? c->triggerStart : 0; voiceGain = 1.0f; }

    const bool syncMode = (int) apvts.getRawParameterValue (IDs::playMode)->load() == 0;
    double shownPos = -1.0;
    const int outCh = buffer.getNumChannels();

    // cattura MIDI: note suonate dalla DAW mentre il trasporto va (posizioni in quarti)
    if (capturing.load() && hostPlaying)
    {
        const double beatsPerSampleC = bpm / 60.0 / sampleRate;
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (! msg.isNoteOnOrOff()) continue;
            const int idx = capturedCount.load();
            if (idx >= (int) captured.size()) break;
            captured[(size_t) idx] = { ppq + m.samplePosition * beatsPerSampleC, msg.getNoteNumber(), msg.isNoteOn() };
            capturedCount = idx + 1;
        }
    }

    if (c != nullptr && c->audio.getNumSamples() > 0)
    {
        const int len = c->audio.getNumSamples();
        const int clipCh = c->audio.getNumChannels();
        auto put = [&] (int i, int idx, float gain)
        {
            for (int ch = 0; ch < outCh; ++ch)
                buffer.addSample (ch, i, c->audio.getSample (juce::jmin (ch, clipCh - 1), idx) * gain);
        };

        if (syncMode && hostPlaying)
        {
            voicePos = -1;
            const double startPpq = (apvts.getRawParameterValue (IDs::startBar)->load() - 1.0) * beatsPerBar;
            const bool looping = apvts.getRawParameterValue (IDs::loop)->load() > 0.5f;
            const double beatsPerSample = bpm / 60.0 / sampleRate;
            const double clipSamplesPerBeat = 60.0 / c->bpm * c->sampleRate;
            const double pre = c->prerollBeats, L = c->lengthBeats;

            for (int i = 0; i < n; ++i)
            {
                const double rel = ppq + i * beatsPerSample - startPpq;   // quarti dalla battuta di partenza
                if (rel < -pre) continue;
                auto play = [&] (double posBeats)
                {
                    const int idx = (int) std::floor ((posBeats + pre) * clipSamplesPerBeat + 0.5);
                    if (idx >= 0 && idx < len) { put (i, idx, 1.0f); shownPos = idx; }
                };
                if (rel < 0.0) { play (rel); continue; }                    // anticipo prima della battuta
                if (looping && L > 0.0)
                {
                    const double b = std::fmod (rel, L);
                    play (b);
                    if (b >= L - pre) play (b - L);                           // anticipo del giro successivo
                }
                else if (rel < L)
                    play (rel);
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
                    if (! syncMode && m.isNoteOn()) { pos = c->triggerStart; voiceGain = m.getFloatVelocity(); }
                    ++it;
                }
                if (pos >= 0)
                {
                    if (pos < len) { put (i, pos, voiceGain); shownPos = pos; ++pos; }
                    else pos = -1;
                }
            }
            voicePos = pos;
        }
    }
    displayPos = shownPos;

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
        st.copyFrom (ch, 0, c->audio, juce::jmin (ch, c->audio.getNumChannels() - 1), 0, c->audio.getNumSamples());

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
    // se la clip ha l'anticipo, il file parte quel numero di quarti prima della battuta: lo dice il nome
    const auto c = getClip();
    const juce::String pre = c != nullptr && c->prerollBeats > 0.0 ? "_pre" + juce::String ((int) c->prerollBeats) + "q" : juce::String();
    auto f = dir.getChildFile ("Vocalis_" + name + pre + "_" + juce::String (juce::Time::getCurrentTime().toMilliseconds() % 100000) + ".wav");
    if (! exportWav (f, error))
        return {};
    return f;
}

// ============================================================================ Stato

void VocalisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("text", text, nullptr);
    state.setProperty ("dictionary", dictionary, nullptr);
    state.setProperty ("edits", editsToString(), nullptr);
    {
        juce::StringArray m;
        for (auto& n : melody) m.add (juce::String (n.startBeat, 6) + ":" + juce::String (n.lengthBeats, 6) + ":" + juce::String (n.pitch));
        state.setProperty ("melody", m.joinIntoString (";"), nullptr);
    }
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
        dictionary = state.getProperty ("dictionary").toString();
        editsFromString (state.getProperty ("edits").toString());
        melody.clear();
        for (auto t : juce::StringArray::fromTokens (state.getProperty ("melody").toString(), ";", ""))
        {
            auto q = juce::StringArray::fromTokens (t, ":", "");
            if (q.size() == 3) melody.push_back ({ q[0].getDoubleValue(), q[1].getDoubleValue(), q[2].getIntValue() });
        }
        ++melodyVersion;
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
