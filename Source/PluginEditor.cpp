#include "PluginEditor.h"

using namespace xsmp;
using namespace xsmp::ui;

namespace
{
    juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); } // 36 = C1
    juce::String timeStr (double s)
    {
        const int m = (int) (s / 60.0);
        return juce::String (m) + ":" + juce::String (s - m * 60.0, 2).paddedLeft ('0', 5);
    }
    const int padKeys[16] = { '1', '2', '3', '4', 'q', 'w', 'e', 'r', 'a', 's', 'd', 'f', 'z', 'x', 'c', 'v' };
}

// =================================================================== WaveformView
juce::Rectangle<float> WaveformView::waveArea() const { return getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (22.0f).withTrimmedBottom (16.0f); }

double WaveformView::xToPos (float x, juce::int64 len) const
{
    const auto a = waveArea();
    return (viewStart + (double) (x - a.getX()) / a.getWidth() * viewLen) * (double) len;
}
float WaveformView::posToX (double pos, juce::int64 len) const
{
    const auto a = waveArea();
    return a.getX() + (float) (((pos / (double) std::max<juce::int64> (1, len)) - viewStart) / viewLen) * a.getWidth();
}

int WaveformView::markerAt (juce::Point<float> p, juce::int64 len) const
{
    int best = -1; float bd = 7.0f;
    for (int i = 0; i < kNumPads; ++i)
    {
        const float x = posToX ((double) proc.getCue (i), len);
        const float d = std::abs (p.x - x);
        const bool inTab = p.y < 22.0f;
        if ((inTab && d < 10.0f && d < bd + 3.0f) || d < bd) { bd = d; best = i; }
    }
    return best;
}

void WaveformView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colours::white);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (dropHighlight ? Colours::text : Colours::edge);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, dropHighlight ? 2.0f : 1.0f);

    auto s = proc.getSample();
    const auto a = waveArea();
    if (s == nullptr || s->length() <= 0)
    {
        g.setColour (Colours::mid);
        g.setFont (uiFont (17.0f, true));
        g.drawText (proc.isLoading() ? "ANALYSING..." : "DROP A WAV / AIFF / MP3 HERE", a, juce::Justification::centred);
        g.setFont (uiFont (12.0f));
        g.setColour (Colours::dim);
        g.drawText ("or press LOAD - BPM, key and 16 cue points are found automatically", a.translated (0, 26), juce::Justification::centred);
        return;
    }
    const auto len = s->length();
    // ruler (minor grey ticks every second / bar)
    g.setColour (Colours::edge);
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    const double bpm = proc.getSampleBpm();
    if (bpm > 0)
    {
        const double beat = 60.0 / bpm * s->fileRate;
        const double visBeats = viewLen * (double) len / beat;
        const int stepBeats = visBeats > 64 ? 16 : visBeats > 16 ? 4 : 1;
        const double cueOrigin = (double) proc.getCue (0);
        double first = std::ceil ((xToPos (a.getX(), len) - cueOrigin) / (beat * stepBeats));
        for (double k = first;; ++k)
        {
            const double p = cueOrigin + k * beat * stepBeats;
            const float x = posToX (p, len);
            if (x > a.getRight()) break;
            if (x < a.getX()) continue;
            g.setColour (Colours::edge);
            g.drawVerticalLine ((int) x, a.getBottom(), a.getBottom() + 6.0f);
        }
    }
    // waveform
    const int w = (int) a.getWidth();
    const float mid = a.getCentreY(), hh = a.getHeight() * 0.48f;
    juce::Path wf;
    const double samplesPerPx = viewLen * (double) len / juce::jmax (1, w);
    std::vector<float> mn ((size_t) w), mx ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const double p0 = (viewStart * (double) len) + x * samplesPerPx, p1 = p0 + samplesPerPx;
        float lo = 0, hi = 0;
        if (samplesPerPx > (double) len / SampleData::kOverview * 2.0)
        {
            const int b0 = juce::jlimit (0, SampleData::kOverview - 1, (int) (p0 / (double) len * SampleData::kOverview));
            const int b1 = juce::jlimit (b0 + 1, SampleData::kOverview, (int) (p1 / (double) len * SampleData::kOverview));
            lo = 1; hi = -1;
            for (int k = b0; k < b1; ++k) { lo = juce::jmin (lo, s->peakMin[(size_t) k]); hi = juce::jmax (hi, s->peakMax[(size_t) k]); }
            if (lo > hi) lo = hi = 0;
        }
        else
        {
            const auto i0 = xsmp::clampI (0, len - 1, (juce::int64) p0), i1 = xsmp::clampI (i0 + 1, len, (juce::int64) std::ceil (p1));
            lo = 1; hi = -1;
            for (auto i = i0; i < i1; ++i) { const float v = 0.5f * (s->audio.getSample (0, (int) i) + s->audio.getSample (1, (int) i)); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
            if (lo > hi) lo = hi = 0;
        }
        mn[(size_t) x] = lo; mx[(size_t) x] = hi;
    }
    wf.startNewSubPath (a.getX(), mid - juce::jlimit (-1.0f, 1.0f, mx[0]) * hh);
    for (int x = 1; x < w; ++x) wf.lineTo (a.getX() + (float) x, mid - juce::jlimit (-1.0f, 1.0f, mx[(size_t) x]) * hh);
    for (int x = w - 1; x >= 0; --x) wf.lineTo (a.getX() + (float) x, mid - juce::jlimit (-1.0f, 1.0f, mn[(size_t) x]) * hh - 0.6f);
    wf.closeSubPath();
    g.setColour (Colours::text);
    g.fillPath (wf);

    // selected slice shading (light grey, minor)
    const int sel = proc.uiSelectedPad.load();
    {
        const auto c = proc.getCue (sel);
        juce::int64 e = len;
        for (int i = 0; i < kNumPads; ++i) { const auto q = proc.getCue (i); if (q > c && q < e) e = q; }
        const float x0 = juce::jmax (a.getX(), posToX ((double) c, len)), x1 = juce::jmin (a.getRight(), posToX ((double) e, len));
        if (x1 > x0) { g.setColour (Colours::text.withAlpha (0.06f)); g.fillRect (x0, a.getY(), x1 - x0, a.getHeight()); }
    }

    // cue markers
    for (int i = 0; i < kNumPads; ++i)
    {
        const float x = posToX ((double) proc.getCue (i), len);
        if (x < a.getX() - 12 || x > a.getRight() + 12) continue;
        const bool hot = i == sel || i == hover || i == dragging;
        g.setColour (Colours::text);
        g.fillRect (x - (hot ? 1.0f : 0.5f), a.getY(), hot ? 2.0f : 1.0f, a.getHeight());
        juce::Rectangle<float> tab (x, 3.0f, 19.0f, 17.0f);
        g.setColour (hot ? Colours::text : Colours::white);
        g.fillRoundedRectangle (tab, 3.0f);
        g.setColour (Colours::text);
        g.drawRoundedRectangle (tab.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (hot ? Colours::white : Colours::text);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String (i + 1), tab, juce::Justification::centred);
    }
    // playheads
    for (int p = 0; p < kNumPads; ++p)
        if (proc.isPadPlaying (p))
        {
            const float x = posToX (proc.getPadPosition (p), len);
            if (x >= a.getX() && x <= a.getRight())
            {
                g.setColour (Colours::text);
                g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
                g.fillEllipse (x - 3.5f, a.getBottom() + 3.0f, 7.0f, 7.0f);
            }
        }
    // footer
    g.setColour (Colours::dim);
    g.setFont (uiFont (10.5f));
    g.drawText (timeStr (viewStart * s->seconds()), juce::Rectangle<float> (a.getX() + 2, a.getBottom() + 2, 80, 14), juce::Justification::left);
    g.drawText (timeStr ((viewStart + viewLen) * s->seconds()), juce::Rectangle<float> (a.getRight() - 82, a.getBottom() + 2, 80, 14), juce::Justification::right);
    g.drawText ("drag markers to move cues  -  wheel = zoom  -  drag = scroll  -  double-click = full view",
                juce::Rectangle<float> (a.getX() + 90, a.getBottom() + 2, a.getWidth() - 180, 14), juce::Justification::centred);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    auto s = proc.getSample();
    const int h = s ? markerAt (e.position, s->length()) : -1;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    dragging = markerAt (e.position, s->length());
    if (dragging >= 0) { if (onCueSelected) onCueSelected (dragging); }
    else { panning = true; panAnchorX = e.position.x; panAnchorStart = viewStart; }
}
void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    if (dragging >= 0)
    {
        auto pos = (juce::int64) xToPos (e.position.x, s->length());
        if (! e.mods.isShiftDown()) // snap to the nearest zero crossing within 2 ms (click-free starts)
        {
            const int win = (int) (0.002 * s->fileRate);
            juce::int64 best = pos; float bv = 1e9f;
            for (auto i = std::max<juce::int64> (1, pos - win); i < juce::jmin (s->length(), pos + win); ++i)
            {
                const float v = std::abs (s->audio.getSample (0, (int) i));
                if (v < bv) { bv = v; best = i; }
            }
            pos = best;
        }
        proc.setCue (dragging, pos);
        repaint();
    }
    else if (panning)
    {
        const double d = (double) (e.position.x - panAnchorX) / waveArea().getWidth() * viewLen;
        viewStart = juce::jlimit (0.0, 1.0 - viewLen, panAnchorStart - d);
        repaint();
    }
}
void WaveformView::mouseUp (const juce::MouseEvent&) { dragging = -1; panning = false; }
void WaveformView::mouseDoubleClick (const juce::MouseEvent&) { viewStart = 0.0; viewLen = 1.0; repaint(); }
void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    const double anchor = xToPos (e.position.x, s->length()) / (double) s->length();
    const double minLen = juce::jmin (1.0, 0.05 * s->fileRate / (double) s->length() * 4.0);
    const double newLen = juce::jlimit (minLen, 1.0, viewLen * std::pow (0.8, (double) w.deltaY * 4.0));
    const double frac = (anchor - viewStart) / viewLen;
    viewLen = newLen;
    viewStart = juce::jlimit (0.0, 1.0 - viewLen, anchor - frac * viewLen);
    repaint();
}

// =================================================================== PadGrid
const char* PadGrid::keyLabel (int pad)
{
    static const char* k[16] = { "1", "2", "3", "4", "Q", "W", "E", "R", "A", "S", "D", "F", "Z", "X", "C", "V" };
    return k[pad];
}
juce::Rectangle<float> PadGrid::padBounds (int pad) const
{
    const float gap = 10.0f;
    const float w = ((float) getWidth() - gap * 3) / 4.0f, h = ((float) getHeight() - gap * 3) / 4.0f;
    return { (float) (pad % 4) * (w + gap), (float) (pad / 4) * (h + gap), w, h };
}
int PadGrid::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < kNumPads; ++i) if (padBounds (i).contains (p)) return i;
    return -1;
}
void PadGrid::paint (juce::Graphics& g)
{
    auto s = proc.getSample();
    const int root = (int) proc.apvts.getRawParameterValue ("pad_root")->load();
    const int sel = proc.uiSelectedPad.load();
    for (int i = 0; i < kNumPads; ++i)
    {
        auto r = padBounds (i).reduced (1.0f);
        const bool playing = proc.isPadPlaying (i);
        g.setColour (playing ? Colours::text : Colours::white);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Colours::text);
        g.drawRoundedRectangle (r, 6.0f, i == sel ? 3.0f : 1.4f);
        const auto fg = playing ? Colours::white : Colours::text;
        const auto fg2 = playing ? Colours::white.withAlpha (0.75f) : Colours::mid;
        g.setColour (fg);
        g.setFont (uiFont (26.0f, true));
        g.drawText (juce::String (i + 1), r.reduced (10, 6), juce::Justification::topLeft);
        g.setFont (uiFont (11.0f, true));
        g.setColour (fg2);
        g.drawText (noteName (root + i), r.reduced (10, 8), juce::Justification::topRight);
        // key hint chip
        juce::Rectangle<float> chip (r.getRight() - 30, r.getBottom() - 26, 20, 18);
        g.setColour (playing ? Colours::white : Colours::text);
        g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        g.setFont (uiFont (11.0f, true));
        g.drawText (keyLabel (i), chip, juce::Justification::centred);
        // mode + cue time
        const int mode = (int) proc.apvts.getRawParameterValue (padId (i, "mode"))->load();
        g.setColour (fg2);
        g.setFont (uiFont (10.5f));
        const juce::String modeStr = mode == 0 ? "ONE-SHOT" : mode == 1 ? "GATE" : "LOOP";
        const juce::String cueStr = s != nullptr ? timeStr ((double) proc.getCue (i) / s->fileRate) : "--:--";
        g.drawText (modeStr + "   " + cueStr, r.reduced (10, 8).withTrimmedRight (30), juce::Justification::bottomLeft);
    }
}
void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int p = padAt (e.position);
    if (p < 0) return;
    if (onPadSelected) onPadSelected (p);
    const int root = (int) proc.apvts.getRawParameterValue ("pad_root")->load();
    mouseNote = root + p;
    const float vel = juce::jlimit (0.3f, 1.0f, 1.0f - (e.position.y - padBounds (p).getY()) / padBounds (p).getHeight() * 0.5f);
    proc.keyboardState.noteOn (1, mouseNote, vel);
}
void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (mouseNote >= 0) proc.keyboardState.noteOff (1, mouseNote, 0.0f);
    mouseNote = -1;
}

// =================================================================== Auto slot tile
AutoSlotTile::AutoSlotTile (XoxicoaSamplerProcessor& p, int slotIndex) : proc (p), slot (slotIndex)
{
    len.addItemList (autoLenChoices(), 1);
    aLen = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, autoLenId (slot), len);
    for (juce::Component* c : { (juce::Component*) &len, (juce::Component*) &roll, (juce::Component*) &lock }) { addAndMakeVisible (*c); c->setWantsKeyboardFocus (false); }
    lock.setClickingTogglesState (true);
    roll.setTooltip ("Re-roll only this version (new seed + strategy)");
    lock.setTooltip ("Lock this version so AUTO keeps it");
    roll.onClick = [this] { proc.autoRollSlot (slot); repaint(); };
    lock.onClick = [this] { proc.setAutoLocked (slot, lock.getToggleState()); refresh(); };
    refresh();
}
void AutoSlotTile::refresh()
{
    lock.setToggleState (proc.isAutoLocked (slot), juce::dontSendNotification);
    roll.setEnabled (! proc.isAutoLocked (slot));
    repaint();
}
void AutoSlotTile::resized()
{
    auto r = getLocalBounds().reduced (5);
    auto bottom = r.removeFromBottom (22);
    roll.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 2));
    bottom.removeFromLeft (4);
    lock.setBounds (bottom);
    r.removeFromBottom (4);
    len.setBounds (r.removeFromBottom (22));
}
void AutoSlotTile::mouseDown (const juce::MouseEvent&)
{
    auto* prm = proc.apvts.getParameter ("auto_slot");
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) slot)); prm->endChangeGesture();
}
void AutoSlotTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool active = (int) proc.apvts.getRawParameterValue ("auto_slot")->load() == slot;
    const bool locked = proc.isAutoLocked (slot);
    g.setColour (Colours::white);
    g.fillRoundedRectangle (r, 5.0f);
    // header strip: black when this is the active version
    auto head = r.withHeight (26.0f);
    if (active)
    {
        juce::Path hp; hp.addRoundedRectangle (head.getX(), head.getY(), head.getWidth(), head.getHeight(), 5.0f, 5.0f, true, true, false, false);
        g.setColour (Colours::text); g.fillPath (hp);
    }
    g.setColour (active ? Colours::white : Colours::text);
    g.setFont (uiFont (18.0f, true));
    g.drawText (autoSlotChoices()[slot], head.reduced (8, 0), juce::Justification::centredLeft);
    g.setFont (uiFont (9.0f, true));
    g.setColour (active ? Colours::white.withAlpha (0.85f) : Colours::mid);
    g.drawText (juce::String (strategyName (proc.getAutoStrategy (slot))) + (locked ? "  *" : ""), head.reduced (7, 0), juce::Justification::centredRight);
    g.setColour (Colours::text);
    g.drawRoundedRectangle (r, 5.0f, active ? 2.4f : 1.0f);

    // mini pattern: what this version will actually play at the current Chaos / Density
    auto pr = juce::Rectangle<float> (r.getX() + 6, head.getBottom() + 5, r.getWidth() - 12, r.getHeight() - 26 - 5 - 58);
    g.setColour (Colours::panelOn);
    g.fillRect (pr);
    const int li = proc.getAutoLenIdx (slot);
    const auto& f = proc.getAutoFlip (slot, li);
    const int L = juce::jmax (1, f.lenTicks);
    const int bars = juce::jmax (1, L / kTicksPerBar);
    g.setColour (Colours::edge);
    for (int b = 1; b < bars && bars <= 16; ++b) { const float x = pr.getX() + pr.getWidth() * (float) (b * kTicksPerBar) / (float) L; g.drawVerticalLine ((int) x, pr.getY(), pr.getBottom()); }
    const float chaosV = proc.apvts.getRawParameterValue ("auto_chaos")->load();
    const float dens = proc.apvts.getRawParameterValue ("auto_density")->load();
    const float rowH = pr.getHeight() / 16.0f;
    for (int i = 0; i < f.num; ++i)
    {
        const auto& e = f.ev[(size_t) i];
        if (e.densRank > dens + 1.0e-6f) continue;
        const bool fx = chaosV > e.fxRank;
        if (fx && (e.flags & kGap)) continue;
        const int pad = chaosV > e.altRank ? e.altPad : e.pad;
        const float x0 = pr.getX() + pr.getWidth() * (float) e.tick / (float) L;
        const float w = juce::jmax (1.2f, pr.getWidth() * (float) e.len / (float) L - 0.6f);
        float y = pr.getY() + (15 - pad) * rowH;
        if (fx && e.semis != 0) y -= juce::jlimit (-3.0f, 3.0f, (float) e.semis / 4.0f) * rowH;   // pitch accents drawn higher/lower
        y = juce::jlimit (pr.getY(), pr.getBottom() - rowH, y);
        juce::Rectangle<float> cell (x0, y, w, juce::jmax (2.0f, rowH));
        g.setColour (Colours::text);
        if (fx && (e.flags & kRev)) g.drawRect (cell, 0.8f); else g.fillRect (cell);
    }
    // playhead
    const double pos = proc.getAutoPosTicks();
    if (active && proc.isAutoRunning() && pos >= 0.0)
    {
        const float x = pr.getX() + pr.getWidth() * (float) (std::fmod (pos, (double) L) / L);
        g.setColour (Colours::white.withAlpha (0.7f));
        g.fillRect (pr.getX(), pr.getY(), x - pr.getX(), pr.getHeight());   // played part washed out
        g.setColour (Colours::text);
        g.fillRect (x - 1.0f, pr.getY() - 2.0f, 2.0f, pr.getHeight() + 4.0f);
        juce::Path tri; tri.addTriangle (x - 4.0f, pr.getY() - 4.0f, x + 4.0f, pr.getY() - 4.0f, x, pr.getY() + 1.0f);
        g.fillPath (tri);
    }
}

void DragOutButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 5 && makeFile)
    {
        dragging = true;
        const auto f = makeFile();
        if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        return;
    }
    juce::TextButton::mouseDrag (e);
}
void DragOutButton::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragging;
    dragging = false;
    if (wasDrag) { setState (juce::Button::buttonNormal); return; }
    juce::TextButton::mouseUp (e);
}

// =================================================================== Editor
XoxicoaSamplerEditor::XoxicoaSamplerEditor (XoxicoaSamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p), pads (p)
{
    setLookAndFeel (&lnf);
    setSize (kW, kH);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (wave);
    addAndMakeVisible (pads);
    wave.onCueSelected = [this] (int i) { selectPad (i); };
    pads.onPadSelected = [this] (int i) { selectPad (i); };

    for (auto* b : { &loadBtn, &halfBtn, &dblBtn, &resetCuesBtn }) { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); }
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) proc.loadFileAsync (f);
        });
    };
    halfBtn.onClick = [this] { if (proc.getSampleBpm() > 0) proc.setBpmOverride (proc.getSampleBpm() * 0.5); };
    dblBtn.onClick  = [this] { if (proc.getSampleBpm() > 0) proc.setBpmOverride (proc.getSampleBpm() * 2.0); };
    resetCuesBtn.onClick = [this] { proc.resetCuesToDetected(); };
    halfBtn.setTooltip ("Detected tempo is double - halve it");
    dblBtn.setTooltip ("Detected tempo is half - double it");

    auto& s = proc.apvts;
    for (auto* k : { &keyShift, &volume, &attack, &release, &filter, &gain }) { addAndMakeVisible (*k); k->setWantsKeyboardFocus (false); k->setPopupDisplayEnabled (true, true, this); k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18); }
    aKeyShift = std::make_unique<SA> (s, "key_shift", keyShift);
    aVolume   = std::make_unique<SA> (s, "master", volume);
    keyTarget.addItemList (keyTargetChoices(), 1);
    for (int n = 0; n <= 112; ++n) padRoot.addItem (noteName (n) + "  (" + juce::String (n) + ")", n + 1);
    for (auto* c : { &keyTarget, &padRoot, &padMode, &choke }) { addAndMakeVisible (*c); c->setWantsKeyboardFocus (false); }
    aKeyTarget = std::make_unique<CA> (s, "key_target", keyTarget);
    aRoot      = std::make_unique<CA> (s, "pad_root", padRoot);
    addAndMakeVisible (sync); sync.setWantsKeyboardFocus (false);
    aSync = std::make_unique<BA> (s, "sync", sync);
    padMode.addItemList (padModeChoices(), 1);
    choke.addItemList (chokeChoices(), 1);

    // ---- AUTO
    autoBtn.getProperties().set ("primary", true);
    autoBtn.getProperties().set ("fontSize", 20.0);
    autoBtn.setTooltip ("New flip: re-rolls every unlocked version A-F from the sample's BPM, key, transients and phrases");
    undoBtn.setTooltip ("Back to the previous roll");
    autoPlayBtn.setClickingTogglesState (true);
    autoPlayBtn.setTooltip ("Latch: plays the active version in sync, starting on the next host bar");
    dragMidi.setTooltip ("Drag the active version out as MIDI (pads from the root note)");
    dragWav.setTooltip ("Drag the active version out as audio at the host tempo");
    for (juce::Component* c : { (juce::Component*) &autoBtn, (juce::Component*) &undoBtn, (juce::Component*) &autoPlayBtn,
                                (juce::Component*) &historyBox, (juce::Component*) &dragMidi, (juce::Component*) &dragWav })
    { addAndMakeVisible (*c); c->setWantsKeyboardFocus (false); }
    autoBtn.onClick = [this] { proc.autoRollAll(); refreshHistory(); for (auto& t : tiles) t->refresh(); };
    undoBtn.onClick = [this] { proc.autoUndo(); refreshHistory(); for (auto& t : tiles) t->refresh(); };
    historyBox.setTextWhenNothingSelected ("HISTORY");
    historyBox.onChange = [this]
    {
        const int idx = historyBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getAutoHistoryPos()) { proc.autoJumpToHistory (idx); for (auto& t : tiles) t->refresh(); }
    };
    aAutoPlay = std::make_unique<BA> (s, "auto_play", autoPlayBtn);
    for (auto* k : { &chaos, &density, &swing }) { addAndMakeVisible (*k); k->setWantsKeyboardFocus (false); k->setPopupDisplayEnabled (true, true, this); k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18); }
    aChaos   = std::make_unique<SA> (s, "auto_chaos", chaos);
    aDensity = std::make_unique<SA> (s, "auto_density", density);
    aSwing   = std::make_unique<SA> (s, "auto_swing", swing);
    chaos.setTooltip ("Variation: reordered chops, reverses, pitch and octave accents, gaps, stutters");
    density.setTooltip ("How many hits play (low = sparse, high = every chop)");
    swing.setTooltip ("Swing on the 1/16 off-beats");
    for (int v = 0; v < kNumAutoSlots; ++v) { tiles[(size_t) v] = std::make_unique<AutoSlotTile> (proc, v); addAndMakeVisible (*tiles[(size_t) v]); }
    dragMidi.makeFile = [this]
    {
        const int slot = (int) proc.apvts.getRawParameterValue ("auto_slot")->load();
        return proc.exportAutoMidi (slot, proc.getHostBpm() > 0 ? proc.getHostBpm() : 120.0);
    };
    dragMidi.onClick = [this] { auto f = dragMidi.makeFile(); if (f.existsAsFile()) f.revealToUser(); };
    dragWav.makeFile = [this] { return writeAutoWav(); };
    dragWav.onClick = [this] { auto f = writeAutoWav(); if (f.existsAsFile()) f.revealToUser(); };
    refreshHistory();
    resized();

    selectPad (proc.uiSelectedPad.load());
    updateReadouts();
    startTimerHz (30);
}

XoxicoaSamplerEditor::~XoxicoaSamplerEditor()
{
    releaseAllKeys();
    setLookAndFeel (nullptr);
}

void XoxicoaSamplerEditor::selectPad (int p)
{
    p = juce::jlimit (0, kNumPads - 1, p);
    proc.uiSelectedPad = p;
    aAttack.reset(); aRelease.reset(); aFilter.reset(); aGain.reset(); aMode.reset(); aChoke.reset();
    auto& s = proc.apvts;
    aAttack  = std::make_unique<SA> (s, padId (p, "attack"), attack);
    aRelease = std::make_unique<SA> (s, padId (p, "release"), release);
    aFilter  = std::make_unique<SA> (s, padId (p, "filter"), filter);
    aGain    = std::make_unique<SA> (s, padId (p, "gain"), gain);
    aMode    = std::make_unique<CA> (s, padId (p, "mode"), padMode);
    aChoke   = std::make_unique<CA> (s, padId (p, "choke"), choke);
    repaint();
}

void XoxicoaSamplerEditor::refreshHistory()
{
    const auto& h = proc.getAutoHistory();
    const int pos = proc.getAutoHistoryPos();
    if ((int) h.size() == shownHistSize && pos == shownHistPos) return;
    shownHistSize = (int) h.size(); shownHistPos = pos;
    historyBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) h.size(); ++i)
        historyBox.addItem ("#" + juce::String (i + 1) + "  " + h[(size_t) i].label, i + 1);
    if (pos >= 0 && pos < (int) h.size()) historyBox.setSelectedId (pos + 1, juce::dontSendNotification);
    undoBtn.setEnabled (pos > 0);
}

juce::File XoxicoaSamplerEditor::writeAutoWav() const
{
    const int slot = (int) proc.apvts.getRawParameterValue ("auto_slot")->load();
    const int li = proc.getAutoLenIdx (slot);
    const double bpm = proc.getHostBpm() > 0 ? proc.getHostBpm() : 120.0;
    juce::AudioBuffer<float> buf;
    if (! proc.renderAutoVersion (slot, li, bpm, 1, buf, 48000.0)) return {};
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("Xoxicoa Auto " + autoSlotChoices()[slot] + " " + autoLenChoices()[li] + " " + juce::String (bpm, 0) + "bpm.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000.0, (unsigned int) buf.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return f;
}

void XoxicoaSamplerEditor::updateReadouts()
{
    auto s = proc.getSample();
    if (s == nullptr) { fileText = proc.isLoading() ? "Analysing..." : "No sample loaded"; bpmText = "--"; keyText = "--"; }
    else
    {
        fileText = s->name + "   " + timeStr (s->seconds()) + "   " + juce::String (s->fileRate / 1000.0, 1) + " kHz";
        const double b = proc.getSampleBpm();
        bpmText = b > 0 ? juce::String (b, std::abs (b - std::round (b)) < 0.01 ? 0 : 1) : "--";
        const auto& a = s->analysis;
        keyText = a.keyRoot >= 0 ? keyNames()[a.keyRoot] + (a.minor ? " min" : " maj") : "--";
    }
    const double hb = proc.getHostBpm();
    const bool syncOn = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    tempoText = syncOn ? (hb > 0 ? "HOST " + juce::String (hb, 1) + " BPM  x" + juce::String (proc.getTempoRatio(), 3) : "HOST TEMPO --")
                       : "SYNC OFF - ORIGINAL TEMPO";
    const double semis = proc.getTotalSemis();
    const int snap = proc.getSnapOffset();
    statusText = "PITCH " + juce::String (semis > 0 ? "+" : "") + juce::String (semis, 0) + " st"
               + (snap != 0 || proc.apvts.getRawParameterValue ("key_target")->load() > 0.5f ? "  (snap " + juce::String (snap > 0 ? "+" : "") + juce::String (snap) + ")" : juce::String());
    auto err = proc.getLastError();
    if (err.isNotEmpty()) fileText += "   -   " + err;
}

void XoxicoaSamplerEditor::timerCallback()
{
    updateReadouts();
    wave.repaint();
    pads.repaint();
    repaint (0, 0, kW, 70);
    repaint (getWidth() - 400, 500, 400, 60);
    refreshHistory();
    for (auto& t : tiles) t->refresh();
    const juce::String apText = proc.isAutoRunning() ? "AUTO PLAY  -  PLAYING" : proc.isAutoArmed() ? "AUTO PLAY  -  NEXT BAR" : "AUTO PLAY";
    if (autoPlayBtn.getButtonText() != apText) autoPlayBtn.setButtonText (apText);
}

void XoxicoaSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    // header
    g.setColour (Colours::text);
    g.setFont (uiFont (24.0f, true));
    g.drawText ("XOXICOA", 20, 14, 130, 30, juce::Justification::left);
    g.setFont (uiFont (24.0f, false));
    g.drawText ("SAMPLER", 136, 14, 140, 30, juce::Justification::left);
    g.setColour (Colours::mid);
    g.setFont (uiFont (11.5f));
    g.drawText (fileText, 20, 44, 560, 16, juce::Justification::left);

    auto readout = [&] (int x, const juce::String& label, const juce::String& value, int w)
    {
        g.setColour (Colours::dim); g.setFont (uiFont (10.0f, true));
        g.drawText (label, x, 12, w, 12, juce::Justification::left);
        g.setColour (Colours::text); g.setFont (uiFont (24.0f, true));
        g.drawText (value, x, 24, w, 30, juce::Justification::left);
    };
    readout (600, "BPM", bpmText, 90);
    readout (770, "KEY", keyText, 110);
    g.setColour (Colours::mid); g.setFont (uiFont (10.5f, true));
    g.drawText (tempoText, 880, 18, 230, 14, juce::Justification::right);
    g.drawText (statusText, 880, 36, 230, 14, juce::Justification::right);
    g.setColour (Colours::edge);
    g.drawHorizontalLine (66, 16.0f, (float) getWidth() - 16.0f);

    // right column panels
    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (Colours::edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (Colours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (title, r.getX() + 14, r.getY() + 10, r.getWidth() - 28, 16, juce::Justification::left);
    };
    const int sel = proc.uiSelectedPad.load();
    panel ({ 16, 300, 1088, 188 }, "AUTO FLIP");
    {
        const int slot = (int) proc.apvts.getRawParameterValue ("auto_slot")->load();
        g.setColour (Colours::mid); g.setFont (uiFont (10.5f, true));
        g.drawText ("ACTIVE  " + autoSlotChoices()[slot] + "  /  " + strategyName (proc.getAutoStrategy (slot)) + "  /  "
                        + autoLenChoices()[proc.getAutoLenIdx (slot)] + "   -   conformed to host BPM + KEY TARGET",
                    120, 310, 640, 14, juce::Justification::left);
        g.setColour (Colours::dim); g.setFont (uiFont (10.0f));
        g.drawText ("C0 plays the active version   C#0-F#0 select A-F", 30, 464, 220, 13, juce::Justification::left);
    }
    panel ({ 636, 500, 468, 186 }, "KEY + TEMPO");
    panel ({ 636, 698, 468, 206 }, "PAD " + juce::String (sel + 1) + "   " + noteName ((int) proc.apvts.getRawParameterValue ("pad_root")->load() + sel)
                                    + "  /  key " + PadGrid::keyLabel (sel));
    g.setColour (Colours::mid); g.setFont (uiFont (10.5f, true));
    auto lbl = [&] (juce::Component& c, const juce::String& t)
    { g.drawText (t, c.getX(), c.getY() - 15, c.getWidth(), 13, juce::Justification::centred); };
    lbl (keyShift, "KEY SHIFT"); lbl (volume, "VOLUME"); lbl (keyTarget, "KEY TARGET (SNAP)"); lbl (padRoot, "PAD ROOT NOTE");
    lbl (attack, "ATTACK"); lbl (release, "RELEASE"); lbl (filter, "FILTER  LP / HP"); lbl (gain, "GAIN");
    lbl (padMode, "MODE"); lbl (sync, "HOST SYNC"); lbl (choke, "CHOKE GROUP");
    lbl (chaos, "CHAOS"); lbl (density, "DENSITY"); lbl (swing, "SWING");
    g.setColour (Colours::dim); g.setFont (uiFont (10.0f));
    g.drawText ("Pads: MIDI " + noteName ((int) proc.apvts.getRawParameterValue ("pad_root")->load()) + " and up, or keys 1-4 / Q-R / A-F / Z-V",
                16, getHeight() - 16, 600, 13, juce::Justification::left);
    if (dragOver)
    {
        g.setColour (Colours::text.withAlpha (0.04f));
        g.fillAll();
    }
}

void XoxicoaSamplerEditor::resized()
{
    loadBtn.setBounds (470, 18, 64, 28);
    halfBtn.setBounds (690, 24, 32, 22);
    dblBtn.setBounds (726, 24, 32, 22);
    wave.setBounds (16, 76, getWidth() - 32, 212);

    // AUTO panel (16,300,1088,188)
    autoBtn.setBounds (30, 334, 100, 50);
    undoBtn.setBounds (138, 334, 102, 23);
    historyBox.setBounds (138, 361, 102, 23);
    autoPlayBtn.setBounds (30, 392, 210, 28);
    dragMidi.setBounds (30, 428, 102, 24);
    dragWav.setBounds (138, 428, 102, 24);
    for (int v = 0; v < kNumAutoSlots; ++v) if (tiles[(size_t) v] != nullptr) tiles[(size_t) v]->setBounds (256 + v * 102, 330, 97, 148);
    chaos.setBounds (876, 350, 74, 100);
    density.setBounds (952, 350, 74, 100);
    swing.setBounds (1028, 350, 74, 100);

    pads.setBounds (16, 500, 604, 386);
    resetCuesBtn.setBounds (1004, 506, 92, 22);
    // KEY + TEMPO panel (636,500,468,186)
    keyShift.setBounds (652, 546, 100, 120);
    keyTarget.setBounds (772, 560, 150, 26);
    padRoot.setBounds (772, 624, 150, 26);
    sync.setBounds (936, 624, 60, 26);
    volume.setBounds (996, 546, 100, 120);
    // PAD panel (636,698,468,206)
    padMode.setBounds (652, 746, 130, 26);
    choke.setBounds (652, 812, 130, 26);
    attack.setBounds (796, 738, 74, 100);
    release.setBounds (872, 738, 74, 100);
    filter.setBounds (948, 738, 74, 100);
    gain.setBounds (1024, 738, 74, 100);
}

bool XoxicoaSamplerEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = (int) juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    for (int i = 0; i < kNumPads; ++i) if (c == padKeys[i]) return true; // handled in keyStateChanged
    return false;
}

bool XoxicoaSamplerEditor::keyStateChanged (bool)
{
    bool used = false;
    const int root = (int) proc.apvts.getRawParameterValue ("pad_root")->load();
    for (int i = 0; i < kNumPads; ++i)
    {
        const int kc = padKeys[i];
        const bool down = juce::KeyPress::isKeyCurrentlyDown (kc) || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) kc));
        if (down != keyDown[(size_t) i])
        {
            keyDown[(size_t) i] = down;
            if (down) { proc.keyboardState.noteOn (1, root + i, 0.8f); selectPad (i); }
            else proc.keyboardState.noteOff (1, root + i, 0.0f);
            used = true;
        }
    }
    return used;
}

void XoxicoaSamplerEditor::releaseAllKeys()
{
    const int root = (int) proc.apvts.getRawParameterValue ("pad_root")->load();
    for (int i = 0; i < kNumPads; ++i)
        if (keyDown[(size_t) i]) { proc.keyboardState.noteOff (1, root + i, 0.0f); keyDown[(size_t) i] = false; }
}
void XoxicoaSamplerEditor::focusLost (FocusChangeType) { releaseAllKeys(); }

bool XoxicoaSamplerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (XoxicoaSamplerProcessor::isSupportedFile (juce::File (f))) return true;
    return false;
}
void XoxicoaSamplerEditor::fileDragEnter (const juce::StringArray&, int, int) { dragOver = wave.dropHighlight = true; repaint(); }
void XoxicoaSamplerEditor::fileDragExit (const juce::StringArray&) { dragOver = wave.dropHighlight = false; repaint(); }
void XoxicoaSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = wave.dropHighlight = false;
    for (auto& f : files)
        if (XoxicoaSamplerProcessor::isSupportedFile (juce::File (f))) { proc.loadFileAsync (juce::File (f)); break; }
    repaint();
}
