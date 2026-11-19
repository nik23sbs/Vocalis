#pragma once
// Solo test: esegue il modello con Tests/tools/kokoro_server.py (quando ONNX Runtime C++ non e' disponibile).
#include "Render/Renderer.h"

inline vocalis::Renderer::ModelRunner makePyModelRunner (const juce::File& dir)
{
    using namespace vocalis;
    return [dir] (const juce::String& chunk, const VoiceMix& mix, float speed, int row, const KokoroPass& p, juce::String& err)
    {
        juce::StringArray l;
        l.add (chunk);
        juce::String m; for (int i = 0; i < 4; ++i) if (mix.id[i].isNotEmpty() && mix.w[i] > 1e-4f) m << mix.id[i] << ":" << mix.w[i] << ",";
        l.add (m); l.add (juce::String (row)); l.add (juce::String (speed));
        juce::String d (p.useDur ? "1" : "0"); for (auto x : p.durations) d << " " << (int) x; l.add (p.useDur ? d : juce::String ("0"));
        juce::String f (p.useF0 ? "1" : "0"); if (p.useF0) for (auto x : p.f0) f << " " << x; l.add (f);
        juce::String en (p.useN ? "1" : "0"); if (p.useN) for (auto x : p.n) en << " " << x; l.add (en);
        dir.getChildFile ("done.txt").deleteFile();
        dir.getChildFile ("req.tmp").replaceWithText (l.joinIntoString ("\n"), false, false, "\n");
        dir.getChildFile ("req.tmp").moveFileTo (dir.getChildFile ("req.txt"));
        KokoroChunkResult r;
        for (int w = 0; w < 120000 && ! dir.getChildFile ("done.txt").existsAsFile(); w += 2) juce::Thread::sleep (2);
        if (! dir.getChildFile ("done.txt").existsAsFile()) { err = "server python non risponde"; return r; }
        juce::MemoryBlock mb; dir.getChildFile ("out.bin").loadFileAsData (mb);
        r.audio.resize (mb.getSize() / sizeof (float));
        std::memcpy (r.audio.data(), mb.getData(), r.audio.size() * sizeof (float));
        auto lines = juce::StringArray::fromLines (dir.getChildFile ("out.txt").loadFileAsString());
        for (auto t : juce::StringArray::fromTokens (lines[0], " ", "")) r.durations.push_back (t.getLargeIntValue());
        for (auto t : juce::StringArray::fromTokens (lines[1], " ", "")) r.f0.push_back (t.getFloatValue());
        for (auto t : juce::StringArray::fromTokens (lines[2], " ", "")) r.n.push_back (t.getFloatValue());
        return r;
    };
}
