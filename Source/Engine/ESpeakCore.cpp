#include "ESpeakCore.h"
#include <espeak-ng/speak_lib.h>

namespace vocalis
{

namespace
{
    bool g_initialised = false;
    int g_sampleRate = 22050;
    juce::String g_error;
}

std::mutex& ESpeakCore::mutex()
{
    static std::mutex m;
    return m;
}

int ESpeakCore::sampleRate() { return g_sampleRate; }

juce::File ESpeakCore::findDataParent()
{
    auto check = [] (const juce::File& f) { return f.getChildFile ("espeak-ng-data").getChildFile ("phontab").existsAsFile(); };

    // Binario del plugin: <bundle>/Contents/MacOS/Vocalis  oppure  <bundle>/Contents/x86_64-linux/Vocalis.so
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> candidates {
        exe.getParentDirectory().getParentDirectory().getChildFile ("Resources"),
        exe.getParentDirectory().getChildFile ("Resources"),
        exe.getParentDirectory(),
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Vocalis"),
        juce::File ("/Library/Application Support/Vocalis"),
        juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Vocalis"),   // Windows: C:\ProgramData\Vocalis
    };

    if (auto env = juce::SystemStats::getEnvironmentVariable ("VOCALIS_ESPEAK_DATA", {}); env.isNotEmpty())
        candidates.insert (0, juce::File (env));

    for (auto& c : candidates)
        if (check (c))
            return c;

    return candidates.getFirst();
}

bool ESpeakCore::ensureInitialised (juce::String& error)
{
    std::lock_guard<std::mutex> lock (mutex());
    if (g_initialised)
        return true;

    if (g_error.isNotEmpty())
    {
        error = g_error;
        return false;
    }

    const auto dataParent = findDataParent();
    if (! dataParent.getChildFile ("espeak-ng-data").getChildFile ("phontab").existsAsFile())
    {
        g_error = error = "Dati eSpeak non trovati (espeak-ng-data). Cercati in: " + dataParent.getFullPathName();
        return false;
    }

    const auto path = dataParent.getFullPathName().toStdString();
    const int sr = espeak_Initialize (AUDIO_OUTPUT_SYNCHRONOUS, 0, path.c_str(),
                                      espeakINITIALIZE_PHONEME_EVENTS | espeakINITIALIZE_PHONEME_IPA
                                          | espeakINITIALIZE_DONT_EXIT);
    if (sr <= 0)
    {
        g_error = error = "Inizializzazione eSpeak fallita";
        return false;
    }

    g_sampleRate = sr;
    g_initialised = true;
    return true;
}

juce::String ESpeakCore::textToIpa (const juce::String& text, const juce::String& language)
{
    juce::String err;
    if (! ensureInitialised (err))
        return {};

    std::lock_guard<std::mutex> lock (mutex());
    if (espeak_SetVoiceByName (language.toRawUTF8()) != EE_OK)
        espeak_SetVoiceByName ("it");

    const auto utf8 = text.toStdString();
    const void* ptr = utf8.c_str();

    // IPA (0x02) + legatura (bit 7) con il carattere U+0361 tra le lettere dello stesso fonema
    const int mode = 0x02 | (0x01 << 7) | (0x0361 << 8);

    juce::StringArray clauses;
    while (ptr != nullptr)
    {
        const char* ph = espeak_TextToPhonemes (&ptr, espeakCHARS_UTF8, mode);
        if (ph == nullptr)
            break;
        auto s = juce::String::fromUTF8 (ph).trim();
        if (s.isNotEmpty())
            clauses.add (s);
    }

    auto out = clauses.joinIntoString (" ").replace ("\n", " ");
    while (out.contains ("  "))
        out = out.replace ("  ", " ");
    return out.replace (juce::String::charToString (0x0361), "^");
}

} // namespace vocalis
