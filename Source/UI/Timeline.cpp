#include "Timeline.h"
#include "VocalisLookAndFeel.h"
#include "PluginProcessor.h"
#include "DSP/PitchPlanner.h"

namespace vocalis
{

juce::String Timeline::noteName (int midi)
{
    static const char* n[] = { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    if (midi < 0) return "-";
    return juce::String (n[midi % 12]) + juce::String (midi / 12 - 1);
}

Timeline::Timeline (VocalisProcessor& p) : proc (p)
{
    setTooltip ("Sillabe: trascina = sposta, bordo = durata, Shift = intonazione, rotella = volume, doppio clic = muta, "
                "tasto destro = menu. Note: trascina in verticale. Trascina qui un file MIDI per la melodia.");
    startTimerHz (30);
}

void Timeline::setClip (RenderedClip::Ptr c)
{
    clip = c;
    if (drag == Drag::None) { dragIndex = -1; dragInfo.clear(); }
    updatePitchRange();
    rebuildPeaks();
    repaint();
}

juce::Rectangle<float> Timeline::rulerArea() const { return getLocalBounds().toFloat().removeFromTop (24.0f); }
juce::Rectangle<float> Timeline::laneArea() const  { return getLocalBounds().toFloat().withTrimmedTop (24.0f).removeFromTop (46.0f).reduced (0, 6); }
juce::Rectangle<float> Timeline::rollArea() const  { return getLocalBounds().toFloat().withTrimmedTop (71.0f).reduced (0, 4); }

double Timeline::samplesPerBeat() const { return clip != nullptr ? 60.0 / clip->bpm * clip->sampleRate : 1.0; }

float Timeline::sampleToX (double s) const
{
    if (clip == nullptr || clip->audio.getNumSamples() == 0) return 0.0f;
    // si mostra dalla battuta di partenza (l'anticipo resta a sinistra, in un margine)
    const double total = (clip->lengthBeats + clip->prerollBeats) * samplesPerBeat();
    return (float) (s / juce::jmax (1.0, total) * getWidth());
}

float Timeline::midiToY (float m) const
{
    auto r = rollArea();
    return r.getBottom() - (m - lowMidi) / juce::jmax (1.0f, highMidi - lowMidi) * r.getHeight();
}

int Timeline::displayNote (int i) const
{
    const auto& m = clip->markers[(size_t) i];
    const auto e = proc.getEdit (m.syllableIndex);
    if (e.note >= 0) return e.note;
    return m.noteMidi > 0 ? m.noteMidi : -1;
}

void Timeline::updatePitchRange()
{
    // l'intervallo segue le note (la curva naturale ha picchi nelle consonanti che non contano)
    float lo = 1000.0f, hi = -1000.0f;
    if (clip != nullptr)
        for (auto& m : clip->markers) if (m.noteMidi > 0) { lo = juce::jmin (lo, (float) m.noteMidi); hi = juce::jmax (hi, (float) m.noteMidi); }
    if (lo > hi) { lo = 52.0f; hi = 68.0f; }
    const float mid = 0.5f * (lo + hi), span = juce::jmax (14.0f, hi - lo + 6.0f);
    lowMidi = std::floor (mid - span * 0.5f);
    highMidi = std::ceil (mid + span * 0.5f);
}

void Timeline::rebuildPeaks()
{
    peaks.clear();
    const int w = juce::jmax (1, getWidth());
    if (clip == nullptr || clip->audio.getNumSamples() == 0) return;
    const int n = clip->audio.getNumSamples();
    const float* d = clip->audio.getReadPointer (0);
    peaks.resize ((size_t) w);
    const double total = (clip->lengthBeats + clip->prerollBeats) * samplesPerBeat();
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) (x * total / w), b = juce::jmax (a + 1, (int) ((x + 1) * total / w));
        float lo = 0, hi = 0;
        for (int i = a; i < juce::jmin (b, n); ++i) { lo = juce::jmin (lo, d[i]); hi = juce::jmax (hi, d[i]); }
        peaks[(size_t) x] = { lo, hi };
    }
}

juce::Rectangle<float> Timeline::blockBounds (int i) const
{
    const auto& m = clip->markers[(size_t) i];
    auto lane = laneArea();
    float x0 = sampleToX (m.start), x1 = sampleToX (m.end);
    if (i == dragIndex)
    {
        if (drag == Drag::Move) { x0 += dragDx; x1 += dragDx; }
        if (drag == Drag::Resize) x1 = juce::jmax (x0 + 6.0f, x1 + dragDx);
    }
    return { x0, lane.getY(), juce::jmax (4.0f, x1 - x0 - 2.0f), lane.getHeight() };
}

int Timeline::markerAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    if (clip == nullptr || ! laneArea().expanded (0, 4).contains (p)) return -1;
    for (int i = (int) clip->markers.size() - 1; i >= 0; --i)
    {
        const auto b = blockBounds (i);
        if (b.contains (p))
        {
            onRightEdge = p.x > b.getRight() - juce::jmin (7.0f, b.getWidth() * 0.3f);
            return i;
        }
    }
    return -1;
}

int Timeline::noteAt (juce::Point<float> p) const
{
    if (clip == nullptr || ! rollArea().contains (p)) return -1;
    for (int i = 0; i < (int) clip->markers.size(); ++i)
    {
        const auto& m = clip->markers[(size_t) i];
        const int n = displayNote (i);
        if (n < 0) continue;
        const float y = midiToY ((float) n);
        if (p.x >= sampleToX (m.nucleus) - 2 && p.x <= sampleToX (m.end) + 2 && std::abs (p.y - y) < 9.0f)
            return i;
    }
    return -1;
}

void Timeline::timerCallback()
{
    const double pos = proc.getPlayPosition();
    if (pos != lastPos) { lastPos = pos; repaint(); }
}

void Timeline::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff11121a));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    if (clip == nullptr || clip->audio.getNumSamples() == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isRendering() ? "Generazione in corso..." : "Scrivi un testo per generare il vocal",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (juce::Path ([&] { juce::Path p; p.addRoundedRectangle (r.reduced (1.0f), 11.0f); return p; }()));

    const float w = (float) getWidth(), h = (float) getHeight();
    const double spb = samplesPerBeat();
    const double pre = clip->prerollBeats;
    const auto ruler = rulerArea(), lane = laneArea(), roll = rollArea();
    auto beatX = [&] (double beat) { return sampleToX ((beat + pre) * spb); };

    // righe dei semitoni (le Do piu' marcate, con nome)
    for (int m = (int) lowMidi; m <= (int) highMidi; ++m)
    {
        const float y = midiToY ((float) m);
        g.setColour (m % 12 == 0 ? juce::Colour (0xff20222e) : juce::Colour (0xff171823));
        g.drawHorizontalLine ((int) y, 0, w);
        if (m % 12 == 0)
        {
            g.setColour (Colours::textDim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (noteName (m), 6, (int) y - 12, 40, 11, juce::Justification::left);
        }
    }

    // griglia: step, quarti, battute (numerate dalla battuta di partenza)
    g.setColour (juce::Colour (0xff12131a));
    g.fillRect (ruler);
    const int startBar = (int) proc.apvts.getRawParameterValue (IDs::startBar)->load();
    const double step = juce::jmax (1.0 / 32.0, clip->stepBeats);
    for (double b = -std::floor (pre / step) * step; b <= clip->lengthBeats + 1e-9; b += step)
    {
        const float x = beatX (b);
        const bool isBeat = std::abs (b - std::round (b)) < 1e-6;
        const bool isBar = isBeat && ((int) std::round (b)) % 4 == 0;
        g.setColour (isBar ? juce::Colour (0xff343747) : isBeat ? juce::Colour (0xff22242e) : juce::Colour (0xff1a1c26));
        g.drawVerticalLine ((int) x, ruler.getBottom(), h);
        if (isBar && b >= 0)
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (startBar + (int) std::round (b) / 4), (int) x + 6, 5, 40, 14, juce::Justification::left);
            g.setColour (juce::Colour (0xff343747));
            g.drawVerticalLine ((int) x, 0, ruler.getBottom());
        }
    }
    if (pre > 0.0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (0.0f, 0.0f, beatX (0.0), h);
        g.setColour (Colours::textDim.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("anticipo", 4, 5, (int) beatX (0.0) - 6, 14, juce::Justification::right);
    }
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine ((int) ruler.getBottom(), 0, w);
    g.drawHorizontalLine ((int) lane.getBottom() + 5, 0, w);

    // forma d'onda (in basso nel piano roll)
    {
        const float base = roll.getBottom() - roll.getHeight() * 0.18f, amp = roll.getHeight() * 0.16f;
        juce::Path p;
        for (size_t x = 0; x < peaks.size(); ++x) { const float y = base - peaks[x].second * amp; if (x == 0) p.startNewSubPath (0, y); else p.lineTo ((float) x, y); }
        for (int x = (int) peaks.size() - 1; x >= 0; --x) p.lineTo ((float) x, base - peaks[(size_t) x].first * amp);
        p.closeSubPath();
        g.setColour (Colours::accent2.withAlpha (0.35f));
        g.fillPath (p);
    }

    // note (una per sillaba)
    const bool canto = (int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2;
    for (int i = 0; i < (int) clip->markers.size(); ++i)
    {
        const auto& m = clip->markers[(size_t) i];
        int n = displayNote (i);
        if (n < 0) continue;
        if (i == dragIndex && drag == Drag::Note) n = juce::jlimit (24, 96, dragStartEdit.note >= 0 ? dragStartEdit.note : n) - (int) std::round (dragDy / juce::jmax (2.0f, roll.getHeight() / (highMidi - lowMidi)));
        const auto e = proc.getEdit (m.syllableIndex);
        const float y = midiToY ((float) n);
        auto nb = juce::Rectangle<float> (sampleToX (m.nucleus), y - 6.0f, juce::jmax (6.0f, sampleToX (m.end) - sampleToX (m.nucleus) - 2.0f), 12.0f);
        const bool sel = m.syllableIndex == selected;
        const juce::Colour c = e.note >= 0 || std::abs (e.pitchSemis) > 0.05f ? Colours::warn : Colours::accent;
        g.setColour (c.withAlpha (canto ? (sel ? 1.0f : 0.85f) : 0.35f));
        g.fillRoundedRectangle (nb, 3.0f);
        if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (nb, 3.0f, 1.2f); }
    }

    // curva d'intonazione finale
    if (! clip->f0Display.empty())
    {
        // solo dove si sente la voce (dalla vocale alla fine della sillaba), spezzata sui salti bruschi
        juce::Path f0;
        bool penDown = false;
        float lastM = 0.0f;
        const double hopSamples = clip->f0Hop * clip->sampleRate;
        size_t mk = 0;
        for (size_t i = 0; i < clip->f0Display.size(); ++i)
        {
            const double s = (double) i * hopSamples;
            while (mk < clip->markers.size() && clip->markers[mk].end < s) ++mk;
            const bool inVoice = mk < clip->markers.size() && s >= clip->markers[mk].nucleus && s <= clip->markers[mk].end;
            const float hz = clip->f0Display[i];
            const float mm = hz > 40.0f ? PitchPlanner::hzToMidi (hz) : 0.0f;
            if (! inVoice || hz < 40.0f || (penDown && std::abs (mm - lastM) > 2.5f)) { penDown = false; if (! inVoice || hz < 40.0f) continue; }
            const float x = sampleToX (s), y = midiToY (mm);
            if (! penDown) { f0.startNewSubPath (x, y); penDown = true; } else f0.lineTo (x, y);
            lastM = mm;
        }
        g.setColour (Colours::pink);
        g.strokePath (f0, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // blocchi delle sillabe
    for (int i = 0; i < (int) clip->markers.size(); ++i)
    {
        const auto& m = clip->markers[(size_t) i];
        const auto e = proc.getEdit (m.syllableIndex);
        auto b = blockBounds (i);
        const bool edited = ! e.isDefault(), sel = m.syllableIndex == selected;
        juce::Colour c = (i % 2 ? Colours::accent2 : Colours::accent);
        if (edited) c = Colours::warn;
        if (m.muted) c = Colours::textDim;
        g.setColour (c.withAlpha ((i == hover || i == dragIndex || sel) ? 0.42f : 0.25f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (sel ? juce::Colours::white : c);
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, sel || edited ? 1.5f : 1.0f);

        const float nx = sampleToX (m.nucleus) + (i == dragIndex && drag == Drag::Move ? dragDx : 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawVerticalLine ((int) nx, b.getY() + 3, b.getBottom() - 3);

        g.setColour (m.muted ? Colours::textDim : Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        juce::String t = m.label;
        if (e.whisper) t << " ~";
        if (e.adlib) t << " +";
        g.drawText (t, b.reduced (b.getWidth() > 30 ? 6 : 1, 2), b.getWidth() > 30 ? juce::Justification::centredLeft : juce::Justification::centred, false);
    }

    if (dragInfo.isNotEmpty())
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (dragInfo, getLocalBounds().reduced (12, 6).removeFromBottom (14), juce::Justification::centredRight);
    }
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (canto ? "Note (canto) - curva d'intonazione - forma d'onda" : "Intonazione - forma d'onda",
                roll.reduced (10, 4).removeFromTop (14).toNearestInt(), juce::Justification::right);

    const double pos = proc.getPlayPosition();
    if (pos >= 0)
    {
        const float x = sampleToX (pos);
        g.setColour (juce::Colours::white);
        g.fillRect (x, 0.0f, 1.5f, h);
    }
}

void Timeline::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i != hover) { hover = i; repaint(); }
    const int n = noteAt (e.position);
    setMouseCursor (n >= 0 ? juce::MouseCursor::UpDownResizeCursor
                    : i < 0 ? juce::MouseCursor::NormalCursor
                    : edge ? juce::MouseCursor::LeftRightResizeCursor
                           : e.mods.isShiftDown() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void Timeline::mouseDown (const juce::MouseEvent& e)
{
    if (clip == nullptr) return;
    bool edge = false;
    int i = markerAt (e.position, edge);
    const int n = i < 0 ? noteAt (e.position) : -1;
    if (i < 0 && n < 0)
    {
        if (selected >= 0) { selected = -1; if (onSelect) onSelect (-1); repaint(); }
        return;
    }
    if (i < 0) i = n;
    const int syl = clip->markers[(size_t) i].syllableIndex;
    if (syl != selected) { selected = syl; if (onSelect) onSelect (syl); }

    if (e.mods.isPopupMenu()) { showMenu (i); return; }

    dragIndex = i;
    dragStartEdit = proc.getEdit (syl);
    if (n >= 0 && dragStartEdit.note < 0 && (int) proc.apvts.getRawParameterValue (IDs::mode)->load() == 2)
        dragStartEdit.note = displayNote (i);
    drag = n >= 0 ? Drag::Note : edge ? Drag::Resize : e.mods.isShiftDown() ? Drag::Pitch : Drag::Move;
    dragDx = dragDy = 0;
    repaint();
}

void Timeline::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || clip == nullptr) return;
    const double total = clip->lengthBeats + clip->prerollBeats;
    const double beatsPerPx = total / juce::jmax (1, getWidth());

    if (drag == Drag::Move)
    {
        double beats = e.getDistanceFromDragStartX() * beatsPerPx;
        if (! e.mods.isAltDown()) beats = std::round (beats * 8.0) / 8.0;
        dragDx = (float) (beats / beatsPerPx);
        dragInfo = "sposta " + juce::String (beats >= 0 ? "+" : "") + juce::String (beats * 4.0, 2) + " sedicesimi";
    }
    else if (drag == Drag::Resize)
    {
        dragDx = (float) e.getDistanceFromDragStartX();
        const auto b = blockBounds (dragIndex);
        const float orig = sampleToX (clip->markers[(size_t) dragIndex].end) - sampleToX (clip->markers[(size_t) dragIndex].start);
        dragInfo = "durata x" + juce::String (b.getWidth() / juce::jmax (1.0f, orig), 2);
    }
    else if (drag == Drag::Pitch)
    {
        dragDy = (float) e.getDistanceFromDragStartY();
        if (! e.mods.isAltDown()) dragDy = std::round (dragDy / 6.0f) * 6.0f;
        const float semis = dragStartEdit.pitchSemis - dragDy / 6.0f;
        dragInfo = "intonazione " + juce::String (semis >= 0 ? "+" : "") + juce::String (semis, 1) + " semitoni";
    }
    else if (drag == Drag::Note)
    {
        dragDy = (float) e.getDistanceFromDragStartY();
        const float px = rollArea().getHeight() / juce::jmax (1.0f, highMidi - lowMidi);
        const int steps = (int) std::round (-dragDy / juce::jmax (2.0f, px));
        if (dragStartEdit.note >= 0)
            dragInfo = "nota " + noteName (juce::jlimit (24, 96, dragStartEdit.note + steps));
        else
            dragInfo = "intonazione " + juce::String (dragStartEdit.pitchSemis + steps, 0) + " semitoni";
    }
    repaint();
}

void Timeline::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex < 0 || clip == nullptr) { drag = Drag::None; return; }
    const auto& m = clip->markers[(size_t) dragIndex];
    auto edit = dragStartEdit;
    const double total = clip->lengthBeats + clip->prerollBeats;
    const double beatsPerPx = total / juce::jmax (1, getWidth());

    bool changed = false;
    if (drag == Drag::Move && std::abs (dragDx) > 0.5f)       { edit.shiftBeats += dragDx * beatsPerPx; changed = true; }
    else if (drag == Drag::Resize && std::abs (dragDx) > 0.5f)
    {
        const float orig = sampleToX (m.end) - sampleToX (m.start);
        edit.lengthScale = juce::jlimit (0.15, 8.0, edit.lengthScale * juce::jmax (6.0f, orig + dragDx) / juce::jmax (1.0f, orig));
        changed = true;
    }
    else if (drag == Drag::Pitch && std::abs (dragDy) > 0.5f)
    {
        edit.pitchSemis = juce::jlimit (-24.0f, 24.0f, dragStartEdit.pitchSemis - dragDy / 6.0f);
        changed = true;
    }
    else if (drag == Drag::Note && std::abs (dragDy) > 2.0f)
    {
        const float px = rollArea().getHeight() / juce::jmax (1.0f, highMidi - lowMidi);
        const int steps = (int) std::round (-dragDy / juce::jmax (2.0f, px));
        if (edit.note >= 0) edit.note = juce::jlimit (24, 96, edit.note + steps);
        else edit.pitchSemis = juce::jlimit (-24.0f, 24.0f, edit.pitchSemis + (float) steps);
        changed = steps != 0;
    }

    if (changed) proc.setEdit (m.syllableIndex, edit);
    drag = Drag::None;
    dragIndex = -1;
    dragDx = dragDy = 0;
    dragInfo = changed ? "rigenero..." : juce::String();
    repaint();
}

void Timeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i < 0 || clip == nullptr) return;
    auto ed = proc.getEdit (clip->markers[(size_t) i].syllableIndex);
    ed.muted = ! ed.muted;
    proc.setEdit (clip->markers[(size_t) i].syllableIndex, ed);
}

void Timeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i < 0 || clip == nullptr) { Component::mouseWheelMove (e, wheel); return; }
    auto ed = proc.getEdit (clip->markers[(size_t) i].syllableIndex);
    ed.gainDb = juce::jlimit (-24.0f, 12.0f, ed.gainDb + (wheel.deltaY > 0 ? 1.0f : -1.0f));
    proc.setEdit (clip->markers[(size_t) i].syllableIndex, ed);
}

bool Timeline::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) return true;
    return false;
}

void Timeline::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            juce::String err;
            if (proc.loadMidiFile (juce::File (f), err))
            {
                if (auto* p = proc.apvts.getParameter (IDs::mode)) p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
                dragInfo = "melodia caricata: " + juce::File (f).getFileName();
            }
            else dragInfo = err;
            repaint();
            return;
        }
}

void Timeline::showMenu (int i)
{
    const int syl = clip->markers[(size_t) i].syllableIndex;
    auto ed = proc.getEdit (syl);
    juce::PopupMenu m;
    m.addSectionHeader ("Sillaba \"" + clip->markers[(size_t) i].label + "\"");
    m.addItem (1, ed.muted ? "Riattiva" : "Muta");
    m.addItem (2, "Sussurrata", true, ed.whisper);
    m.addItem (3, "Ad-lib", true, ed.adlib);
    m.addItem (4, "Vibrato", true, ed.vibrato);
    m.addItem (5, "Un'ottava su");
    m.addItem (6, "Un'ottava giu'");
    m.addItem (7, "Ripristina questa sillaba", ! ed.isDefault());
    m.addSeparator();
    m.addItem (8, "Ripristina tutte le sillabe", proc.hasEdits());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [this, syl, ed] (int r) mutable
        {
            switch (r)
            {
                case 1: ed.muted = ! ed.muted; break;
                case 2: ed.whisper = ! ed.whisper; break;
                case 3: ed.adlib = ! ed.adlib; break;
                case 4: ed.vibrato = ! ed.vibrato; break;
                case 5: if (ed.note >= 0) ed.note = juce::jmin (96, ed.note + 12); else ed.pitchSemis = juce::jmin (24.0f, ed.pitchSemis + 12.0f); break;
                case 6: if (ed.note >= 0) ed.note = juce::jmax (24, ed.note - 12); else ed.pitchSemis = juce::jmax (-24.0f, ed.pitchSemis - 12.0f); break;
                case 7: ed = {}; break;
                case 8: proc.resetEdits(); return;
                default: return;
            }
            proc.setEdit (syl, ed);
        });
}

} // namespace vocalis
