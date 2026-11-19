// Test unitari del motore v3 (senza ONNX Runtime): sillabe a livello di token, tempi, intonazione.
#include "Engine/TokenSyllables.h"
#include "Engine/KokoroVoices.h"
#include "DSP/TimingPlanner.h"
#include "DSP/PitchPlanner.h"
#include "DSP/TimeStretch.h"
#include "DSP/EffectsChain.h"
#include <iostream>

using namespace vocalis;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "FAIL " << msg << "  (" #cond ")\n"; } } while (0)

static juce::String labels (const std::vector<TokenSyllable>& s)
{
    juce::StringArray a;
    for (auto& x : s) a.add (x.label);
    return a.joinIntoString ("-");
}

static void testTokenSyllables()
{
    // "ciao, questa e' la nuova voce" (fonemi come li produce Phonemizer)
    auto s = TokenSyllables::build (juce::CharPointer_UTF8 ("ʧˈao, kwˈesta ˈɛ la nʊˈɔva vˈoʧe."));
    CHECK (labels (s) == juce::String (juce::CharPointer_UTF8 ("ʧao-kwe-sta-ɛ-la-nʊ-ɔ-va-vo-ʧe")), "sillabe: " << labels (s));
    CHECK (s.size() == 10, "numero sillabe " << s.size());
    if (s.size() == 10)
    {
        CHECK (s[0].pauseSteps == 1, "virgola dopo ciao -> 1 pausa");
        CHECK (s[9].pauseSteps == 2, "punto finale -> 2 pause");
        CHECK (s[0].stressed && ! s[2].stressed && s[1].stressed, "accenti");
        CHECK (s[1].wordStart && ! s[2].wordStart, "inizio parola");
        // indici: il nucleo di "ciao" sono i token 2..3 ("ao"), l'attacco parte da 0
        CHECK (s[0].firstToken == 0 && s[0].nucleusFirst == 2 && s[0].nucleusLast == 3, "indici ciao");
        // ogni token appartiene a esattamente una sillaba, in ordine
        bool contiguous = true;
        for (size_t i = 1; i < s.size(); ++i) contiguous &= s[i].firstToken == s[i - 1].lastToken + 1;
        CHECK (contiguous, "sillabe contigue");
        CHECK (s.back().lastToken == juce::String (juce::CharPointer_UTF8 ("ʧˈao, kwˈesta ˈɛ la nʊˈɔva vˈoʧe.")).length() - 1, "ultima sillaba copre la fine");
    }
    // nesso consonante + r resta insieme: "altro" -> al-tro
    auto t = TokenSyllables::build (juce::CharPointer_UTF8 ("ˈaltro"));
    CHECK (labels (t) == "al-tro", "altro: " << labels (t));
    // geminata: la consonante lunga apre la sillaba dopo ("gnocchi" -> ɲo-kɪ)
    auto g = TokenSyllables::build (juce::CharPointer_UTF8 ("ɲˈokːɪ"));
    CHECK (labels (g) == juce::String (juce::CharPointer_UTF8 ("ɲo-kɪ")), "gnocchi: " << labels (g));
    // nessuna vocale: nessuna sillaba, nessun crash
    CHECK (TokenSyllables::build ("...").empty(), "solo punteggiatura");
}

static void testVoiceMix()
{
    auto f = juce::File (juce::SystemStats::getEnvironmentVariable ("VOCALIS_MODELS", "/home/claude/models")).getChildFile ("voices-v1.0.bin");
    if (! f.existsAsFile()) { std::cout << "(voci non trovate: salto testVoiceMix)\n"; return; }
    KokoroVoices v; juce::String err;
    CHECK (v.open (f, err), "apertura voci " << err);
    std::vector<float> a, b, ab, mix;
    v.styleFor ("if_sara", {}, 0.0f, 20, a, err);
    v.styleFor ("if_sara", "im_nicola", 0.5f, 20, ab, err);

    VoiceMix one; one.id[0] = "if_sara"; one.w[0] = 1.0f;
    CHECK (v.styleFor (one, 20, 0, mix, err) && mix == a, "una voce = stile originale");

    VoiceMix two; two.id[0] = "if_sara"; two.id[1] = "im_nicola"; two.w[0] = 2.0f; two.w[1] = 2.0f;
    two.id[2] = "af_heart"; two.w[2] = 0.0f;           // peso zero: ignorata
    two.id[3] = ""; two.w[3] = 5.0f;                   // senza nome: ignorata
    CHECK (v.styleFor (two, 20, 0, mix, err), "miscela " << err);
    float maxDiff = 0; for (size_t i = 0; i < mix.size(); ++i) maxDiff = std::max (maxDiff, std::abs (mix[i] - ab[i]));
    CHECK (maxDiff < 1e-5f, "50/50 normalizzato = vecchia miscela, diff " << maxDiff);

    v.styleFor ("if_sara", {}, 0.0f, 32, b, err);
    CHECK (v.styleFor (one, 20, 12, mix, err) && mix == b, "variante: riga spostata");
    CHECK (v.styleFor (one, 500, 100, mix, err) && mix.size() == (size_t) KokoroVoices::styleDim, "riga oltre la fine: limitata");

    VoiceMix none;
    CHECK (! v.styleFor (none, 20, 0, mix, err), "nessuna voce: errore");
}

// fonemi di prova e durate "predette" plausibili (frame da 25 ms): consonanti 2, vocali 4, spazi 1, punteggiatura 6
static std::vector<int64_t> fakePredicted (const juce::String& ps)
{
    std::vector<int64_t> d { 4 };
    for (int i = 0; i < ps.length(); ++i)
    {
        const auto c = ps[i];
        d.push_back (TokenSyllables::isVowel (c) ? 4 : c == ' ' ? 1 : (c == ',' || c == '.') ? 6 : (c == 0x02C8 ? 0 : 2));
    }
    d.push_back (6);
    return d;
}

static int64_t cumFrames (const std::vector<int64_t>& d, int token) // frame d'inizio del token
{
    int64_t s = 0;
    for (int i = 0; i <= token; ++i) s += d[(size_t) i];   // d[0] = inizio frase
    return s;
}

static void testTimingGrid()
{
    const juce::String ps (juce::CharPointer_UTF8 ("ʧˈao, kwˈesta ˈɛ la nʊˈɔva vˈoʧe."));
    auto sy = TokenSyllables::build (ps);
    RhythmSettings rs; rs.bpm = 120; rs.stepBeats = 0.25; rs.stepsPerSyllable = 1; rs.swing = 0.5; rs.restStepsMax = 4;
    auto t = TimingPlanner::plan (sy, rs, {}, {}, VocalMode::Rap, {});
    CHECK (t.size() == sy.size(), "un target per sillaba");
    if (t.size() < 3) return;
    CHECK (std::abs (t[0].anchorBeat) < 1e-9, "prima vocale sul battere");
    CHECK (std::abs (t[1].anchorBeat - 0.5) < 1e-9, "virgola: una pausa (ciao | kwe) -> 0.5, era " << t[1].anchorBeat);
    CHECK (std::abs (t[2].anchorBeat - 0.75) < 1e-9, "sillaba successiva allo step dopo");

    auto pred = fakePredicted (ps);
    const double pre = TimingPlanner::prerollBeats (pred, sy, t, rs.bpm);
    CHECK (std::abs (pre - 1.0) < 1e-9, "consonanti iniziali prima del battere -> anticipo 1 quarto, era " << pre);
    auto d = TimingPlanner::durations (ps, pred, sy, t, {}, rs.bpm, -pre);
    CHECK (d.durations.size() == pred.size(), "lunghezza durate");
    // ogni vocale realizzata entro mezzo frame dal target (diffusione dell'errore), target = (beat+pre)*20 frame/quarto
    double worst = 0;
    for (size_t k = 0; k < sy.size(); ++k)
    {
        const double want = (t[k].anchorBeat + pre) * 20.0;
        const double got = cumFrames (d.durations, sy[k].nucleusFirst) + d.padSamples / 600.0;
        worst = std::max (worst, std::abs (got - want));
        CHECK (d.vowelFrame[k] == (int) cumFrames (d.durations, sy[k].nucleusFirst), "vowelFrame coerente");
    }
    CHECK (d.onsetFrame.size() == sy.size() && d.onsetFrame[1] == (int) cumFrames (d.durations, sy[1].firstToken), "onsetFrame");
    CHECK (d.endFrame[0] <= d.onsetFrame[1] + 6 && d.endFrame[0] > d.vowelFrame[0], "fine voce prima della sillaba dopo (pausa)");
    CHECK (worst <= 0.5 + 1e-9, "vocali sulla griglia entro mezzo frame, peggiore " << worst);
    bool positive = true; for (size_t i = 0; i + 1 < pred.size(); ++i) if (TokenSyllables::isVowel (ps[(int) i]) && d.durations[i + 1] < 1) positive = false;
    CHECK (positive, "ogni vocale almeno un frame");

    // swing: gli step dispari ritardati
    rs.swing = 0.75;
    auto sw = TimingPlanner::plan (sy, rs, {}, {}, VocalMode::Rap, {});
    CHECK (std::abs (sw[2].anchorBeat - (0.75 + 0.125)) < 1e-9, "swing sullo step dispari");
}

static void testTimingEdgeCases()
{
    const juce::String ps (juce::CharPointer_UTF8 ("strˈada strˈetta"));
    auto sy = TokenSyllables::build (ps);
    RhythmSettings rs; rs.bpm = 200; rs.stepBeats = 0.125;           // 1/32 a 200 BPM: 37 ms per sillaba
    auto t = TimingPlanner::plan (sy, rs, {}, {}, VocalMode::Rap, {});
    auto pred = fakePredicted (ps);
    auto d = TimingPlanner::durations (ps, pred, sy, t, {}, rs.bpm, -TimingPlanner::prerollBeats (pred, sy, t, rs.bpm));
    bool vowelsOk = true, mono = true;
    for (int i = 0; i < ps.length(); ++i) if (TokenSyllables::isVowel (ps[i]) && d.durations[(size_t) i + 1] < 1) vowelsOk = false;
    for (size_t k = 1; k < d.vowelFrame.size(); ++k) if (d.vowelFrame[k] <= d.vowelFrame[k - 1]) mono = false;
    CHECK (vowelsOk, "tempo velocissimo: vocali >= 1 frame");
    CHECK (mono, "tempo velocissimo: vocali in ordine");

    // modifica che sposta una sillaba prima della precedente: viene limitata, l'ordine resta
    std::vector<SyllableEdit> e (2); e[1].shiftBeats = -2.0;
    rs.bpm = 120; rs.stepBeats = 0.25;
    auto t2 = TimingPlanner::plan (sy, rs, {}, e, VocalMode::Rap, {});
    CHECK (t2[1].anchorBeat > t2[0].anchorBeat, "sovrapposizione limitata");

    // Canto: le note decidono i tempi; meno note che sillabe -> le altre seguono sulla griglia
    std::vector<NoteEvent> notes { { 0.0, 1.0, 60 }, { 1.0, 0.5, 64 } };
    auto c = TimingPlanner::plan (sy, rs, {}, {}, VocalMode::Canto, notes);
    CHECK (c.size() == sy.size() && c[1].noteMidi == 64 && std::abs (c[1].anchorBeat - 1.0) < 1e-9, "nota 2 sulla sillaba 2");
    CHECK (std::abs (c[0].endBeat - 1.0) < 1e-9, "fine sillaba = fine nota");
    CHECK (c[2].anchorBeat >= 1.5 - 1e-9 && c[2].noteMidi == 64, "dopo le note: griglia, ultima nota tenuta");

    // Canto: le consonanti si accorciano (accenti e spazi a zero), la vocale prende quasi tutta la nota
    {
        const juce::String cps (juce::CharPointer_UTF8 ("lˈuna splˈɛnde"));
        auto csy = TokenSyllables::build (cps);       // lu-na-splɛn-de
        std::vector<NoteEvent> cn { { 0, 0.5, 60 }, { 0.5, 0.5, 62 }, { 1.0, 0.5, 64 }, { 1.5, 1.0, 62 } };
        RhythmSettings crs; crs.bpm = 90;
        auto ct = TimingPlanner::plan (csy, crs, {}, {}, VocalMode::Canto, cn);
        auto cpred = fakePredicted (cps);
        for (int i = 0; i < cps.length(); ++i) if (cps[i] == 0x02C8) cpred[(size_t) i + 1] = 2;   // accenti "lunghi" come nel modello
        auto cd = TimingPlanner::durations (cps, cpred, csy, ct, {}, crs.bpm, -TimingPlanner::prerollBeats (cpred, csy, ct, crs.bpm), VocalMode::Canto);
        const int aOfNa = csy[1].nucleusFirst;
        const double spanNa = 0.5 * 40.0 * 60.0 / 90.0;          // 13,3 frame
        CHECK (cd.durations[(size_t) aOfNa + 1] >= 0.6 * spanNa, "Canto: la vocale di 'na' tiene la nota, frame " << cd.durations[(size_t) aOfNa + 1]);
        bool zeroMarks = true;
        for (int i = 0; i < cps.length(); ++i) if ((cps[i] == ' ' || cps[i] == 0x02C8) && cd.durations[(size_t) i + 1] != 0) zeroMarks = false;
        CHECK (zeroMarks, "Canto: accenti e spazi senza durata");
    }

    // Parlato: nessun target (tempi naturali)
    CHECK (TimingPlanner::plan (sy, rs, {}, {}, VocalMode::Parlato, {}).empty(), "Parlato senza griglia");
}

static void testPitch()
{
    const juce::String ps (juce::CharPointer_UTF8 ("lˈa kˈasa"));     // la-ka-sa
    auto sy = TokenSyllables::build (ps);
    std::vector<int64_t> dur { 2, 2, 0, 20, 1, 2, 0, 20, 3, 20, 3 };   // vocali lunghe (500 ms)
    int64_t tot = 0; for (auto d : dur) tot += d;
    std::vector<float> nat ((size_t) (2 * tot));
    for (size_t i = 0; i < nat.size(); ++i) nat[i] = 200.0f * std::pow (2.0f, 0.15f * std::sin ((float) i * 0.07f));
    nat[10] = 0.0f;                                                    // un punto non intonato
    auto f0At = [&] (int token, double fraction) {                     // f0 a una frazione del token
        int64_t s = 0; for (int i = 0; i <= token; ++i) s += dur[(size_t) i];
        return (size_t) (2 * (s + (int64_t) (fraction * dur[(size_t) token + 1]))); };

    PitchSettings ps0;
    CHECK (PitchPlanner::isIdentity ({}, {}, ps0), "impostazioni neutre = identita'");
    CHECK (PitchPlanner::build (nat, dur, sy, {}, {}, ps0) == nat, "identita' esatta");

    auto up = ps0; up.globalSemis = 12.0f;
    auto o = PitchPlanner::build (nat, dur, sy, {}, {}, up);
    CHECK (std::abs (o[50] - 2.0f * nat[50]) < 0.01f && o[10] == 0.0f, "+12 st raddoppia, non intonato resta 0");

    auto flat = ps0; flat.intonation = 0.0f;
    o = PitchPlanner::build (nat, dur, sy, {}, {}, flat);
    float lo = 1e9f, hi = 0; for (size_t i = 0; i < o.size(); ++i) if (o[i] > 0) { lo = std::min (lo, o[i]); hi = std::max (hi, o[i]); }
    CHECK (hi / lo < 1.01f, "intonazione 0 = monotono");

    std::vector<SyllableEdit> e (3); e[1].pitchSemis = 5.0f;
    o = PitchPlanner::build (nat, dur, sy, {}, e, ps0);
    const auto a1 = f0At (2, 0.5), b1 = f0At (6, 0.5);                 // vocale sillaba 0 e sillaba 1
    CHECK (std::abs (o[a1] - nat[a1]) < 1e-3f, "modifica: sillaba 0 invariata");
    CHECK (std::abs (PitchPlanner::hzToMidi (o[b1]) - PitchPlanner::hzToMidi (nat[b1]) - 5.0f) < 0.05f, "modifica: sillaba 1 +5 st");

    std::vector<SyllableEdit> w (3); w[2].whisper = true;
    o = PitchPlanner::build (nat, dur, sy, {}, w, ps0);
    CHECK (o[f0At (8, 0.5)] == 0.0f && o[a1] > 0, "sussurrato: F0 a zero solo sulla sillaba");

    auto sing = ps0; sing.mode = VocalMode::Canto; sing.expressivity = 0.0f;
    std::vector<int> notes { 69, 72, 64 };
    o = PitchPlanner::build (nat, dur, sy, notes, {}, sing);
    CHECK (std::abs (o[a1] - 440.0f) < 4.4f, "Canto: La 440 al centro della vocale, era " << o[a1]);
    CHECK (std::abs (o[b1] - PitchPlanner::midiToHz (72)) < 5.3f, "Canto: Do5 sulla seconda sillaba");
    float maxJump = 0; for (size_t i = 1; i < o.size(); ++i) if (o[i] > 0 && o[i - 1] > 0) maxJump = std::max (maxJump, std::abs (PitchPlanner::hzToMidi (o[i]) - PitchPlanner::hzToMidi (o[i - 1])));
    CHECK (maxJump < 1.0f, "glide: nessun salto brusco tra note, max " << maxJump << " st");

    // Canto: la vocale resta intonata anche dove il modello farebbe cadere la F0 (fine frase)
    {
        auto drop = nat;
        for (size_t i = f0At (8, 0.3); i < f0At (8, 0.9); ++i) drop[i] = 5.0f;
        auto od = PitchPlanner::build (drop, dur, sy, notes, {}, sing);
        CHECK (std::abs (od[f0At (8, 0.6)] - PitchPlanner::midiToHz (64)) < 4.0f, "Canto: vocale sempre intonata, era " << od[f0At (8, 0.6)]);
    }

    // energia: nel Canto le vocali non si spengono
    {
        std::vector<float> nN (nat.size(), 8.0f);
        const size_t v0 = f0At (8, 0.0), v1 = f0At (8, 1.0);
        for (size_t i = v0; i < v1; ++i) nN[i] = 8.0f - 18.0f * (float) (i - v0) / (float) (v1 - v0);   // decade fino a -10
        CHECK (PitchPlanner::sustainEnergy (nN, dur, sy, VocalMode::Rap) == nN, "energia invariata fuori dal Canto");
        auto en = PitchPlanner::sustainEnergy (nN, dur, sy, VocalMode::Canto);
        CHECK (en[f0At (8, 0.7)] > 7.0f, "Canto: vocale finale sostenuta, era " << en[f0At (8, 0.7)]);
        CHECK (en[v1 - 1] < en[f0At (8, 0.7)], "Canto: rilascio alla fine della vocale");
        CHECK (en[f0At (7, 0.5)] == nN[f0At (7, 0.5)], "consonanti invariate");
    }

    auto vib = sing; vib.vibratoCents = 50.0f;
    o = PitchPlanner::build (nat, dur, sy, notes, {}, vib);
    float dev = 0, early = 0;
    for (size_t i = f0At (2, 0.0); i < f0At (2, 0.9); ++i)
    {
        if (o[i] <= 0.0f) continue;   // punto non intonato
        const float c = std::abs (1200.0f * std::log2 (o[i] / 440.0f));
        if (i < f0At (2, 0.0) + 12) early = std::max (early, c); else dev = std::max (dev, c);
    }
    CHECK (dev > 35.0f && dev < 65.0f, "vibrato ~50 cent, era " << dev);
    CHECK (early < 20.0f, "vibrato non subito all'attacco, era " << early);

    // valori minuscoli del modello nelle consonanti sorde: non sono note e passano invariati
    std::vector<float> low (nat); for (size_t i = 0; i < 6; ++i) low[i] = 3.0f;
    CHECK (PitchPlanner::medianMidi (low, 0, 6) == 0.0f, "F0 < 40 Hz = non intonato");
    o = PitchPlanner::build (low, dur, sy, {}, {}, up);
    CHECK (o[2] == 3.0f, "F0 sorda invariata anche con trasposizione");

    CHECK (PitchPlanner::diatonicShift (60, 2, 0, false) == 4, "Do maggiore: terza sopra Do = +4");
    CHECK (PitchPlanner::diatonicShift (64, 2, 0, false) == 3, "terza sopra Mi = +3");
    CHECK (PitchPlanner::diatonicShift (69, 2, 9, true) == 3, "La minore: terza sopra La = +3");
    CHECK (PitchPlanner::diatonicShift (60, 4, 0, false) == 7, "quinta sopra Do = +7");
    CHECK (PitchPlanner::diatonicShift (60, -7, 0, false) == -12, "ottava sotto = -12");
    CHECK (PitchPlanner::diatonicShift (61, 2, 0, false) == 3 || PitchPlanner::diatonicShift (61, 2, 0, false) == 4, "nota fuori scala: terza ragionevole");
}

static void testWarp()
{
    const double sr = 24000.0;
    std::vector<float> x (24000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * std::sin (2.0f * 3.14159265f * 150.0f * (float) i / (float) sr);
    x[12000] = 1.0f;                                                       // click a 0,5 s
    auto same = TimeStretch::warp (x.data(), (int) x.size(), { 0, 24000 }, { 0, 24000 }, sr);
    CHECK (same == x, "mappa identita' = ingresso");

    auto w = TimeStretch::warp (x.data(), (int) x.size(), { 0, 12000, 24000 }, { 0, 12240, 24000 }, sr);
    CHECK (w.size() == 24000, "lunghezza uscita = ultimo punto");
    int peak = 0; for (int i = 1; i < (int) w.size(); ++i) if (std::abs (w[(size_t) i]) > std::abs (w[(size_t) peak])) peak = i;
    CHECK (std::abs (peak - 12240) <= 120, "click spostato di +10 ms (entro 5 ms), trovato a " << peak);
    float rmsIn = 0, rmsOut = 0; for (int i = 2000; i < 10000; ++i) { rmsIn += x[(size_t) i] * x[(size_t) i]; rmsOut += w[(size_t) i] * w[(size_t) i]; }
    CHECK (std::abs (std::sqrt (rmsOut / rmsIn) - 1.0f) < 0.1f, "livello conservato");
}

static void testMacros()
{
    const double sr = 48000.0;
    auto make = [&] { juce::AudioBuffer<float> b (2, 48000); b.clear();
                      juce::Random r (1); for (int i = 0; i < 12000; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, (r.nextFloat() * 2 - 1) * 0.2f); return b; };
    auto lowEnergy = [] (const juce::AudioBuffer<float>& b) {    // energia sotto ~300 Hz (media mobile)
        double e = 0, m = 0; for (int i = 0; i < 12000; ++i) { m += 0.04 * (b.getSample (0, i) - m); e += m * m; } return e; };
    auto tail = [] (const juce::AudioBuffer<float>& b) { return b.getMagnitude (0, 20000, 20000); };

    EffectsParams dry; dry.eqOn = dry.compOn = dry.deessOn = dry.limiterOn = false;
    auto a = make(); { EffectsChain c; c.prepare (sr, 512); c.process (a, dry, 120); }
    auto w = make(); auto pw = dry; pw.warmth = 1.0f; { EffectsChain c; c.prepare (sr, 512); c.process (w, pw, 120); }
    CHECK (lowEnergy (w) > 1.5 * lowEnergy (a), "Calore: piu' basse");
    auto sp = make(); auto ps = dry; ps.space = 1.0f; { EffectsChain c; c.prepare (sr, 512); c.process (sp, ps, 120); }
    CHECK (tail (a) < 1.0e-6f && tail (sp) > 1.0e-3f, "Spazio: coda di riverbero anche con il riverbero spento");
    CHECK (EffectsChain::tailSeconds (ps, 120) > 1.0, "Spazio: coda in export");
}

int main()
{
    testMacros();
    testWarp();
    testPitch();
    testTimingGrid();
    testTimingEdgeCases();
    testTokenSyllables();
    testVoiceMix();
    std::cout << (checks - failures) << "/" << checks << " controlli superati\n";
    return failures == 0 ? 0 : 1;
}
