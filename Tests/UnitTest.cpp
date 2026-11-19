// Test unitari del motore v3 (senza ONNX Runtime): sillabe a livello di token, tempi, intonazione.
#include "Engine/TokenSyllables.h"
#include "Engine/KokoroVoices.h"
#include "DSP/TimingPlanner.h"
#include "DSP/PitchPlanner.h"
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

    CHECK (PitchPlanner::diatonicShift (60, 2, 0, false) == 4, "Do maggiore: terza sopra Do = +4");
    CHECK (PitchPlanner::diatonicShift (64, 2, 0, false) == 3, "terza sopra Mi = +3");
    CHECK (PitchPlanner::diatonicShift (69, 2, 9, true) == 3, "La minore: terza sopra La = +3");
    CHECK (PitchPlanner::diatonicShift (60, 4, 0, false) == 7, "quinta sopra Do = +7");
    CHECK (PitchPlanner::diatonicShift (60, -7, 0, false) == -12, "ottava sotto = -12");
    CHECK (PitchPlanner::diatonicShift (61, 2, 0, false) == 3 || PitchPlanner::diatonicShift (61, 2, 0, false) == 4, "nota fuori scala: terza ragionevole");
}

int main()
{
    testPitch();
    testTimingGrid();
    testTimingEdgeCases();
    testTokenSyllables();
    testVoiceMix();
    std::cout << (checks - failures) << "/" << checks << " controlli superati\n";
    return failures == 0 ? 0 : 1;
}
