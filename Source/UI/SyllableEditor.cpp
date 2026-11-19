#include "SyllableEditor.h"
#include "VocalisLookAndFeel.h"
#include "PluginProcessor.h"

namespace vocalis
{

SyllableEditor::SyllableEditor (VocalisProcessor& p) : proc (p)
{
    setTooltip ("Trascina una sillaba per spostarla - bordo destro: durata - Shift+su/giu': intonazione - "
                "rotella: volume - doppio clic: muta - tasto destro: menu");
    startTimerHz (30);
}

void SyllableEditor::setClip (RenderedClip::Ptr c)
{
    clip = c;
    if (drag == Drag::None) { dragIndex = -1; dragInfo.clear(); }
    rebuildPeaks();
    repaint();
}

juce::Rectangle<float> SyllableEditor::laneArea() const { return getLocalBounds().toFloat().reduced (1).removeFromTop (46.0f).withTrimmedTop (4); }
juce::Rectangle<float> SyllableEditor::waveArea() const { return getLocalBounds().toFloat().reduced (1).withTrimmedTop (52.0f).withTrimmedBottom (16.0f); }

double SyllableEditor::samplesPerBeat() const
{
    return clip != nullptr ? 60.0 / clip->bpm * clip->sampleRate : 1.0;
}

float SyllableEditor::sampleToX (double s) const
{
    if (clip == nullptr || clip->audio.getNumSamples() == 0) return 0.0f;
    return (float) (s / clip->audio.getNumSamples() * getWidth());
}

void SyllableEditor::rebuildPeaks()
{
    peaks.clear();
    const int w = juce::jmax (1, getWidth());
    if (clip == nullptr || clip->audio.getNumSamples() == 0) return;
    const int n = clip->audio.getNumSamples();
    const float* d = clip->audio.getReadPointer (0);
    peaks.resize ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
        float lo = 0, hi = 0;
        for (int i = a; i < juce::jmin (b, n); ++i) { lo = juce::jmin (lo, d[i]); hi = juce::jmax (hi, d[i]); }
        peaks[(size_t) x] = { lo, hi };
    }
}

juce::Rectangle<float> SyllableEditor::blockBounds (int i) const
{
    const auto& m = clip->markers[(size_t) i];
    auto lane = laneArea();
    float x0 = sampleToX (m.start), x1 = sampleToX (m.end);
    if (i == dragIndex)
    {
        if (drag == Drag::Move) { x0 += dragDx; x1 += dragDx; }
        if (drag == Drag::Resize) x1 = juce::jmax (x0 + 6.0f, x1 + dragDx);
    }
    return { x0, lane.getY(), juce::jmax (4.0f, x1 - x0), lane.getHeight() };
}

int SyllableEditor::markerAt (juce::Point<float> p, bool& onRightEdge) const
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

void SyllableEditor::timerCallback()
{
    const double pos = proc.getPlayPosition();
    if (pos != lastPos) { lastPos = pos; repaint(); }
}

void SyllableEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0c0d11));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    if (clip == nullptr || clip->audio.getNumSamples() == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isRendering() ? "Generazione in corso..." : "Scrivi un testo per generare il vocal",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float w = (float) getWidth(), h = (float) getHeight();
    const double spb = samplesPerBeat();
    const double totalBeats = clip->audio.getNumSamples() / spb;
    const auto wave = waveArea(), lane = laneArea();
    const float mid = wave.getCentreY();

    // griglia: step (tenue), quarti, battute con numero
    const double step = juce::jmax (1.0 / 32.0, clip->stepBeats);
    for (double b = 0; b <= totalBeats + 1e-9; b += step)
    {
        const float x = (float) (b / totalBeats * w);
        const bool isBeat = std::abs (b - std::round (b)) < 1e-6;
        const bool isBar = isBeat && ((int) std::round (b)) % 4 == 0;
        g.setColour (isBar ? Colours::panelEdge.brighter (0.45f) : isBeat ? Colours::panelEdge.brighter (0.1f) : Colours::panelEdge.withAlpha (0.45f));
        g.drawVerticalLine ((int) x, lane.getY(), h - 14);
        if (isBar)
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (juce::String ((int) std::round (b) / 4 + 1), (int) x + 3, (int) (h - 15), 30, 12, juce::Justification::left);
        }
    }

    // waveform
    juce::Path p;
    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float y = mid - peaks[x].second * wave.getHeight() * 0.5f;
        if (x == 0) p.startNewSubPath (0, y); else p.lineTo ((float) x, y);
    }
    for (int x = (int) peaks.size() - 1; x >= 0; --x)
        p.lineTo ((float) x, mid - peaks[(size_t) x].first * wave.getHeight() * 0.5f);
    p.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.85f), 0, 0, Colours::accent2.withAlpha (0.85f), w, 0, false));
    g.fillPath (p);

    // blocchi delle sillabe
    for (int i = 0; i < (int) clip->markers.size(); ++i)
    {
        const auto& m = clip->markers[(size_t) i];
        const auto e = proc.getEdit (m.syllableIndex);
        auto b = blockBounds (i);
        const bool edited = ! e.isDefault();
        juce::Colour c = (i % 2 ? Colours::accent2 : Colours::accent);
        if (m.muted) c = Colours::textDim;
        const float alpha = (i == hover || i == dragIndex) ? 0.55f : 0.32f;

        g.setColour (c.withAlpha (alpha));
        g.fillRoundedRectangle (b.reduced (0.5f, 0), 4.0f);
        g.setColour (edited ? Colours::warn : c.withAlpha (0.9f));
        g.drawRoundedRectangle (b.reduced (0.5f, 0), 4.0f, edited ? 1.5f : 1.0f);

        // attacco della vocale (il punto che cade sulla griglia)
        const float nx = sampleToX (m.nucleus) + (i == dragIndex && drag == Drag::Move ? dragDx : 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawVerticalLine ((int) nx, b.getY() + 2, b.getBottom() - 2);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawVerticalLine ((int) nx, wave.getY(), wave.getBottom());

        g.setColour (m.muted ? Colours::textDim : Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (m.label, b.reduced (4, 2).removeFromTop (18), juce::Justification::centredLeft, false);

        juce::String sub;
        const float pitch = (i == dragIndex && drag == Drag::Pitch) ? dragStartEdit.pitchSemis - dragDy / 6.0f : e.pitchSemis;
        if (std::abs (pitch) > 0.05f) sub << (pitch > 0 ? "+" : "") << juce::String (pitch, 1) << "st ";
        if (std::abs (e.gainDb) > 0.05f) sub << (e.gainDb > 0 ? "+" : "") << juce::String (e.gainDb, 0) << "dB";
        if (m.muted) sub = "muta";
        if (sub.isNotEmpty())
        {
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (Colours::warn);
            g.drawText (sub, b.reduced (4, 2).removeFromBottom (14), juce::Justification::centredLeft, false);
        }
    }

    if (dragInfo.isNotEmpty())
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (dragInfo, getLocalBounds().reduced (8, 4).removeFromBottom (14), juce::Justification::centredRight);
    }

    // cursore di riproduzione
    const double pos = proc.getPlayPosition();
    if (pos >= 0)
    {
        const float x = sampleToX (pos);
        g.setColour (juce::Colours::white);
        g.fillRect (x, 0.0f, 1.5f, h);
    }
}

void SyllableEditor::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i != hover) { hover = i; repaint(); }
    setMouseCursor (i < 0 ? juce::MouseCursor::NormalCursor
                          : edge ? juce::MouseCursor::LeftRightResizeCursor
                                 : e.mods.isShiftDown() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void SyllableEditor::mouseDown (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i < 0 || clip == nullptr) return;

    if (e.mods.isPopupMenu()) { showMenu (i); return; }

    dragIndex = i;
    dragStartEdit = proc.getEdit (clip->markers[(size_t) i].syllableIndex);
    drag = edge ? Drag::Resize : e.mods.isShiftDown() ? Drag::Pitch : Drag::Move;
    dragDx = dragDy = 0;
}

void SyllableEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || clip == nullptr) return;
    const double beatsPerPx = (clip->audio.getNumSamples() / samplesPerBeat()) / juce::jmax (1, getWidth());

    if (drag == Drag::Move)
    {
        double beats = e.getDistanceFromDragStartX() * beatsPerPx;
        if (! e.mods.isAltDown()) beats = std::round (beats * 8.0) / 8.0;   // aggancio a 1/32
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
        if (! e.mods.isAltDown()) dragDy = std::round (dragDy / 6.0f) * 6.0f;   // semitoni interi
        const float semis = dragStartEdit.pitchSemis - dragDy / 6.0f;
        dragInfo = "intonazione " + juce::String (semis >= 0 ? "+" : "") + juce::String (semis, 1) + " semitoni";
    }
    repaint();
}

void SyllableEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex < 0 || clip == nullptr) { drag = Drag::None; return; }
    const auto& m = clip->markers[(size_t) dragIndex];
    auto edit = dragStartEdit;
    const double beatsPerPx = (clip->audio.getNumSamples() / samplesPerBeat()) / juce::jmax (1, getWidth());

    bool changed = false;
    if (drag == Drag::Move && std::abs (dragDx) > 0.5f)
    {
        edit.shiftBeats += dragDx * beatsPerPx;
        changed = true;
    }
    else if (drag == Drag::Resize && std::abs (dragDx) > 0.5f)
    {
        const float orig = sampleToX (m.end) - sampleToX (m.start);
        const float now = juce::jmax (6.0f, orig + dragDx);
        edit.lengthScale = juce::jlimit (0.15, 8.0, edit.lengthScale * now / juce::jmax (1.0f, orig));
        changed = true;
    }
    else if (drag == Drag::Pitch && std::abs (dragDy) > 0.5f)
    {
        edit.pitchSemis = juce::jlimit (-24.0f, 24.0f, dragStartEdit.pitchSemis - dragDy / 6.0f);
        changed = true;
    }

    if (changed)
        proc.setEdit (m.syllableIndex, edit);

    drag = Drag::None;
    dragIndex = -1;
    dragDx = dragDy = 0;
    dragInfo = changed ? "rigenero..." : juce::String();
    repaint();
}

void SyllableEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i < 0 || clip == nullptr) return;
    auto ed = proc.getEdit (clip->markers[(size_t) i].syllableIndex);
    ed.muted = ! ed.muted;
    proc.setEdit (clip->markers[(size_t) i].syllableIndex, ed);
}

void SyllableEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    bool edge = false;
    const int i = markerAt (e.position, edge);
    if (i < 0 || clip == nullptr) { Component::mouseWheelMove (e, wheel); return; }
    auto ed = proc.getEdit (clip->markers[(size_t) i].syllableIndex);
    ed.gainDb = juce::jlimit (-24.0f, 12.0f, ed.gainDb + (wheel.deltaY > 0 ? 1.0f : -1.0f));
    proc.setEdit (clip->markers[(size_t) i].syllableIndex, ed);
}

void SyllableEditor::showMenu (int i)
{
    const int syl = clip->markers[(size_t) i].syllableIndex;
    auto ed = proc.getEdit (syl);
    juce::PopupMenu m;
    m.addSectionHeader ("Sillaba \"" + clip->markers[(size_t) i].label + "\"");
    m.addItem (1, ed.muted ? "Riattiva" : "Muta");
    m.addItem (2, "Volume +3 dB");
    m.addItem (3, "Volume -3 dB");
    m.addItem (4, "Un'ottava su");
    m.addItem (5, "Un'ottava giu'");
    m.addItem (6, "Ripristina questa sillaba", ! ed.isDefault());
    m.addSeparator();
    m.addItem (7, "Ripristina tutte le sillabe", proc.hasEdits());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [this, syl, ed] (int r) mutable
        {
            switch (r)
            {
                case 1: ed.muted = ! ed.muted; break;
                case 2: ed.gainDb = juce::jmin (12.0f, ed.gainDb + 3.0f); break;
                case 3: ed.gainDb = juce::jmax (-24.0f, ed.gainDb - 3.0f); break;
                case 4: ed.pitchSemis = juce::jmin (24.0f, ed.pitchSemis + 12.0f); break;
                case 5: ed.pitchSemis = juce::jmax (-24.0f, ed.pitchSemis - 12.0f); break;
                case 6: ed = {}; break;
                case 7: proc.resetEdits(); return;
                default: return;
            }
            proc.setEdit (syl, ed);
        });
}

// ============================================================================ DragOutHandle

DragOutHandle::DragOutHandle (VocalisProcessor& p) : proc (p)
{
    setTooltip ("Trascina nella DAW per importare il vocal (WAV con effetti)");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragOutHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::panelEdge);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::accent2);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("\xE2\x87\xA3 TRASCINA"), getLocalBounds(), juce::Justification::centred);
}

void DragOutHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 5) return;
    started = true;
    juce::String error;
    auto f = proc.createTempExport (error);
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

} // namespace vocalis
