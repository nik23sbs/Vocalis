#include "Phonemizer.h"
#include "ESpeakCore.h"
#include <map>

namespace vocalis
{

namespace
{
    const std::map<juce::juce_wchar, int>& vocab()
    {
        static const std::map<juce::juce_wchar, int> v { {0x003B,1}, {0x003A,2}, {0x002C,3}, {0x002E,4}, {0x0021,5}, {0x003F,6}, {0x2014,9}, {0x2026,10}, {0x0022,11}, {0x0028,12}, {0x0029,13}, {0x201C,14}, {0x201D,15}, {0x0020,16}, {0x0303,17}, {0x02A3,18}, {0x02A5,19}, {0x02A6,20}, {0x02A8,21}, {0x1D5D,22}, {0xAB67,23}, {0x0041,24}, {0x0049,25}, {0x004F,31}, {0x0051,33}, {0x0053,35}, {0x0054,36}, {0x0057,39}, {0x0059,41}, {0x1D4A,42}, {0x0061,43}, {0x0062,44}, {0x0063,45}, {0x0064,46}, {0x0065,47}, {0x0066,48}, {0x0068,50}, {0x0069,51}, {0x006A,52}, {0x006B,53}, {0x006C,54}, {0x006D,55}, {0x006E,56}, {0x006F,57}, {0x0070,58}, {0x0071,59}, {0x0072,60}, {0x0073,61}, {0x0074,62}, {0x0075,63}, {0x0076,64}, {0x0077,65}, {0x0078,66}, {0x0079,67}, {0x007A,68}, {0x0251,69}, {0x0250,70}, {0x0252,71}, {0x00E6,72}, {0x03B2,75}, {0x0254,76}, {0x0255,77}, {0x00E7,78}, {0x0256,80}, {0x00F0,81}, {0x02A4,82}, {0x0259,83}, {0x025A,85}, {0x025B,86}, {0x025C,87}, {0x025F,90}, {0x0261,92}, {0x0265,99}, {0x0268,101}, {0x026A,102}, {0x029D,103}, {0x026F,110}, {0x0270,111}, {0x014B,112}, {0x0273,113}, {0x0272,114}, {0x0274,115}, {0x00F8,116}, {0x0278,118}, {0x03B8,119}, {0x0153,120}, {0x0279,123}, {0x027E,125}, {0x027B,126}, {0x0281,128}, {0x027D,129}, {0x0282,130}, {0x0283,131}, {0x0288,132}, {0x02A7,133}, {0x028A,135}, {0x028B,136}, {0x028C,138}, {0x0263,139}, {0x0264,140}, {0x03C7,142}, {0x028E,143}, {0x0292,147}, {0x0294,148}, {0x02C8,156}, {0x02CC,157}, {0x02D0,158}, {0x02B0,162}, {0x02B2,164}, {0x2193,169}, {0x2192,171}, {0x2197,172}, {0x2198,173}, {0x1D7B,177} };
        return v;
    }

    bool isPunct (juce::juce_wchar c)
    {
        return juce::String::fromUTF8 (";:,.!?\xE2\x80\x94\xE2\x80\xA6\"\xC2\xAB\xC2\xBB\xE2\x80\x9C\xE2\x80\x9D(){}[]\xC2\xA1\xC2\xBF").containsChar (c);
    }

    struct Segment { juce::String text; bool isIpa = false; };

    /** Applica il dizionario: le riscritture sostituiscono il testo, le voci /ipa/ diventano segmenti fonetici. */
    std::vector<Segment> applyDictionary (const juce::String& text, const juce::String& dictionary)
    {
        std::map<juce::String, juce::String> entries;
        for (auto line : juce::StringArray::fromLines (dictionary))
        {
            const int eq = line.indexOfChar ('=');
            if (eq <= 0) continue;
            auto key = line.substring (0, eq).trim().toLowerCase();
            auto val = line.substring (eq + 1).trim();
            if (key.isNotEmpty() && val.isNotEmpty())
                entries[key] = val;
        }

        std::vector<Segment> segs;
        if (entries.empty())
        {
            segs.push_back ({ text, false });
            return segs;
        }

        juce::String current, word;
        auto flushWord = [&]
        {
            if (word.isEmpty()) return;
            auto it = entries.find (word.toLowerCase());
            if (it == entries.end())
                current << word;
            else if (it->second.startsWithChar ('/') && it->second.endsWithChar ('/') && it->second.length() > 2)
            {
                if (current.isNotEmpty()) segs.push_back ({ current, false });
                current.clear();
                segs.push_back ({ it->second.substring (1, it->second.length() - 1), true });
            }
            else
                current << it->second;
            word.clear();
        };

        for (auto c : text)
        {
            if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '\'')
                word << juce::String::charToString (c);
            else
            {
                flushWord();
                current << juce::String::charToString (c);
            }
        }
        flushWord();
        if (current.isNotEmpty())
            segs.push_back ({ current, false });
        return segs;
    }

    /** Come phonemizer(preserve_punctuation=True): fonetizza i pezzi tra la punteggiatura e la reinserisce. */
    juce::String phonemizePlain (const juce::String& text, const juce::String& language)
    {
        juce::String out, chunk;
        auto flushChunk = [&]
        {
            if (chunk.trim().isNotEmpty())
            {
                auto ipa = ESpeakCore::textToIpa (chunk.trim(), language);
                if (ipa.isNotEmpty())
                {
                    if (out.isNotEmpty() && ! out.endsWithChar (' ')) out << " ";
                    out << ipa;
                }
            }
            chunk.clear();
        };

        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            if (isPunct (c))
            {
                flushChunk();
                out = out.trimEnd();
                out << juce::String::charToString (c);
                // la punteggiatura e' seguita da uno spazio (se poi arriva altro testo)
                const bool nextIsPunct = i + 1 < text.length() && isPunct (text[i + 1]);
                if (! nextIsPunct) out << " ";
            }
            else
                chunk << juce::String::charToString (c);
        }
        flushChunk();
        return out.trim();
    }
}

bool Phonemizer::inVocab (juce::juce_wchar c) { return vocab().count (c) > 0; }

juce::String Phonemizer::espeakToKokoro (const juce::String& ipaIn)
{
    // Regole di misaki.espeak.EspeakG2P (stesso ordine: ordinate per chiave)
    static const std::vector<std::pair<juce::String, juce::String>> e2m {
        { juce::String::fromUTF8 ("a^\xC9\xAA"), "I" },          // a^ɪ
        { juce::String::fromUTF8 ("a^\xCA\x8A"), "W" },          // a^ʊ
        { juce::String::fromUTF8 ("d^z"), juce::String::fromUTF8 ("\xCA\xA3") },        // ʣ
        { juce::String::fromUTF8 ("d^\xCA\x92"), juce::String::fromUTF8 ("\xCA\xA4") },// d^ʒ -> ʤ
        { juce::String::fromUTF8 ("e^\xC9\xAA"), "A" },          // e^ɪ
        { juce::String::fromUTF8 ("o^\xCA\x8A"), "O" },          // o^ʊ
        { juce::String::fromUTF8 ("s^s"), "S" },
        { juce::String::fromUTF8 ("t^s"), juce::String::fromUTF8 ("\xCA\xA6") },        // ʦ
        { juce::String::fromUTF8 ("t^\xCA\x83"), juce::String::fromUTF8 ("\xCA\xA7") },// t^ʃ -> ʧ
        { juce::String::fromUTF8 ("\xC9\x94^\xC9\xAA"), "Y" }, // ɔ^ɪ
        { juce::String::fromUTF8 ("\xC9\x99^\xCA\x8A"), "Q" }, // ə^ʊ
    };

    auto ps = ipaIn;
    for (auto& [from, to] : e2m)
        ps = ps.replace (from, to);
    ps = ps.removeCharacters ("^-");
    // flag di cambio lingua "(en)" -> rimossi (language_switch='remove-flags')
    while (true)
    {
        const int a = ps.indexOfChar ('(');
        if (a < 0) break;
        const int b = ps.indexOfChar (a, ')');
        if (b < 0) break;
        ps = ps.substring (0, a) + ps.substring (b + 1);
    }
    return ps;
}

juce::String Phonemizer::filterToVocab (const juce::String& phonemes)
{
    juce::String out;
    for (auto c : phonemes)
        if (inVocab (c))
            out << juce::String::charToString (c);
    return out.trim();
}

std::vector<int64_t> Phonemizer::tokenize (const juce::String& phonemes)
{
    std::vector<int64_t> t;
    for (auto c : phonemes)
    {
        auto it = vocab().find (c);
        if (it != vocab().end())
            t.push_back (it->second);
    }
    return t;
}

juce::String Phonemizer::phonemize (const juce::String& text, const juce::String& language, const juce::String& dictionary)
{
    auto prepared = text.replace (juce::String::fromUTF8 ("\xC2\xAB"), juce::String::fromUTF8 ("\xE2\x80\x9C"))
                        .replace (juce::String::fromUTF8 ("\xC2\xBB"), juce::String::fromUTF8 ("\xE2\x80\x9D"))
                        .replace ("\r", " ").replace ("\n", " ").replace ("\t", " ");

    juce::String result;
    for (auto& seg : applyDictionary (prepared, dictionary))
    {
        juce::String ps = seg.isIpa ? seg.text : espeakToKokoro (phonemizePlain (seg.text, language));
        ps = ps.trim();
        if (ps.isEmpty()) continue;
        if (result.isNotEmpty() && ! result.endsWithChar (' ')) result << " ";
        result << ps;
    }
    return filterToVocab (result);
}

juce::StringArray Phonemizer::splitForModel (const juce::String& phonemes, int maxTokens)
{
    juce::StringArray out;
    juce::String current;
    auto countTokens = [] (const juce::String& s) { int n = 0; for (auto c : s) if (inVocab (c)) ++n; return n; };

    // spezza dopo . ! ? ; : (fine frase), poi dopo la virgola, poi a una parola qualsiasi
    juce::StringArray sentences;
    {
        juce::String cur;
        for (auto c : phonemes)
        {
            cur << juce::String::charToString (c);
            if (juce::String (".!?;:").containsChar (c)) { sentences.add (cur.trim()); cur.clear(); }
        }
        if (cur.trim().isNotEmpty()) sentences.add (cur.trim());
    }

    for (auto s : sentences)
    {
        while (countTokens (s) > maxTokens)
        {
            // taglia all'ultima virgola o spazio prima del limite
            int cut = -1, n = 0;
            for (int i = 0; i < s.length(); ++i)
            {
                if (++n >= maxTokens) break;
                if (s[i] == ',' ) cut = i + 1;
                else if (s[i] == ' ' && cut < 0) cut = i;
            }
            if (cut <= 0) cut = maxTokens;
            if (current.isNotEmpty()) { out.add (current.trim()); current.clear(); }
            out.add (s.substring (0, cut).trim());
            s = s.substring (cut).trim();
        }
        if (countTokens (current) + countTokens (s) + 1 > maxTokens && current.isNotEmpty())
        {
            out.add (current.trim());
            current.clear();
        }
        if (current.isNotEmpty()) current << " ";
        current << s;
    }
    if (current.trim().isNotEmpty())
        out.add (current.trim());
    return out;
}

} // namespace vocalis
