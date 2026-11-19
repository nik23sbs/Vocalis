#include "TokenSyllables.h"

namespace vocalis
{

bool TokenSyllables::isVowel (juce::juce_wchar c)
{
    static const juce::String v (juce::CharPointer_UTF8 ("aeiouyAEIOUYQWɑɐɒæɔəɘɚɛɜɝɞɤɨɪɯɵøœɶʉʊʌʏᵻ"));
    return v.containsChar (c);
}

namespace
{
    bool isStress (juce::juce_wchar c) { return c == 0x02C8 || c == 0x02CC; }
    bool isLength (juce::juce_wchar c) { return c == 0x02D0; }
    bool isBoundary (juce::juce_wchar c) { return c == ' ' || juce::String (juce::CharPointer_UTF8 (";:,.!?—…\"()“”«»")).containsChar (c); }
    bool isLiquidOrGlide (juce::juce_wchar c) { return c == 'l' || c == 'r' || c == 'j' || c == 'w' || c == 0x027E; }
    bool isObstruent (juce::juce_wchar c)
    {
        return juce::String (juce::CharPointer_UTF8 ("pbtdkgfvszʃʒʧʤʦʣθðxɣ")).containsChar (c);
    }
}

std::vector<TokenSyllable> TokenSyllables::build (const juce::String& ps)
{
    const int n = ps.length();
    std::vector<std::pair<int, int>> nuclei;
    for (int i = 0; i < n;)
    {
        if (isVowel (ps[i]))
        {
            int j = i;
            while (j + 1 < n && (isVowel (ps[j + 1]) || isLength (ps[j + 1]))) ++j;
            nuclei.push_back ({ i, j });
            i = j + 1;
        }
        else ++i;
    }

    std::vector<TokenSyllable> out;
    if (nuclei.empty()) return out;

    // inizio di ogni sillaba: tra due nuclei, dopo l'ultimo confine di parola, altrimenti regola delle consonanti
    std::vector<int> starts (nuclei.size(), 0);
    for (size_t k = 1; k < nuclei.size(); ++k)
    {
        const int a = nuclei[k - 1].second + 1, b = nuclei[k].first;   // [a, b) = tra le vocali
        int lastBoundary = -1;
        for (int i = a; i < b; ++i) if (isBoundary (ps[i])) lastBoundary = i;
        if (lastBoundary >= 0) { starts[k] = lastBoundary + 1; continue; }

        // consonanti (gli accenti seguono la consonante che precedono: restano con l'attacco)
        int s = b;
        while (s > a && isStress (ps[s - 1])) --s;
        if (s > a) --s;                                            // l'ultima consonante va alla sillaba dopo
        if (s > a && isLength (ps[s])) --s;                        // geminata (kː): la consonante lunga apre la sillaba
        while (s > a && (isStress (ps[s - 1]) || isLength (ps[s - 1]))) --s;
        if (s > a && isLiquidOrGlide (ps[s]) && isObstruent (ps[s - 1])) --s;   // tr, pl, kw ...
        if (s > a && (ps[s - 1] == 's' || ps[s - 1] == 'z') && isObstruent (ps[s])) --s; // s impura
        starts[k] = s;
    }

    for (size_t k = 0; k < nuclei.size(); ++k)
    {
        TokenSyllable t;
        t.firstToken = starts[k];
        t.nucleusFirst = nuclei[k].first;
        t.nucleusLast = nuclei[k].second;
        t.lastToken = k + 1 < nuclei.size() ? starts[k + 1] - 1 : n - 1;
        for (int i = t.firstToken; i <= t.lastToken; ++i)
        {
            const auto c = ps[i];
            if (isStress (c) && i < t.nucleusFirst) t.stressed = true;
            if (! isStress (c) && ! isBoundary (c) && ! isLength (c)) t.label << juce::String::charToString (c);
        }
        t.wordStart = k == 0;
        for (int i = t.firstToken - 1; i >= 0 && i >= (k > 0 ? out.back().nucleusLast : 0); --i)
            if (isBoundary (ps[i])) { t.wordStart = true; break; }
        for (int i = t.nucleusLast + 1; i <= t.lastToken; ++i)
        {
            const auto c = ps[i];
            if (c == '.' || c == '!' || c == '?' || c == 0x2026) t.pauseSteps = juce::jmax (t.pauseSteps, 2);
            else if (c == ',' || c == ';' || c == ':' || c == 0x2014) t.pauseSteps = juce::jmax (t.pauseSteps, 1);
        }
        out.push_back (t);
    }
    return out;
}

} // namespace vocalis
