// Motore v3: la voce nasce gia' a tempo (durate imposte a Kokoro), intonazione/canto/armonie via F0 neurale.
#include "Renderer.h"
#include "DSP/TimeStretch.h"
#include "DSP/WorldVoice.h"
#include "DSP/Loudness.h"
#include "DSP/EffectsChain.h"
#include "Engine/Phonemizer.h"
#include <cmath>

namespace vocalis
{

namespace
{
    constexpr double modelRate = 24000.0;
    constexpr int spf = KokoroEngine::samplesPerFrame;

    template <typename T>
    juce::String hashOf (const std::vector<T>& v)
    {
        // FNV-1a sui byte: chiave di cache compatta per durate/F0
        uint64_t h = 1469598103934665603ull;
        auto* p = reinterpret_cast<const unsigned char*> (v.data());
        for (size_t i = 0; i < v.size() * sizeof (T); ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return juce::String::toHexString ((juce::int64) h) + ":" + juce::String ((int) v.size());
    }

    /** Una voce (principale, armonia, doppio...) gia' allineata, in campioni a 24 kHz dall'inizio della clip. */
    struct Track
    {
        std::vector<float> audio;
        void mixAt (int pos, const std::vector<float>& v, float gain)
        {
            if (pos < 0) return;
            if ((size_t) pos + v.size() > audio.size()) audio.resize ((size_t) pos + v.size(), 0.0f);
            for (size_t i = 0; i < v.size(); ++i) audio[(size_t) pos + i] += v[i] * gain;
        }
    };

    /** Informazioni per sillaba nella clip (campioni a 24 kHz). */
    struct SylInfo
    {
        int onset = 0, vowel = 0, end = 0, nextOnset = 0;
        double gridBeat = 0.0;
        int note = -1;
        float gainDb = 0.0f;
        bool muted = false, phraseStart = false;
        juce::String label, word;
        size_t chunk = 0;
        int localIndex = 0;      // indice nella sillabazione del blocco
        int wordIndex = 0;
    };

    /** Un blocco di fonemi (<= 480 token) con tutto quello che serve per le passate del modello. */
    struct Chunk
    {
        juce::String ps;
        std::vector<TokenSyllable> sylls;
        size_t firstSyl = 0;                 // indice globale della prima sillaba
        std::shared_ptr<KokoroChunkResult> natural;   // prima passata (durate predette)
        TimedDurations timing;               // durate imposte (Rap/Canto)
        std::vector<int64_t> durations;      // durate usate nelle passate successive
        std::vector<float> f0Natural, f0Final;
        std::vector<float> nFinal;           // energia sostenuta (Canto), vuota = naturale
        double offset = 0.0;                 // campione della clip dove inizia l'audio del modello
        std::vector<double> warpIn, warpOut; // micro-allineamento delle vocali (campioni del modello)
    };

    std::vector<float> warpChunk (const std::vector<float>& a, const Chunk& c)
    {
        if (c.warpIn.size() < 2) return a;
        auto in = c.warpIn, out = c.warpOut;
        in.push_back ((double) a.size());
        out.push_back (out.back() + ((double) a.size() - c.warpIn.back()));
        return TimeStretch::warp (a.data(), (int) a.size(), in, out, modelRate);
    }

    void panInto (juce::AudioBuffer<float>& st, const std::vector<float>& v, float gain, float pan, int delay = 0)
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = std::cos (a) * gain, gr = std::sin (a) * gain;
        const int n = juce::jmin ((int) v.size(), st.getNumSamples() - delay);
        for (int i = 0; i < n; ++i)
        {
            st.addSample (0, i + delay, v[(size_t) i] * gl);
            st.addSample (1, i + delay, v[(size_t) i] * gr);
        }
    }

    /** Respiro sintetico: rumore filtrato (500 Hz - 4 kHz) con inviluppo morbido. */
    std::vector<float> makeBreath (int len, uint32_t seed)
    {
        std::vector<float> b ((size_t) len);
        juce::Random rnd ((juce::int64) seed);
        float lp = 0.0f, hp = 0.0f, prev = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const float x = rnd.nextFloat() * 2.0f - 1.0f;
            lp += 0.55f * (x - lp);                 // ~4 kHz
            hp = 0.88f * (hp + lp - prev); prev = lp; // ~500 Hz
            const float t = (float) i / (float) len;
            const float env = t < 0.6f ? std::sin (t / 0.6f * juce::MathConstants<float>::halfPi)
                                       : std::cos ((t - 0.6f) / 0.4f * juce::MathConstants<float>::halfPi);
            b[(size_t) i] = hp * env * env;
        }
        return b;
    }
}

std::shared_ptr<KokoroChunkResult> Renderer::runCached (const juce::String& chunk, const VoiceMix& mix, float speed, int row,
                                                        const KokoroPass& pass, juce::String& error)
{
    const auto key = chunk + "|" + mix.key() + "|" + juce::String (speed, 3) + "|" + juce::String (row) + "|"
                   + (pass.useDur ? hashOf (pass.durations) : juce::String ("-")) + "|" + (pass.useF0 ? hashOf (pass.f0) : juce::String ("-"))
                   + "|" + (pass.useN ? hashOf (pass.n) : juce::String ("-"));
    ModelRunner run;
    {
        std::lock_guard<std::mutex> l (cacheMutex);
        if (auto it = passCache.find (key); it != passCache.end()) return it->second;
        run = runner;
    }
    auto res = std::make_shared<KokoroChunkResult> (run != nullptr ? run (chunk, mix, speed, row, pass, error)
                                                                     : kokoro.run (chunk, mix, speed, row, pass, error));
    if (error.isNotEmpty() || res->audio.empty())
    {
        if (error.isEmpty()) error = "Il motore neurale non ha prodotto audio";
        return nullptr;
    }
    std::lock_guard<std::mutex> l (cacheMutex);
    if (passCache.size() > 96) passCache.clear();
    passCache[key] = res;
    return res;
}

bool Renderer::renderTimed (const RenderRequest& r, RenderedClip& clip)
{
    const auto& v = r.voice;
    const double bpm = juce::jmax (20.0, r.rhythm.bpm);
    const double samplesPerBeat = 60.0 / bpm * modelRate;
    const bool timedMode = r.mode != VocalMode::Parlato;
    juce::String err;

    // ---- testo -> blocchi di fonemi -> sillabe
    const auto phonemes = Phonemizer::phonemize (r.text, v.language, v.dictionary);
    std::vector<Chunk> chunks;
    std::vector<TokenSyllable> all;
    for (auto c : Phonemizer::splitForModel (phonemes))
    {
        Chunk ch;
        ch.ps = Phonemizer::filterToVocab (c);
        ch.sylls = TokenSyllables::build (ch.ps);
        if (ch.ps.isEmpty() || ch.sylls.empty()) continue;
        ch.firstSyl = all.size();
        // l'ultima sillaba di un blocco chiude una frase
        ch.sylls.back().pauseSteps = juce::jmax (ch.sylls.back().pauseSteps, 2);
        all.insert (all.end(), ch.sylls.begin(), ch.sylls.end());
        chunks.push_back (std::move (ch));
    }
    if (chunks.empty())
    {
        clip.error = "Nessun fonema riconosciuto nel testo";
        return true;   // errore definitivo: la catena v2 non farebbe meglio
    }
    clip.phonemeText = phonemes;

    // ---- 1) passata naturale (durate e intonazione predette)
    for (auto& ch : chunks)
        if ((ch.natural = runCached (ch.ps, v.mix, v.speed, v.variant, {}, err)) == nullptr)
        {
            clip.warning = err;
            return false;
        }

    // ---- 2) dove cade ogni vocale
    auto targets = TimingPlanner::plan (all, r.rhythm, r.groove, r.edits, r.mode, r.notes);
    const double preroll = timedMode ? TimingPlanner::prerollBeats (chunks[0].natural->durations, chunks[0].sylls,
                                                                    std::vector<SyllableTarget> (targets.begin(), targets.begin() + (long) chunks[0].sylls.size()), bpm)
                                     : 0.0;
    const double startBeat = -preroll;

    double cursor = 0.0;   // Parlato: blocchi uno dopo l'altro con 120 ms di pausa
    for (auto& ch : chunks)
    {
        if (timedMode)
        {
            std::vector<SyllableTarget> t (targets.begin() + (long) ch.firstSyl, targets.begin() + (long) (ch.firstSyl + ch.sylls.size()));
            std::vector<SyllableEdit> e;
            for (size_t k = 0; k < ch.sylls.size(); ++k)
                e.push_back (ch.firstSyl + k < r.edits.size() ? r.edits[ch.firstSyl + k] : SyllableEdit {});
            ch.timing = TimingPlanner::durations (ch.ps, ch.natural->durations, ch.sylls, t, e, bpm, startBeat, r.mode);
            ch.durations = ch.timing.durations;
            ch.offset = ch.timing.padSamples;
            // vocali realizzate (frame interi) -> posizione esatta (punti strettamente crescenti)
            ch.warpIn = { 0.0 }; ch.warpOut = { 0.0 };
            for (size_t k = 0; k < ch.sylls.size(); ++k)
            {
                const double got = ch.timing.vowelFrame[k] * (double) spf, want = ch.timing.targetFrame[k] * spf;
                if (got > ch.warpIn.back() + 1.0 && want > ch.warpOut.back() + 1.0)
                {
                    ch.warpIn.push_back (got);
                    ch.warpOut.push_back (want);
                }
            }
            if (ch.warpIn.size() < 2) { ch.warpIn.clear(); ch.warpOut.clear(); }
        }
        else
        {
            ch.durations = ch.natural->durations;
            ch.offset = cursor;
            cursor += (double) ch.natural->audio.size() + 0.12 * modelRate;
        }
    }

    // ---- 3) passata a tempo (o naturale nel Parlato): audio + F0 naturale con queste durate
    auto pitchSettings = PitchSettings {};
    pitchSettings.mode = r.mode;
    pitchSettings.globalSemis = v.pitchSemis;
    pitchSettings.intonation = v.intonation;
    pitchSettings.expressivity = r.expressivity;
    pitchSettings.vibratoCents = r.vibratoCents;
    pitchSettings.glideMs = r.glideMs;

    std::vector<int> notesAll (all.size(), -1);
    for (size_t k = 0; k < targets.size(); ++k) notesAll[k] = targets[k].noteMidi;

    std::vector<std::shared_ptr<KokoroChunkResult>> lead (chunks.size());
    bool anyAdlib = false;
    for (auto& e : r.edits) anyAdlib |= e.adlib;

    for (size_t c = 0; c < chunks.size(); ++c)
    {
        auto& ch = chunks[c];
        KokoroPass p;
        p.useDur = true; p.durations = ch.durations;
        auto timed = timedMode ? runCached (ch.ps, v.mix, v.speed, v.variant, p, err) : ch.natural;
        if (timed == nullptr) { clip.warning = err; return false; }
        ch.f0Natural = timed->f0;
        if (r.mode == VocalMode::Canto && ! timed->n.empty())
            ch.nFinal = PitchPlanner::sustainEnergy (timed->n, ch.durations, ch.sylls, r.mode);

        std::vector<int> notes (notesAll.begin() + (long) ch.firstSyl, notesAll.begin() + (long) (ch.firstSyl + ch.sylls.size()));
        std::vector<SyllableEdit> e;
        for (size_t k = 0; k < ch.sylls.size(); ++k) e.push_back (ch.firstSyl + k < r.edits.size() ? r.edits[ch.firstSyl + k] : SyllableEdit {});

        if (PitchPlanner::isIdentity (notes, e, pitchSettings) || ch.f0Natural.empty())
        {
            ch.f0Final = ch.f0Natural;
            lead[c] = timed;
        }
        else
        {
            ch.f0Final = PitchPlanner::build (ch.f0Natural, ch.durations, ch.sylls, notes, e, pitchSettings);
            KokoroPass p3 = p; p3.useF0 = true; p3.f0 = ch.f0Final;
            if (! ch.nFinal.empty()) { p3.useN = true; p3.n = ch.nFinal; }
            if ((lead[c] = runCached (ch.ps, v.mix, v.speed, v.variant, p3, err)) == nullptr) { clip.warning = err; return false; }
        }
    }

    // ---- 4) informazioni per sillaba nella clip
    std::vector<SylInfo> info (all.size());
    {
        int w = -1;
        for (size_t g = 0; g < all.size(); ++g) { if (all[g].wordStart || w < 0) ++w; info[g].wordIndex = w; }
    }
    for (size_t c = 0; c < chunks.size(); ++c)
    {
        const auto& ch = chunks[c];
        std::vector<int> cum (ch.durations.size() + 1, 0);
        for (size_t i = 0; i < ch.durations.size(); ++i) cum[i + 1] = cum[i] + (int) ch.durations[i];
        auto tokenSample = [&] (int token) { return cum[(size_t) token + 1] * spf; };
        // posizione nella clip di un campione del modello, dopo il micro-allineamento
        auto mapped = [&] (double s)
        {
            if (ch.warpIn.size() < 2) return ch.offset + s;
            size_t j = 1;
            while (j + 1 < ch.warpIn.size() && ch.warpIn[j] < s) ++j;
            const double i0 = ch.warpIn[j - 1], i1 = ch.warpIn[j], o0 = ch.warpOut[j - 1], o1 = ch.warpOut[j];
            return ch.offset + (s <= i1 || i1 <= i0 ? o0 + (s - i0) * (o1 - o0) / juce::jmax (1.0, i1 - i0) : o1 + (s - i1));
        };

        // parola di ogni sillaba: testo dei fonemi tra due spazi
        for (size_t k = 0; k < ch.sylls.size(); ++k)
        {
            const auto& s = ch.sylls[k];
            auto& in = info[ch.firstSyl + k];
            in.chunk = c; in.localIndex = (int) k;
            in.onset = (int) std::lround (mapped (tokenSample (s.firstToken) - 0.0));
            in.vowel = (int) std::lround (mapped (tokenSample (s.nucleusFirst)));
            const int endTok = k + 1 < ch.sylls.size() ? ch.sylls[k + 1].firstToken : ch.ps.length();
            in.nextOnset = (int) std::lround (mapped (tokenSample (endTok)));
            in.end = timedMode ? (int) std::lround (mapped (ch.timing.endFrame[k] * (double) spf)) : in.nextOnset;
            in.end = juce::jlimit (in.vowel, juce::jmax (in.vowel, in.nextOnset), in.end);
            in.label = s.label;
            const size_t g = ch.firstSyl + k;
            in.gridBeat = g < targets.size() ? targets[g].gridBeat : (in.vowel - preroll * samplesPerBeat) / samplesPerBeat;
            in.gainDb = g < targets.size() ? targets[g].gainDb : (g < r.edits.size() ? r.edits[g].gainDb : 0.0f);
            in.muted = g < targets.size() ? targets[g].muted : (g < r.edits.size() && r.edits[g].muted);
            in.note = notesAll[g] >= 0 ? notesAll[g]
                                       : (int) std::lround (PitchPlanner::medianMidi (ch.f0Final, 2 * cum[(size_t) s.nucleusFirst + 1], 2 * cum[(size_t) endTok + 1]));
            in.phraseStart = g == 0 || (g > 0 && all[g - 1].pauseSteps >= 2);

            int a = s.firstToken; while (a > 0 && ch.ps[a - 1] != ' ') --a;
            int b = s.lastToken;  while (b + 1 < ch.ps.length() && ch.ps[b + 1] != ' ') ++b;
            in.word = ch.ps.substring (a, b + 1).removeCharacters (juce::String (juce::CharPointer_UTF8 ("ˈˌ;:,.!?—…\"()“”«» ")));
        }
    }

    // ---- 5) tracce: voce principale, armonie, doppio, ad-lib (stessi tempi, F0 diverse)
    auto renderVoice = [&] (std::function<std::vector<float> (const Chunk&, size_t c)> f0For, int rowExtra, bool& ok)
    {
        Track t;
        for (size_t c = 0; c < chunks.size(); ++c)
        {
            const auto& ch = chunks[c];
            KokoroPass p; p.useDur = true; p.durations = ch.durations;
            p.f0 = f0For (ch, c); p.useF0 = ! p.f0.empty();
            if (! ch.nFinal.empty()) { p.useN = true; p.n = ch.nFinal; }
            auto res = runCached (ch.ps, v.mix, v.speed, v.variant + rowExtra, p, err);
            if (res == nullptr) { ok = false; return t; }
            t.mixAt ((int) std::lround (ch.offset), warpChunk (res->audio, ch), 1.0f);
        }
        ok = true;
        return t;
    };

    Track leadTrack;
    for (size_t c = 0; c < chunks.size(); ++c)
        leadTrack.mixAt ((int) std::lround (chunks[c].offset), warpChunk (lead[c]->audio, chunks[c]), 1.0f);

    // formanti e robot (WORLD / STFT) su ogni voce mono, prima del mix
    auto colour = [&] (std::vector<float>& a)
    {
        if (a.empty()) return;
        juce::AudioBuffer<float> b (1, (int) a.size());
        b.copyFrom (0, 0, a.data(), (int) a.size());
        if (std::abs (v.formantSemis) > 0.01f)
        {
            WorldTransform wt; wt.formantSemis = v.formantSemis;
            WorldVoice::process (b, modelRate, wt);
        }
        robotize (b, modelRate, v.robotHz, v.robotize);
        std::memcpy (a.data(), b.getReadPointer (0), a.size() * sizeof (float));
    };
    colour (leadTrack.audio);

    const auto& h = r.harmony;
    std::vector<std::pair<Track, float>> extra;   // traccia + pan
    std::vector<float> extraGain;
    auto harmonyF0 = [&] (int degrees)
    {
        return [&, degrees] (const Chunk& ch, size_t) {
            std::vector<float> f = ch.f0Final.empty() ? ch.f0Natural : ch.f0Final;
            std::vector<int> cum (ch.durations.size() + 1, 0);
            for (size_t i = 0; i < ch.durations.size(); ++i) cum[i + 1] = cum[i] + (int) ch.durations[i];
            for (size_t k = 0; k < ch.sylls.size(); ++k)
            {
                const auto& in = info[ch.firstSyl + k];
                const int semis = degrees == -7 || degrees == 7 ? (degrees > 0 ? 12 : -12)
                                                                 : PitchPlanner::diatonicShift (in.note > 0 ? in.note : 60, degrees, h.key, h.minor);
                const int a = k == 0 ? 0 : 2 * cum[(size_t) ch.sylls[k].firstToken + 1];
                const int b = k + 1 < ch.sylls.size() ? 2 * cum[(size_t) ch.sylls[k + 1].firstToken + 1] : (int) f.size();
                const float ratio = std::pow (2.0f, semis / 12.0f);
                for (int i = juce::jmax (0, a); i < juce::jmin ((int) f.size(), b); ++i) f[(size_t) i] *= ratio;
            }
            return f;
        };
    };

    bool ok = true;
    const int hcount = (h.degrees1 != 0 ? 1 : 0) + (h.degrees2 != 0 ? 1 : 0);
    if (h.degrees1 != 0)
    {
        auto t = renderVoice (harmonyF0 (h.degrees1), 3, ok);
        if (! ok) { clip.warning = err; return false; }
        colour (t.audio);
        extra.push_back ({ std::move (t), hcount == 2 ? -h.width : -0.5f * h.width });
        extraGain.push_back (h.level);
    }
    if (h.degrees2 != 0)
    {
        auto t = renderVoice (harmonyF0 (h.degrees2), 6, ok);
        if (! ok) { clip.warning = err; return false; }
        colour (t.audio);
        extra.push_back ({ std::move (t), hcount == 2 ? h.width : 0.5f * h.width });
        extraGain.push_back (h.level);
    }
    if (h.doubleOn)
    {
        auto detune = [&] (const Chunk& ch, size_t) {
            std::vector<float> f = ch.f0Final.empty() ? ch.f0Natural : ch.f0Final;
            for (auto& x : f) x *= 1.007f;   // +12 cent
            return f;
        };
        auto t = renderVoice (detune, 9, ok);
        if (! ok) { clip.warning = err; return false; }
        colour (t.audio);
        extra.push_back ({ std::move (t), 2.0f });   // 2 = doppio (Haas sui due lati)
        extraGain.push_back (h.doubleLevel);
    }

    Track adlib;
    if (anyAdlib)
    {
        auto octave = [&] (const Chunk& ch, size_t) {
            std::vector<float> f = ch.f0Final.empty() ? ch.f0Natural : ch.f0Final;
            for (auto& x : f) x *= 2.0f;
            return f;
        };
        adlib = renderVoice (octave, 12, ok);
        if (! ok) { clip.warning = err; return false; }
        colour (adlib.audio);
    }

    // ---- 6) mix stereo a 24 kHz
    size_t total = leadTrack.audio.size();
    for (auto& e : extra) total = juce::jmax (total, e.first.audio.size());
    const int tail = (int) (0.6 * samplesPerBeat);
    juce::AudioBuffer<float> st (2, (int) total + tail);
    st.clear();
    panInto (st, leadTrack.audio, 1.0f, 0.0f);
    for (size_t i = 0; i < extra.size(); ++i)
    {
        auto& [t, pan] = extra[i];
        if (pan > 1.5f)
        {
            panInto (st, t.audio, extraGain[i] * 0.7f, -0.8f, (int) (0.012 * modelRate));
            panInto (st, t.audio, extraGain[i] * 0.7f, 0.8f, (int) (0.021 * modelRate));
        }
        else
            panInto (st, t.audio, extraGain[i], pan);
    }

    // inviluppo: silenzio, volume, legato (la voce si chiude prima della sillaba dopo)
    std::vector<float> env ((size_t) st.getNumSamples(), 1.0f);
    const int ramp = (int) (0.006 * modelRate), release = (int) (0.02 * modelRate);
    for (size_t k = 0; k < info.size(); ++k)
    {
        const auto& in = info[k];
        const float g = in.muted ? 0.0f : juce::Decibels::decibelsToGain (in.gainDb);
        const int a = juce::jmax (0, in.onset), b = juce::jmin ((int) env.size(), juce::jmax (a, in.nextOnset));
        if (std::abs (g - 1.0f) > 1.0e-4f)
            for (int i = a; i < b; ++i)
            {
                const float t = juce::jmin (1.0f, (float) (i - a) / ramp, (float) (b - i) / ramp);
                env[(size_t) i] *= 1.0f + (g - 1.0f) * juce::jmax (0.0f, t);
            }
        if (timedMode && in.end + release < in.nextOnset)
            for (int i = in.end; i < b; ++i)
                env[(size_t) i] *= juce::jmax (0.0f, 1.0f - (float) (i - in.end) / release);
    }
    for (int c = 0; c < 2; ++c)
    {
        auto* d = st.getWritePointer (c);
        for (int i = 0; i < st.getNumSamples(); ++i) d[i] *= env[(size_t) i];
    }

    // ad-lib: la sillaba un'ottava sopra, ritardata di 1/8 e aperta di lato (alternando)
    int side = 0;
    for (size_t k = 0; k < info.size() && ! adlib.audio.empty(); ++k)
    {
        if (k >= r.edits.size() || ! r.edits[k].adlib) continue;
        const auto& in = info[k];
        const int a = juce::jmax (0, in.onset), b = juce::jmin ((int) adlib.audio.size(), in.end + (int) (0.08 * modelRate));
        if (b <= a) continue;
        std::vector<float> piece (adlib.audio.begin() + a, adlib.audio.begin() + b);
        for (size_t i = 0; i < piece.size(); ++i)
            piece[i] *= juce::jmin (1.0f, (float) i / ramp, (float) (piece.size() - i) / (4.0f * ramp));
        const int delay = (int) (0.5 * samplesPerBeat);
        if (a + delay + (int) piece.size() > st.getNumSamples()) st.setSize (2, a + delay + (int) piece.size(), true, true);
        juce::AudioBuffer<float> tmp (2, st.getNumSamples() - a - delay); tmp.clear();
        panInto (tmp, piece, 0.55f, (side++ % 2 == 0) ? -0.7f : 0.7f);
        for (int c = 0; c < 2; ++c) st.addFrom (c, a + delay, tmp, c, 0, tmp.getNumSamples());
    }

    // respiri prima delle frasi, solo dove c'e' silenzio
    if (r.breath > 0.001f)
    {
        float rms = 0.0f;
        for (auto x : leadTrack.audio) rms += x * x;
        rms = std::sqrt (rms / (float) juce::jmax ((size_t) 1, leadTrack.audio.size()));
        for (size_t k = 0; k < info.size(); ++k)
        {
            if (! info[k].phraseStart) continue;
            const int endAt = info[k].onset - (int) (0.04 * modelRate);
            const int len = (int) (0.28 * modelRate);
            const int from = juce::jmax (k == 0 ? 0 : info[k - 1].end + (int) (0.03 * modelRate), endAt - len);
            if (endAt - from < (int) (0.1 * modelRate)) continue;
            auto b = makeBreath (endAt - from, (uint32_t) k * 7919u + 17u);
            panInto (st, b, rms * 2.2f * r.breath, 0.0f, from);
        }
    }

    // ---- 7) ricampionamento al sample rate della DAW
    const double ratio = modelRate / r.targetSampleRate;
    const double beatsTotal = st.getNumSamples() / samplesPerBeat - preroll;
    const double bar = juce::jmax (1, r.rhythm.beatsPerBar);
    clip.lengthBeats = juce::jmax (bar, std::ceil (beatsTotal / bar - 1.0e-6) * bar);
    clip.prerollBeats = preroll;
    const int outLen = juce::jmax ((int) std::ceil ((clip.lengthBeats + preroll) * 60.0 / bpm * r.targetSampleRate),
                                   (int) std::ceil (st.getNumSamples() / ratio));
    clip.audio.setSize (2, outLen);
    clip.audio.clear();
    for (int c = 0; c < 2; ++c)
    {
        std::vector<float> padded ((size_t) st.getNumSamples() + 64, 0.0f);
        std::memcpy (padded.data(), st.getReadPointer (c), (size_t) st.getNumSamples() * sizeof (float));
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, padded.data(), clip.audio.getWritePointer (c), juce::jmin (outLen, (int) std::floor (st.getNumSamples() / ratio)));
    }

    // ---- 8) marcatori, F0 per la grafica, livello
    const double toOut = r.targetSampleRate / modelRate;
    for (size_t k = 0; k < info.size(); ++k)
    {
        const auto& in = info[k];
        ClipMarker m;
        m.start = (int) std::lround (in.onset * toOut);
        m.nucleus = (int) std::lround (in.vowel * toOut);
        m.end = (int) std::lround (juce::jmax (in.end, in.vowel + 1) * toOut);
        m.label = in.label;
        m.syllableIndex = (int) k;
        m.gridBeat = in.gridBeat;
        m.muted = in.muted;
        m.noteMidi = in.note;
        clip.markers.push_back (m);
        clip.syllableLabels.add (in.label);
        clip.syllableWords.add (in.word);
        clip.syllableWordIndex.push_back (in.wordIndex);
    }

    clip.f0Display.assign ((size_t) std::ceil (st.getNumSamples() / (modelRate * clip.f0Hop)) + 1, 0.0f);
    for (const auto& ch : chunks)
    {
        const auto& f = ch.f0Final.empty() ? ch.f0Natural : ch.f0Final;
        for (size_t i = 0; i < f.size(); ++i)
        {
            const size_t idx = (size_t) std::lround ((ch.offset + (double) i * spf / 2.0) / (modelRate * clip.f0Hop));
            if (idx < clip.f0Display.size()) clip.f0Display[idx] = f[i];
        }
    }

    if (r.loudnessLufs < -0.5)
        Loudness::normalise (clip.audio, r.targetSampleRate, r.loudnessLufs, -1.0f);
    else
    {
        const float peak = juce::jmax (clip.audio.getMagnitude (0, 0, outLen), clip.audio.getMagnitude (1, 0, outLen));
        if (peak > 1.0e-4f) clip.audio.applyGain (juce::Decibels::decibelsToGain (-3.0f) / peak);
    }
    if (! clip.markers.empty())
        clip.triggerStart = juce::jmax (0, clip.markers.front().start - (int) (0.01 * r.targetSampleRate));
    clip.engineName = "Kokoro v3";
    clip.timed = true;
    return true;
}

} // namespace vocalis
