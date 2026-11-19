#include "KokoroEngine.h"
#include "KokoroAligner.h"
#include "Phonemizer.h"
#include "Syllabifier.h"
#include <mutex>

#if VOCALIS_HAS_ORT
 #include <onnxruntime_cxx_api.h>
#endif

namespace vocalis
{
#if VOCALIS_HAS_ORT
/** Carica la libreria ONNX Runtime una sola volta e inizializza l'API C++ (ORT_API_MANUAL_INIT). */
static bool loadOrt (juce::String& error)
{
    static std::mutex m;
    static bool done = false, ok = false;
    static juce::String err;
    // mai chiusa: gli oggetti ORT non devono essere distrutti dopo la libreria all'uscita del processo
    static auto* libPtr = new juce::DynamicLibrary();
    auto& lib = *libPtr;

    std::lock_guard<std::mutex> l (m);
    if (done) { error = err; return ok; }
    done = true;

    juce::Array<juce::File> candidates;
    if (auto env = juce::SystemStats::getEnvironmentVariable ("VOCALIS_ORT_LIB", {}); env.isNotEmpty())
        candidates.add (juce::File (env));

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { exe.getParentDirectory().getParentDirectory().getChildFile ("Frameworks"),
                      exe.getParentDirectory(),
                      juce::File ("/Library/Application Support/Vocalis/lib") })
    {
       #if JUCE_MAC
        for (auto f : dir.findChildFiles (juce::File::findFiles, false, "libonnxruntime*.dylib"))
       #elif JUCE_WINDOWS
        for (auto f : dir.findChildFiles (juce::File::findFiles, false, "onnxruntime.dll"))
       #else
        for (auto f : dir.findChildFiles (juce::File::findFiles, false, "libonnxruntime.so*"))
       #endif
            if (! f.getFileName().contains ("providers"))
                candidates.add (f);
    }

    for (auto& f : candidates)
    {
        if (! lib.open (f.getFullPathName()))
            continue;
        using GetApiBase = const OrtApiBase* (ORT_API_CALL*) ();
        auto fn = (GetApiBase) lib.getFunction ("OrtGetApiBase");
        const OrtApi* api = fn != nullptr ? fn()->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            lib.close();
            continue;
        }
        Ort::InitApi (api);
        ok = true;
        return true;
    }

   #if JUCE_MAC && JUCE_INTEL
    err = "la voce neurale richiede un Mac con Apple Silicon";
   #else
    err = "libreria ONNX Runtime non trovata";
   #endif
    error = err;
    return false;
}
#endif
}

namespace vocalis
{

namespace
{
    const char* ctrlModelFileName = "kokoro-v1.0-ctrl.onnx";
    const char* modelFileName = "kokoro-v1.0.onnx";

    juce::File modelFileIn (const juce::File& dir)
    {
        auto c = dir.getChildFile (ctrlModelFileName);
        return c.existsAsFile() ? c : dir.getChildFile (modelFileName);
    }
    const char* voicesFileName = "voices-v1.0.bin";

#if VOCALIS_HAS_ORT
    /** Sessione ONNX condivisa da tutte le istanze del plugin (il modello pesa ~300 MB). */
    struct SharedModel
    {
        std::mutex lock;
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::Session> session;
        juce::File loadedFrom;
        bool controllable = false;
        KokoroVoices voices;
        juce::String error;

        bool ensureLoaded (const juce::File& dir)
        {
            std::lock_guard<std::mutex> l (lock);
            if (session != nullptr && loadedFrom == dir)
                return true;

            try
            {
                if (env == nullptr)
                    env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_ERROR, "vocalis");

                Ort::SessionOptions so;
                so.SetIntraOpNumThreads (juce::jlimit (1, 8, juce::SystemStats::getNumPhysicalCpus()));
                so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
                const auto path = modelFileIn (dir).getFullPathName();
               #if JUCE_WINDOWS
                session = std::make_unique<Ort::Session> (*env, path.toWideCharPointer(), so);
               #else
                session = std::make_unique<Ort::Session> (*env, path.toRawUTF8(), so);
               #endif
            }
            catch (const std::exception& e)
            {
                error = juce::String ("Impossibile caricare il modello neurale: ") + e.what();
                session.reset();
                return false;
            }

            juce::String verr;
            if (! voices.open (dir.getChildFile (voicesFileName), verr))
            {
                error = verr;
                session.reset();
                return false;
            }
            controllable = session->GetInputCount() >= 7 && session->GetOutputCount() >= 3;
            loadedFrom = dir;
            error.clear();
            return true;
        }
    };

    SharedModel& shared()
    {
        // allocata e mai distrutta: all'uscita della DAW le statiche interne di ONNX Runtime possono essere gia'
        // state finalizzate, e distruggere Env/Session dopo di loro manda in crash il processo
        static auto* m = new SharedModel();
        return *m;
    }
#endif
}

struct KokoroEngine::Impl {};

KokoroEngine::KokoroEngine() : impl (std::make_unique<Impl>()) {}
KokoroEngine::~KokoroEngine() = default;

juce::File KokoroEngine::findModelDir()
{
    juce::Array<juce::File> candidates;
    if (auto env = juce::SystemStats::getEnvironmentVariable ("VOCALIS_MODELS", {}); env.isNotEmpty())
        candidates.add (juce::File (env));

    const auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    candidates.add (userData.getChildFile ("Application Support/Vocalis/models"));
    candidates.add (juce::File ("/Library/Application Support/Vocalis/models"));
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    candidates.add (exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/models"));
    candidates.add (exe.getParentDirectory().getChildFile ("Resources/models"));
    candidates.add (userData.getChildFile ("Vocalis/models"));

    for (auto& c : candidates)
        if (modelFileIn (c).existsAsFile() && c.getChildFile (voicesFileName).existsAsFile())
            return c;
    return {};
}

bool KokoroEngine::isAvailable (juce::String* reason)
{
   #if ! VOCALIS_HAS_ORT
    if (reason) *reason = "motore neurale non incluso in questa build";
    return false;
   #else
    if (findModelDir() == juce::File())
    {
        if (reason) *reason = "modello neurale non trovato (reinstalla Vocalis)";
        return false;
    }
    juce::String err;
    if (! loadOrt (err))
    {
        if (reason) *reason = err;
        return false;
    }
    return true;
   #endif
}

bool KokoroEngine::isControllable()
{
   #if ! VOCALIS_HAS_ORT
    return false;
   #else
    const auto dir = findModelDir();
    if (dir == juce::File() || ! modelFileIn (dir).getFileName().contains ("ctrl"))
        return false;
    juce::String err;
    if (! loadOrt (err) || ! shared().ensureLoaded (dir))
        return false;
    return shared().controllable;
   #endif
}

KokoroChunkResult KokoroEngine::run (const juce::String& chunk, const VoiceMix& mix, float speed, int styleRowOffset,
                                     const KokoroPass& pass, juce::String& error)
{
    KokoroChunkResult r;
#if ! VOCALIS_HAS_ORT
    juce::ignoreUnused (chunk, mix, speed, styleRowOffset, pass);
    error = "Motore neurale non incluso in questa build";
    return r;
#else
    const auto dir = findModelDir();
    if (dir == juce::File()) { error = "Modello neurale non trovato"; return r; }
    if (! loadOrt (error)) return r;
    auto& m = shared();
    if (! m.ensureLoaded (dir)) { error = m.error; return r; }
    if (! m.controllable && (pass.useDur || pass.useF0)) { error = "Il modello installato non accetta tempi imposti"; return r; }

    auto tokens = Phonemizer::tokenize (chunk);
    if (tokens.empty()) { error = "Nessun fonema"; return r; }

    std::vector<float> style;
    if (! m.voices.styleFor (mix, (int) tokens.size(), styleRowOffset, style, error))
        return r;

    std::vector<int64_t> ids;
    ids.reserve (tokens.size() + 2);
    ids.push_back (0);
    ids.insert (ids.end(), tokens.begin(), tokens.end());
    ids.push_back (0);

    if (pass.useDur && pass.durations.size() != ids.size())
    {
        error = "Durate imposte di lunghezza errata";
        return r;
    }
    int64_t totalDur = 0;
    for (auto d : pass.durations) totalDur += d;
    if (pass.useF0 && (! pass.useDur || (int64_t) pass.f0.size() != 2 * totalDur))
    {
        error = "Curva F0 di lunghezza errata";
        return r;
    }

    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        float spd = juce::jlimit (0.5f, 2.0f, speed);
        int64_t noDur = 0; float noF0 = 0.0f;
        bool useDur = pass.useDur, useF0 = pass.useF0;

        const int64_t idShape[2] = { 1, (int64_t) ids.size() };
        const int64_t styleShape[2] = { 1, (int64_t) KokoroVoices::styleDim };
        const int64_t oneShape[1] = { 1 };
        const int64_t durShape[1] = { useDur ? (int64_t) pass.durations.size() : 1 };
        const int64_t f0Shape[2] = { 1, useF0 ? (int64_t) pass.f0.size() : 1 };

        std::vector<Ort::Value> in;
        in.push_back (Ort::Value::CreateTensor<int64_t> (mem, ids.data(), ids.size(), idShape, 2));
        in.push_back (Ort::Value::CreateTensor<float> (mem, style.data(), style.size(), styleShape, 2));
        in.push_back (Ort::Value::CreateTensor<float> (mem, &spd, 1, oneShape, 1));

        std::vector<int64_t> durCopy = pass.durations;
        std::vector<float> f0Copy = pass.f0;
        if (m.controllable)
        {
            in.push_back (useDur ? Ort::Value::CreateTensor<int64_t> (mem, durCopy.data(), durCopy.size(), durShape, 1)
                                 : Ort::Value::CreateTensor<int64_t> (mem, &noDur, 1, durShape, 1));
            in.push_back (Ort::Value::CreateTensor<bool> (mem, &useDur, 1, oneShape, 0));
            in.push_back (useF0 ? Ort::Value::CreateTensor<float> (mem, f0Copy.data(), f0Copy.size(), f0Shape, 2)
                                : Ort::Value::CreateTensor<float> (mem, &noF0, 1, f0Shape, 2));
            in.push_back (Ort::Value::CreateTensor<bool> (mem, &useF0, 1, oneShape, 0));
        }

        const char* inNames[] = { "input_ids", "style", "speed", "dur_override", "use_dur", "f0_override", "use_f0" };
        const char* outNames[] = { "waveform", "duration", "f0" };
        auto out = m.session->Run (Ort::RunOptions { nullptr }, inNames, in.data(), in.size(), outNames, m.controllable ? 3 : 2);

        const float* wav = out[0].GetTensorData<float>();
        r.audio.assign (wav, wav + out[0].GetTensorTypeAndShapeInfo().GetElementCount());
        const int64_t* dur = out[1].GetTensorData<int64_t>();
        r.durations.assign (dur, dur + out[1].GetTensorTypeAndShapeInfo().GetElementCount());
        if (useDur) r.durations = pass.durations;   // l'uscita "duration" e' sempre quella predetta
        if (m.controllable)
        {
            const float* f = out[2].GetTensorData<float>();
            r.f0.assign (f, f + out[2].GetTensorTypeAndShapeInfo().GetElementCount());
        }
    }
    catch (const std::exception& e)
    {
        error = juce::String ("Errore del motore neurale: ") + e.what();
        r = {};
    }
    return r;
#endif
}

SynthResult KokoroEngine::synthesize (const juce::String& text, const VoiceSettings& s)
{
    SynthResult result;
    result.sampleRate = sampleRate;
    result.engineName = "Kokoro";

#if ! VOCALIS_HAS_ORT
    juce::ignoreUnused (text, s);
    result.error = "Motore neurale non incluso in questa build";
    return result;
#else
    const auto dir = findModelDir();
    if (dir == juce::File())
    {
        result.error = "Modello neurale non trovato";
        return result;
    }
    if (juce::String err; ! loadOrt (err))
    {
        result.error = err;
        return result;
    }
    auto& m = shared();
    if (! m.ensureLoaded (dir))
    {
        result.error = m.error;
        return result;
    }

    const auto phonemes = Phonemizer::phonemize (text, s.language, s.dictionary);
    if (phonemes.isEmpty())
    {
        result.error = "Nessun fonema riconosciuto nel testo";
        return result;
    }
    result.phonemeText = phonemes;

    std::vector<float> audio;
    std::vector<Phoneme> allPhonemes;

    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const char* inNames[] = { "input_ids", "style", "speed" };
        const char* ctrlInNames[] = { "input_ids", "style", "speed", "dur_override", "use_dur", "f0_override", "use_f0" };
        const char* outNames[] = { "waveform", "duration" };

        for (auto chunk : Phonemizer::splitForModel (phonemes))
        {
            auto tokens = Phonemizer::tokenize (chunk);
            if (tokens.empty()) continue;

            std::vector<float> style;
            juce::String verr;
            if (! m.voices.styleFor (s.mix, (int) tokens.size(), s.variant, style, verr))
            {
                result.error = verr;
                return result;
            }

            std::vector<int64_t> ids;
            ids.reserve (tokens.size() + 2);
            ids.push_back (0);
            ids.insert (ids.end(), tokens.begin(), tokens.end());
            ids.push_back (0);
            float speed = juce::jlimit (0.5f, 2.0f, s.speed);

            const int64_t idShape[2] = { 1, (int64_t) ids.size() };
            const int64_t styleShape[2] = { 1, (int64_t) KokoroVoices::styleDim };
            const int64_t speedShape[1] = { 1 };

            std::vector<Ort::Value> inputs;
            inputs.push_back (Ort::Value::CreateTensor<int64_t> (mem, ids.data(), ids.size(), idShape, 2));
            inputs.push_back (Ort::Value::CreateTensor<float> (mem, style.data(), style.size(), styleShape, 2));
            inputs.push_back (Ort::Value::CreateTensor<float> (mem, &speed, 1, speedShape, 1));
            int64_t noDur = 0; float noF0 = 0.0f; bool off = false;
            const int64_t oneShape[1] = { 1 }, f0Shape[2] = { 1, 1 };
            if (m.controllable)
            {
                inputs.push_back (Ort::Value::CreateTensor<int64_t> (mem, &noDur, 1, oneShape, 1));
                inputs.push_back (Ort::Value::CreateTensor<bool> (mem, &off, 1, oneShape, 0));
                inputs.push_back (Ort::Value::CreateTensor<float> (mem, &noF0, 1, f0Shape, 2));
                inputs.push_back (Ort::Value::CreateTensor<bool> (mem, &off, 1, oneShape, 0));
            }

            auto outputs = m.session->Run (Ort::RunOptions { nullptr }, m.controllable ? ctrlInNames : inNames, inputs.data(),
                                           inputs.size(), outNames, 2);

            const float* wav = outputs[0].GetTensorData<float>();
            const size_t numWav = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
            const int64_t* dur = outputs[1].GetTensorData<int64_t>();
            const size_t numDur = outputs[1].GetTensorTypeAndShapeInfo().GetElementCount();
            std::vector<int64_t> durations (dur, dur + numDur);

            int64_t totalFrames = 0;
            for (auto d : durations) totalFrames += juce::jmax ((int64_t) 0, d);
            const double spf = totalFrames > 0 ? (double) numWav / (double) totalFrames : 600.0;

            // silenzio iniziale/finale del modello: ne teniamo al massimo 50 ms / 75 ms
            const int lead = (int) std::round (juce::jmax ((int64_t) 0, durations.front() - 2) * spf);
            const int tail = (int) std::round (juce::jmax ((int64_t) 0, durations.back() - 3) * spf);
            const int keepStart = juce::jlimit (0, (int) numWav, lead);
            const int keepEnd = juce::jlimit (keepStart, (int) numWav, (int) numWav - tail);

            const int offset = (int) audio.size() - keepStart;
            auto ph = KokoroAligner::align (chunk, durations, (int) numWav, offset);
            for (auto& p : ph)
            {
                p.start = juce::jlimit ((int) audio.size(), (int) audio.size() + keepEnd - keepStart, p.start);
                p.end = juce::jlimit (p.start, (int) audio.size() + keepEnd - keepStart, p.end);
            }
            allPhonemes.insert (allPhonemes.end(), ph.begin(), ph.end());
            audio.insert (audio.end(), wav + keepStart, wav + keepEnd);

            // breve pausa tra un blocco e l'altro (sono gia' fine frase)
            Phoneme gap; gap.name = "."; gap.isPause = true;
            gap.start = (int) audio.size();
            audio.insert (audio.end(), (size_t) (0.12 * sampleRate), 0.0f);
            gap.end = (int) audio.size();
            allPhonemes.push_back (gap);
        }
    }
    catch (const std::exception& e)
    {
        result.error = juce::String ("Errore del motore neurale: ") + e.what();
        return result;
    }

    if (audio.empty())
    {
        result.error = "Il motore neurale non ha prodotto audio";
        return result;
    }

    result.audio.setSize (1, (int) audio.size());
    result.audio.copyFrom (0, 0, audio.data(), (int) audio.size());
    result.phonemes = std::move (allPhonemes);
    result.syllables = Syllabifier::build (result.phonemes, result.audio, sampleRate);
    return result;
#endif
}

} // namespace vocalis
