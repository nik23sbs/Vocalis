#pragma once
#include <juce_core/juce_core.h>
#include <mutex>

namespace vocalis
{

/** Stato condiviso di eSpeak-NG (la libreria ha stato globale).
    Serve sia per la fonetica del motore neurale sia per le voci robot. */
class ESpeakCore
{
public:
    /** Inizializza una volta per processo. Ritorna false e riempie error se mancano i dati. */
    static bool ensureInitialised (juce::String& error);
    static std::mutex& mutex();
    static int sampleRate();

    /** Testo -> IPA con stress e legature ('^' tra le lettere di un fonema), come phonemizer(tie='^').
        Chiamare con il mutex NON acquisito. */
    static juce::String textToIpa (const juce::String& text, const juce::String& language);

    static juce::File findDataParent();
};

} // namespace vocalis
