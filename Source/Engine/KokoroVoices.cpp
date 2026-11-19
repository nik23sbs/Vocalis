#include "KokoroVoices.h"

namespace vocalis
{

bool KokoroVoices::open (const juce::File& npzFile, juce::String& error)
{
    std::lock_guard<std::mutex> l (lock);
    if (zip != nullptr && file == npzFile)
        return true;

    cache.clear();
    zip = std::make_unique<juce::ZipFile> (npzFile);
    if (zip->getNumEntries() == 0)
    {
        zip.reset();
        error = "File voci non valido: " + npzFile.getFullPathName();
        return false;
    }
    file = npzFile;
    return true;
}

juce::StringArray KokoroVoices::voiceNames() const
{
    juce::StringArray names;
    if (zip != nullptr)
        for (int i = 0; i < zip->getNumEntries(); ++i)
            names.add (zip->getEntry (i)->filename.upToLastOccurrenceOf (".npy", false, false));
    return names;
}

const std::vector<float>* KokoroVoices::load (const juce::String& name, juce::String& error)
{
    if (auto it = cache.find (name); it != cache.end())
        return &it->second;

    if (zip == nullptr) { error = "Voci non caricate"; return nullptr; }

    const int idx = zip->getIndexOfFileName (name + ".npy");
    if (idx < 0) { error = "Voce non trovata: " + name; return nullptr; }

    std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (idx));
    if (in == nullptr) { error = "Impossibile leggere la voce " + name; return nullptr; }

    juce::MemoryBlock mb;
    in->readIntoMemoryBlock (mb);
    const auto* bytes = static_cast<const unsigned char*> (mb.getData());
    const size_t size = mb.getSize();

    // Formato .npy: \x93NUMPY, versione (1 byte major, 1 minor), lunghezza header (2 o 4 byte LE), header dict, dati
    if (size < 12 || bytes[0] != 0x93 || std::memcmp (bytes + 1, "NUMPY", 5) != 0)
    {
        error = "Formato voce non riconosciuto: " + name;
        return nullptr;
    }
    const int major = bytes[6];
    size_t headerLen, offset;
    if (major == 1) { headerLen = (size_t) bytes[8] | ((size_t) bytes[9] << 8); offset = 10; }
    else { headerLen = (size_t) bytes[8] | ((size_t) bytes[9] << 8) | ((size_t) bytes[10] << 16) | ((size_t) bytes[11] << 24); offset = 12; }

    const juce::String header (reinterpret_cast<const char*> (bytes + offset), headerLen);
    if (! header.contains ("<f4") || header.contains ("'fortran_order': True"))
    {
        error = "Tipo dati voce non supportato: " + header;
        return nullptr;
    }

    const size_t dataStart = offset + headerLen;
    const size_t count = (size - dataStart) / sizeof (float);
    if (count < (size_t) styleDim)
    {
        error = "Voce troppo corta: " + name;
        return nullptr;
    }

    std::vector<float> data (count);
    std::memcpy (data.data(), bytes + dataStart, count * sizeof (float)); // little endian (x86/ARM)
    auto [it, ok] = cache.emplace (name, std::move (data));
    juce::ignoreUnused (ok);
    return &it->second;
}

bool KokoroVoices::styleFor (const juce::String& voiceA, const juce::String& voiceB, float blend, int numTokens,
                             std::vector<float>& out, juce::String& error)
{
    std::lock_guard<std::mutex> l (lock);
    const auto* a = load (voiceA, error);
    if (a == nullptr) return false;

    const int rows = (int) (a->size() / styleDim);
    const int row = juce::jlimit (0, rows - 1, numTokens - 1);
    out.assign (a->begin() + row * styleDim, a->begin() + (row + 1) * styleDim);

    blend = juce::jlimit (0.0f, 1.0f, blend);
    if (voiceB.isNotEmpty() && voiceB != voiceA && blend > 0.001f)
    {
        const auto* b = load (voiceB, error);
        if (b == nullptr) return false;
        const int rowsB = (int) (b->size() / styleDim);
        const int rb = juce::jlimit (0, rowsB - 1, numTokens - 1);
        for (int i = 0; i < styleDim; ++i)
            out[(size_t) i] = out[(size_t) i] * (1.0f - blend) + (*b)[(size_t) (rb * styleDim + i)] * blend;
    }
    return true;
}

bool KokoroVoices::styleFor (const VoiceMix& mix, int numTokens, int rowOffset, std::vector<float>& out, juce::String& error)
{
    std::lock_guard<std::mutex> l (lock);
    float total = 0.0f;
    for (int k = 0; k < 4; ++k)
        if (mix.id[k].isNotEmpty() && mix.w[k] > 1.0e-4f) total += mix.w[k];
    if (total <= 0.0f)
    {
        error = "Nessuna voce selezionata";
        return false;
    }

    out.assign ((size_t) styleDim, 0.0f);
    for (int k = 0; k < 4; ++k)
    {
        if (mix.id[k].isEmpty() || mix.w[k] <= 1.0e-4f) continue;
        const auto* d = load (mix.id[k], error);
        if (d == nullptr) return false;
        const int rows = (int) (d->size() / styleDim);
        const int row = juce::jlimit (0, rows - 1, numTokens - 1 + rowOffset);
        const float w = mix.w[k] / total;
        for (int i = 0; i < styleDim; ++i)
            out[(size_t) i] += (*d)[(size_t) (row * styleDim + i)] * w;
    }
    return true;
}

const std::vector<KokoroVoices::VoiceInfo>& KokoroVoices::catalogue()
{
    static const std::vector<VoiceInfo> c {
        { "if_sara", "Sara (italiano)", "it", false },
        { "im_nicola", "Nicola (italiano)", "it", true },
        { "af_heart", "Heart (US)", "en", false }, { "af_bella", "Bella (US)", "en", false },
        { "af_nicole", "Nicole (US, sussurrata)", "en", false }, { "af_sarah", "Sarah (US)", "en", false },
        { "af_sky", "Sky (US)", "en", false }, { "af_nova", "Nova (US)", "en", false },
        { "af_aoede", "Aoede (US)", "en", false }, { "af_kore", "Kore (US)", "en", false },
        { "af_jessica", "Jessica (US)", "en", false }, { "af_river", "River (US)", "en", false },
        { "af_alloy", "Alloy (US)", "en", false },
        { "am_michael", "Michael (US)", "en", true }, { "am_fenrir", "Fenrir (US)", "en", true },
        { "am_puck", "Puck (US)", "en", true }, { "am_echo", "Echo (US)", "en", true },
        { "am_eric", "Eric (US)", "en", true }, { "am_liam", "Liam (US)", "en", true },
        { "am_onyx", "Onyx (US)", "en", true }, { "am_adam", "Adam (US)", "en", true },
        { "am_santa", "Santa (US)", "en", true },
        { "bf_emma", "Emma (UK)", "en", false }, { "bf_isabella", "Isabella (UK)", "en", false },
        { "bf_alice", "Alice (UK)", "en", false }, { "bf_lily", "Lily (UK)", "en", false },
        { "bm_george", "George (UK)", "en", true }, { "bm_fable", "Fable (UK)", "en", true },
        { "bm_lewis", "Lewis (UK)", "en", true }, { "bm_daniel", "Daniel (UK)", "en", true },
        { "ef_dora", "Dora (spagnolo)", "es", false }, { "em_alex", "Alex (spagnolo)", "es", true },
        { "em_santa", "Santa (spagnolo)", "es", true },
        { "ff_siwis", "Siwis (francese)", "fr", false },
        { "pf_dora", "Dora (portoghese)", "pt", false }, { "pm_alex", "Alex (portoghese)", "pt", true },
        { "pm_santa", "Santa (portoghese)", "pt", true },
        { "hf_alpha", "Alpha (hindi)", "hi", false }, { "hf_beta", "Beta (hindi)", "hi", false },
        { "hm_omega", "Omega (hindi)", "hi", true }, { "hm_psi", "Psi (hindi)", "hi", true },
        { "jf_alpha", "Alpha (giapponese)", "ja", false }, { "jm_kumo", "Kumo (giapponese)", "ja", true },
        { "zf_xiaoxiao", "Xiaoxiao (cinese)", "zh", false }, { "zm_yunxi", "Yunxi (cinese)", "zh", true },
    };
    return c;
}

} // namespace vocalis
