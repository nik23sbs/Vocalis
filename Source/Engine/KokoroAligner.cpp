#include "KokoroAligner.h"
#include "Phonemizer.h"
#include "Syllabifier.h"

namespace vocalis
{

std::vector<Phoneme> KokoroAligner::align (const juce::String& phonemesIn, const std::vector<int64_t>& durations,
                                           int numSamples, int sampleOffset)
{
    std::vector<Phoneme> out;

    // solo i caratteri che il modello ha visto (allineati 1:1 ai token)
    juce::Array<juce::juce_wchar> chars;
    for (auto c : phonemesIn)
        if (Phonemizer::inVocab (c))
            chars.add (c);

    if (chars.isEmpty() || (int) durations.size() < chars.size() + 1)
        return out;

    int64_t totalFrames = 0;
    for (auto d : durations) totalFrames += juce::jmax ((int64_t) 0, d);
    if (totalFrames <= 0)
        return out;
    const double samplesPerFrame = (double) numSamples / (double) totalFrames;

    // inizio di ogni carattere (in campioni); durations[0] = silenzio iniziale
    std::vector<int> charStart ((size_t) chars.size() + 1);
    int64_t acc = juce::jmax ((int64_t) 0, durations[0]);
    for (int i = 0; i < chars.size(); ++i)
    {
        charStart[(size_t) i] = (int) std::round (acc * samplesPerFrame);
        acc += juce::jmax ((int64_t) 0, durations[(size_t) i + 1]);
    }
    charStart[(size_t) chars.size()] = (int) std::round (acc * samplesPerFrame);

    const auto isStress = [] (juce::juce_wchar c) { return c == 0x02C8 || c == 0x02CC; };
    const auto isModifier = [] (juce::juce_wchar c) { return c == 0x02D0 || c == 0x0303 || c == 0x02B0 || c == 0x02B2; };
    const auto isPunct = [] (juce::juce_wchar c) { return juce::String::fromUTF8 (";:,.!?\xE2\x80\x94\xE2\x80\xA6\"()\xE2\x80\x9C\xE2\x80\x9D").containsChar (c); };

    juce::String pendingStress;
    int pendingStart = -1;
    bool nextWordStart = true;

    for (int i = 0; i < chars.size(); ++i)
    {
        const auto c = chars[i];
        const int s0 = charStart[(size_t) i] + sampleOffset, s1 = charStart[(size_t) i + 1] + sampleOffset;

        if (isStress (c))
        {
            pendingStress << juce::String::charToString (c);
            if (pendingStart < 0) pendingStart = s0;
            continue;
        }
        if (isModifier (c))
        {
            if (! out.empty() && ! out.back().isPause)
            {
                out.back().name << juce::String::charToString (c);
                out.back().end = s1;
            }
            continue;
        }
        if (c == ' ')
        {
            // lo spazio e' una transizione: meta' alla parola precedente, meta' alla seguente
            const int mid = (s0 + s1) / 2;
            if (! out.empty() && ! out.back().isPause) out.back().end = mid;
            pendingStart = mid;
            nextWordStart = true;
            continue;
        }

        Phoneme p;
        p.start = pendingStart >= 0 ? juce::jmin (pendingStart, s0) : s0;
        p.end = s1;
        p.name = pendingStress + juce::String::charToString (c);
        p.isPause = isPunct (c);
        p.isVowel = ! p.isPause && Syllabifier::isVowelName (p.name);
        p.wordStart = nextWordStart && ! p.isPause;

        if (p.isPause)
        {
            nextWordStart = true;
            p.name = juce::String::charToString (c);
        }
        else
            nextWordStart = false;

        // contiguita': chiudi il fonema precedente dove inizia questo
        if (! out.empty() && out.back().end < p.start && ! out.back().isPause && ! p.isPause)
            out.back().end = p.start;

        out.push_back (p);
        pendingStress.clear();
        pendingStart = -1;
    }

    return out;
}

} // namespace vocalis
