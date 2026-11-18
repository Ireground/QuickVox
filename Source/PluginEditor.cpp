#include "PluginEditor.h"

namespace
{
    // HatsOff light scheme
    const juce::Colour cBg      { 0xFF141414 };
    const juce::Colour cPanel   { 0xFFE4E4E1 };
    const juce::Colour cInset   { 0xFFC9C9C5 };
    const juce::Colour cInset2  { 0xFFBBBBB7 };
    const juce::Colour cLabel   { 0xFF5C5C59 };
    const juce::Colour cText    { 0xFF1C1C1A };
    const juce::Colour cTrack   { 0xFFB4B4B0 };
    const juce::Colour cOrange  { 0xFFEF5C25 };
    const juce::Colour cBlue    { 0xFF3960B6 };
    const juce::Colour cBlueH   { 0xFF2F519C };
    const juce::Colour cTeal    { 0xFF2D908B };
    const juce::Colour cPurple  { 0xFF8A4AB8 };
    const juce::Colour cGreen   { 0xFF23955F };

    juce::Font mono (float size, bool bold = false)
    {
       #if JUCE_WINDOWS
        const juce::String name ("Consolas");
       #else
        const juce::String name (juce::Font::getDefaultMonospacedFontName());
       #endif
        return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::String dotSep (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    const char* const modeLabels[]  = { "FOLLOW VOICE", "FIXED NOTE", "MIDI" };
    const char* const chordLabels[] = { "OFF", "OCT", "5TH", "MAJ", "MIN" };
    const char* const chordShort[]  = { "", " OCT", " 5TH", " MAJ", " MIN" };
}

//==============================================================================
QuickVoxEditor::QuickVoxEditor (QuickVoxProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    using R = juce::Rectangle<float>;
    pHeader  = R (10, 10, 740, 46);
    bSize    = R (300, 23, 96, 20);

    pCarrier = R (10, 64, 360, 170);
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i] = R (24.0f + i * 112.0f, 92, 106, 24);
    for (int i = 0; i < 5; ++i) chordBtns[(size_t) i] = R (88.0f + i * 54.0f, 126, 50, 22);
    noteBox  = R (24, 158, 196, 62);
    bSnap    = R (228, 158, 128, 24);

    pSound   = R (378, 64, 372, 170);
    pBands   = R (10, 242, 500, 140);
    meterArea = R (24, 268, 472, 100);
    pOut     = R (518, 242, 232, 140);
    levelBar = R (532, 352, 204, 8);

    sliders = {
        { "bands",    "BANDS",    R (392,  90, 344, 26), 100, 74, cOrange },
        { "formant",  "FORMANT",  R (392, 118, 344, 26), 100, 74, cOrange },
        { "air",      "AIR",      R (392, 146, 344, 26), 100, 74, cOrange },
        { "response", "RESPONSE", R (392, 174, 344, 26), 100, 74, cOrange },
        { "thick",    "THICK",    R (392, 202, 344, 26), 100, 74, cOrange },
        { "mix",      "MIX",      R (532, 272, 204, 26),  64, 66, cPurple },
        { "output",   "OUTPUT",   R (532, 304, 204, 26),  64, 66, cPurple },
    };

    // Read the saved size before setResizeLimits(): that call triggers resized() at the
    // minimum size, which used to overwrite the saved scale.
    const float s = juce::jlimit (0.75f, 2.5f, proc.uiScale.load());
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 5 / 2, H * 5 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / (double) H);
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    sizeReady = true;
    startTimerHz (30);
}

void QuickVoxEditor::resized() { if (sizeReady) proc.uiScale = uiScale(); }

void QuickVoxEditor::timerCallback()
{
    const int nb = proc.engine.meterBandCount();
    for (int b = 0; b < qv::Engine::MaxBands; ++b)
    {
        float v = 0.0f;
        if (b < nb)
        {
            const float e = proc.engine.bandMeter (b);
            const float db = e > 1.0e-6f ? 20.0f * std::log10 (e) : -120.0f;
            v = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 54.0f);
        }
        auto& d = bandDisp[(size_t) b];
        d = v > d ? v : d * 0.86f;
    }
    const float lv = proc.outLevel.load();
    const float ldb = lv > 1.0e-6f ? 20.0f * std::log10 (lv) : -120.0f;
    const float ln = juce::jlimit (0.0f, 1.0f, (ldb + 60.0f) / 60.0f);
    levelDisp = ln > levelDisp ? ln : levelDisp * 0.9f;
    repaint();
}

//==============================================================================
juce::String QuickVoxEditor::noteName (int m)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
}

juce::Rectangle<float> QuickVoxEditor::trackRect (const SliderDef& s) const
{
    return { s.row.getX() + s.labelW, s.row.getY(), s.row.getWidth() - s.labelW - s.valueW, s.row.getHeight() };
}

juce::String QuickVoxEditor::formatValue (int i) const
{
    const auto& s = sliders[(size_t) i];
    const juce::String id (s.id);
    const float v = value (s.id);
    if (id == "bands")    return juce::String ((int) v);
    if (id == "formant")  return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " st";
    if (id == "response") return juce::String (juce::roundToInt (v)) + " ms";
    if (id == "output")   return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
    return juce::String (juce::roundToInt (v)) + " %";
}

void QuickVoxEditor::setNorm (const char* id, float n)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
}

void QuickVoxEditor::clickSet (const char* id, float v)
{
    if (auto* p = param (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

QuickVoxEditor::Target QuickVoxEditor::findTarget (juce::Point<float> pt) const
{
    if (bSize.contains (pt)) return { SizeBtn, -1 };
    for (int i = 0; i < 3; ++i) if (modeBtns[(size_t) i].contains (pt)) return { ModeBtn, i };
    for (int i = 0; i < 5; ++i) if (chordBtns[(size_t) i].contains (pt)) return { ChordBtn, i };
    if (bSnap.contains (pt)) return { SnapBtn, -1 };
    if (noteBox.contains (pt)) return { NoteBox, -1 };
    for (int i = 0; i < (int) sliders.size(); ++i)
        if (trackRect (sliders[(size_t) i]).expanded (6, 0).contains (pt)) return { Slider, i };
    return {};
}

//==============================================================================
void QuickVoxEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    if (! (t == hover)) { hover = t; repaint(); }
}

void QuickVoxEditor::mouseExit (const juce::MouseEvent&) { hover = {}; repaint(); }

void QuickVoxEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    active = {};
    dragStartX = pos (e).x;
    dragStartY = pos (e).y;
    dragFine = e.mods.isShiftDown();

    switch (t.kind)
    {
        case ModeBtn:  clickSet ("mode", (float) t.index); break;
        case ChordBtn: clickSet ("chord", (float) t.index); break;
        case SnapBtn:  clickSet ("snap", value ("snap") > 0.5f ? 0.0f : 1.0f); break;
        case SizeBtn:  openSizeMenu(); break;
        case NoteBox:
            if ((int) value ("mode") == 1)
            {
                active = t;
                dragStartVal = value ("note");
                if (auto* p = param ("note")) p->beginChangeGesture();
            }
            break;
        case Slider:
        {
            active = t;
            auto* p = param (sliders[(size_t) t.index].id);
            dragStartVal = p->getValue();
            p->beginChangeGesture();
            break;
        }
        default: break;
    }
    repaint();
}

void QuickVoxEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active.kind == None) return;
    const bool fine = e.mods.isShiftDown();
    if (fine != dragFine)   // re-anchor so switching to fine mode doesn't jump
    {
        dragFine = fine;
        dragStartX = pos (e).x;
        dragStartY = pos (e).y;
        dragStartVal = active.kind == NoteBox ? value ("note") : param (sliders[(size_t) active.index].id)->getValue();
    }
    const auto pt = pos (e);
    if (active.kind == Slider)
    {
        const auto& s = sliders[(size_t) active.index];
        setNorm (s.id, dragStartVal + (pt.x - dragStartX) / trackRect (s).getWidth() * (fine ? 0.15f : 1.0f));
    }
    else if (active.kind == NoteBox)
    {
        const float dy = dragStartY - pt.y;
        setValue ("note", juce::jlimit (24.0f, 72.0f, std::floor (dragStartVal + dy / (fine ? 24.0f : 8.0f) + 0.5f)));
    }
    repaint();
}

void QuickVoxEditor::mouseUp (const juce::MouseEvent& e)
{
    if (active.kind == Slider)       param (sliders[(size_t) active.index].id)->endChangeGesture();
    else if (active.kind == NoteBox) param ("note")->endChangeGesture();
    active = {};
    hover = findTarget (pos (e));
    repaint();
}

void QuickVoxEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    if (t.kind == Slider)
    {
        auto* p = param (sliders[(size_t) t.index].id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    else if (t.kind == NoteBox && (int) value ("mode") == 1)
        clickSet ("note", 48.0f);
    repaint();
}

void QuickVoxEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const float dir = w.deltaY > 0 ? 1.0f : -1.0f;
    const auto t = findTarget (pos (e));
    if (t.kind == Slider)
    {
        const auto& s = sliders[(size_t) t.index];
        auto* p = param (s.id);
        const juce::String id (s.id);
        p->beginChangeGesture();
        if (id == "bands")
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (8.0f, 40.0f, value (s.id) + dir)));
        else
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + dir * (e.mods.isShiftDown() ? 0.002f : 0.01f)));
        p->endChangeGesture();
    }
    else if (t.kind == NoteBox && (int) value ("mode") == 1)
        clickSet ("note", juce::jlimit (24.0f, 72.0f, value ("note") + dir));
    else
        return;
    repaint();
}

void QuickVoxEditor::openSizeMenu()
{
    juce::PopupMenu m;
    const int pcts[] = { 100, 125, 150, 175, 200 };
    const int cur = juce::roundToInt (uiScale() * 100.0f);
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, juce::String (pcts[i]) + "%", true, std::abs (cur - pcts[i]) <= 1);
    juce::Component::SafePointer<QuickVoxEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal ((bSize * uiScale()).toNearestInt())),
                     [safe, pcts] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         const float f = pcts[r - 1] / 100.0f;
                         safe->setSize (juce::roundToInt (W * f), juce::roundToInt (H * f));
                     });
}

//==============================================================================
void QuickVoxEditor::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour dot) const
{
    g.setColour (cPanel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (cLabel);
    g.setFont (mono (12.0f));
    g.drawText (title, r.getX() + 14, r.getY() + 8, r.getWidth() - 40, 14, juce::Justification::centredLeft);
    g.setColour (dot);
    g.fillEllipse (r.getRight() - 19, r.getY() + 11, 8, 8);
}

void QuickVoxEditor::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                               juce::Colour fill, juce::Colour textCol, float fontSize) const
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (textCol);
    g.setFont (mono (fontSize, true));
    g.drawText (text, r, juce::Justification::centred);
}

void QuickVoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);
    g.addTransform (juce::AffineTransform::scale (uiScale()));
    auto hov = [this] (Kind k, int idx = -1) { return hover.kind == k && hover.index == idx && active.kind == None; };

    const int mode = (int) value ("mode");
    const int chord = (int) value ("chord");
    const bool snap = value ("snap") > 0.5f;
    const float live = proc.engine.detectedNote();
    const int held = proc.heldNotes.load();

    // ---------- header ----------
    g.setColour (cPanel);
    g.fillRoundedRectangle (pHeader, 7.0f);
    g.setFont (mono (25.0f, true));
    g.setColour (cText);
    g.drawText ("QUICK", pHeader.getX() + 16, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setColour (cOrange);
    g.drawText ("VOX", pHeader.getX() + 16 + 70, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setFont (mono (11.0f));
    g.setColour (cLabel);
    g.drawText ("SIMPLE VOCODER", pHeader.getX() + 148, pHeader.getY() + 2, 140, pHeader.getHeight(), juce::Justification::centredLeft);

    drawPill (g, bSize, "SIZE " + juce::String (juce::roundToInt (uiScale() * 100.0f)) + "%", hov (SizeBtn) ? cInset2 : cInset, cText);
    {
        juce::Path tri;
        const float tx = bSize.getRight() - 12, ty = bSize.getCentreY();
        tri.addTriangle (tx - 3.5f, ty - 2, tx + 3.5f, ty - 2, tx, ty + 2.5f);
        g.setColour (cText);
        g.fillPath (tri);
    }
    {
        juce::String st;
        if (mode == 0)      st = "FOLLOW" + dotSep + (live > 0 ? noteName (juce::roundToInt (live)) : juce::String ("--")) + chordShort[chord];
        else if (mode == 1) st = "NOTE" + dotSep + noteName ((int) value ("note")) + chordShort[chord];
        else                st = "MIDI" + dotSep + juce::String (held) + (held == 1 ? " NOTE" : " NOTES");
        const float pw = st.length() * 6.4f + 22.0f;
        drawPill (g, { pHeader.getRight() - 14 - pw, pHeader.getY() + 13, pw, 20 }, st, cTeal, juce::Colours::white, 11.0f);
    }

    // ---------- carrier ----------
    drawPanel (g, pCarrier, "CARRIER", cBlue);
    for (int i = 0; i < 3; ++i)
    {
        const bool on = mode == i;
        drawPill (g, modeBtns[(size_t) i], modeLabels[i],
                  on ? (hov (ModeBtn, i) ? cBlueH : cBlue) : (hov (ModeBtn, i) ? cInset2 : cInset),
                  on ? juce::Colours::white : cText);
    }
    const bool chordOn = mode != 2;
    g.setFont (mono (11.0f));
    g.setColour (chordOn ? cLabel : cLabel.withAlpha (0.45f));
    g.drawText ("CHORD", 24, (int) chordBtns[0].getY(), 60, (int) chordBtns[0].getHeight(), juce::Justification::centredLeft);
    for (int i = 0; i < 5; ++i)
    {
        const bool on = chord == i;
        auto fill = on ? cBlue : (hov (ChordBtn, i) ? cInset2 : cInset);
        auto txt = on ? juce::Colours::white : cText;
        if (! chordOn) { fill = fill.withAlpha (0.4f); txt = txt.withAlpha (0.5f); }
        drawPill (g, chordBtns[(size_t) i], chordLabels[i], fill, txt, 10.5f);
    }

    // note display
    g.setColour (cInset);
    g.fillRoundedRectangle (noteBox, 7.0f);
    if (mode == 1 && (hov (NoteBox) || active.kind == NoteBox))
    {
        g.setColour (cInset2);
        g.fillRoundedRectangle (noteBox.reduced (4), 5.0f);
    }
    {
        juce::String cap, big;
        if (mode == 0)      { cap = "LIVE PITCH";          big = live > 0 ? noteName (juce::roundToInt (live)) : juce::String ("--"); }
        else if (mode == 1) { cap = "NOTE  DRAG / WHEEL";  big = noteName ((int) value ("note")); }
        else                { cap = "PLAY MIDI ON TRACK";  big = held > 0 ? juce::String (held) + (held == 1 ? " NOTE" : " NOTES") : juce::String ("--"); }
        g.setColour (cLabel);
        g.setFont (mono (10.0f));
        g.drawText (cap, noteBox.getX() + 10, noteBox.getY() + 6, noteBox.getWidth() - 20, 12, juce::Justification::centredLeft);
        g.setColour (cBlue);
        g.setFont (mono (mode == 2 ? 24.0f : 30.0f, true));
        g.drawText (big, noteBox.withTrimmedTop (14), juce::Justification::centred);
    }
    {
        const bool en = mode == 0;
        auto fill = snap ? cBlue : (hov (SnapBtn) ? cInset2 : cInset);
        auto txt = snap ? juce::Colours::white : cText;
        if (! en) { fill = fill.withAlpha (0.4f); txt = txt.withAlpha (0.5f); }
        drawPill (g, bSnap, snap ? "SNAP ON" : "SNAP OFF", fill, txt);
        g.setFont (mono (10.0f));
        g.setColour (en ? cLabel : cLabel.withAlpha (0.45f));
        g.drawFittedText ("SNAP = ROBOTIC\nSTEPS ON NOTES", (int) bSnap.getX() + 4, (int) bSnap.getBottom() + 6,
                          (int) bSnap.getWidth(), 28, juce::Justification::topLeft, 2);
    }

    // ---------- sound + output sliders ----------
    drawPanel (g, pSound, "SOUND", cOrange);
    drawPanel (g, pOut, "OUTPUT", cPurple);
    for (int i = 0; i < (int) sliders.size(); ++i)
    {
        const auto& s = sliders[(size_t) i];
        const auto tr = trackRect (s);
        const bool hot = hov (Slider, i) || (active.kind == Slider && active.index == i);
        g.setFont (mono (11.0f));
        g.setColour (cLabel);
        g.drawText (s.label, s.row.getX(), s.row.getY(), s.labelW - 6, s.row.getHeight(), juce::Justification::centredLeft);
        g.setFont (mono (12.0f, true));
        g.setColour (cText);
        g.drawText (formatValue (i), tr.getRight() + 4, s.row.getY(), s.valueW - 4, s.row.getHeight(), juce::Justification::centredRight);

        const float n = param (s.id)->getValue();
        const float y = tr.getCentreY();
        g.setColour (cTrack);
        g.fillRect (tr.getX(), y - 1, tr.getWidth(), 2.0f);
        g.setColour (s.col);
        if (juce::String (s.id) == "formant")     // bipolar: fill from the centre
        {
            const float cx = tr.getX() + 0.5f * tr.getWidth(), hx = tr.getX() + n * tr.getWidth();
            g.fillRect (std::min (cx, hx), y - 1, std::abs (hx - cx), 2.0f);
        }
        else
            g.fillRect (tr.getX(), y - 1, n * tr.getWidth(), 2.0f);
        g.setColour (hot ? cText : s.col);
        g.fillRect (tr.getX() + n * tr.getWidth() - 1.5f, y - 6, 3.0f, 12.0f);
    }

    // output level
    g.setColour (cInset);
    g.fillRoundedRectangle (levelBar, 4.0f);
    g.setColour (levelDisp > 0.95f ? cOrange : cGreen);
    g.fillRoundedRectangle (levelBar.withWidth (std::max (8.0f, levelDisp * levelBar.getWidth())), 4.0f);
    g.setFont (mono (10.0f));
    g.setColour (cLabel);
    g.drawText ("LEVEL", levelBar.getX(), levelBar.getY() - 16, 80, 12, juce::Justification::centredLeft);

    // ---------- band meter ----------
    drawPanel (g, pBands, "BANDS", cGreen);
    g.setColour (cInset);
    g.fillRoundedRectangle (meterArea, 5.0f);
    {
        const int nb = std::max (1, proc.engine.meterBandCount());
        const auto inner = meterArea.reduced (8, 8);
        const float bw = inner.getWidth() / (float) nb;
        constexpr int steps = 14;
        const float sh = inner.getHeight() / (float) steps;
        for (int b = 0; b < nb; ++b)
        {
            const int lit = juce::roundToInt (bandDisp[(size_t) b] * steps);
            for (int k = 0; k < steps; ++k)
            {
                const juce::Rectangle<float> cell (inner.getX() + b * bw + 1.0f, inner.getBottom() - (k + 1) * sh + 1.0f,
                                                   std::max (1.0f, bw - 2.0f), std::max (1.0f, sh - 2.0f));
                g.setColour (k < lit ? cGreen : cInset2);
                g.fillRoundedRectangle (cell, 1.5f);
            }
        }
        g.setFont (mono (10.0f));
        g.setColour (cLabel);
        g.drawText ("LOW", meterArea.getX(), meterArea.getBottom() + 2, 60, 12, juce::Justification::centredLeft);
        g.drawText ("HIGH", meterArea.getRight() - 60, meterArea.getBottom() + 2, 60, 12, juce::Justification::centredRight);
    }
}
