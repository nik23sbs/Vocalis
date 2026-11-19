// Test unitari del motore v3 (senza ONNX Runtime): sillabe a livello di token, tempi, intonazione.
#include "Engine/TokenSyllables.h"
#include "Engine/KokoroVoices.h"
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

int main()
{
    testTokenSyllables();
    testVoiceMix();
    std::cout << (checks - failures) << "/" << checks << " controlli superati\n";
    return failures == 0 ? 0 : 1;
}
