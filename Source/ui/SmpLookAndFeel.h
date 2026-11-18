#pragma once
// Xoxicoa Sampler look: the INVERSE of Xoxicoa FX. White is the main colour (window + panels),
// black is secondary (controls, text, waveform, pads); light greys only for minor details.
// XfxKnob is the exact same responsive rotary as in Xoxicoa FX (velocity drag, Shift fine,
// Alt/Cmd-click or double-click reset, wheel, value bubble).
#include <juce_gui_basics/juce_gui_basics.h>

namespace xsmp::ui
{
namespace Colours
{
    const juce::Colour bg      { 0xffffffff };   // window (white = main colour)
    const juce::Colour panel   { 0xffffffff };   // panels
    const juce::Colour panelOn { 0xfff7f7f7 };
    const juce::Colour well    { 0xffffffff };   // combo / button body
    const juce::Colour knob    { 0xff0a0a0a };   // knob body (black = secondary)
    const juce::Colour edge    { 0xffe2e2e2 };   // hairlines (minor, light grey)
    const juce::Colour edgeHi  { 0xff0a0a0a };   // hover / focus outline
    const juce::Colour track   { 0xffe6e6e6 };   // knob track (minor)
    const juce::Colour dim     { 0xff8c8c8c };   // tertiary text
    const juce::Colour mid     { 0xff3a3a3a };   // secondary text
    const juce::Colour text    { 0xff0a0a0a };   // primary text / active (black)
    const juce::Colour white   { 0xffffffff };
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

/** Rotary knob: velocity-sensitive vertical/horizontal drag, Shift = fine, Alt/Cmd-click or
    double-click = default, mouse wheel (Shift = fine), value bubble on hover and drag. */
class XfxKnob : public juce::Slider
{
public:
    XfxKnob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setScrollWheelEnabled (true);
        setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { juce::Slider::mouseDown (e); return; }
        if ((e.mods.isAltDown() || e.mods.isCommandDown()) && isDoubleClickReturnEnabled())
        {
            setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
            return;
        }
        juce::Slider::mouseDown (e);   // starts the host gesture + popup bubble
        lastPos = e.position;
        lastTime = e.eventTime;
        dragProp = valueToProportionOfLength (getValue());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const auto d = e.position - lastPos;
        const float px = d.x - d.y;                                    // up / right = increase
        const double dt = juce::jmax (1.0, (double) (e.eventTime - lastTime).inMilliseconds());
        const double speed = std::abs (px) / dt;                      // pixels per ms
        double perPixel = 1.0 / 220.0;                                 // 220 px = full sweep at slow speed
        perPixel *= juce::jlimit (1.0, 3.0, 0.75 + speed * 0.9);       // fast flicks cover more ground
        if (e.mods.isShiftDown()) perPixel = 1.0 / 1600.0;             // fine
        dragProp = juce::jlimit (0.0, 1.0, dragProp + px * perPixel);
        setValue (proportionOfLengthToValue (dragProp), juce::sendNotificationSync);
        lastPos = e.position;
        lastTime = e.eventTime;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float delta = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f);
        if (delta == 0.0f) return;
        const double step = e.mods.isShiftDown() ? 0.004 : 0.03;
        auto p = valueToProportionOfLength (getValue()) + (delta > 0 ? step : -step) * juce::jmin (3.0f, std::abs (delta) * 4.0f + 0.5f);
        setValue (proportionOfLengthToValue (juce::jlimit (0.0, 1.0, p)), juce::sendNotificationSync);
    }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

private:
    juce::Point<float> lastPos;
    juce::Time lastTime;
    double dragProp = 0.0;
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Colours::edge);
        setColour (juce::Label::textColourId, Colours::mid);
        setColour (juce::Label::textWhenEditingColourId, Colours::text);
        setColour (juce::Label::backgroundWhenEditingColourId, Colours::well);
        setColour (juce::Label::outlineWhenEditingColourId, Colours::edgeHi);
        setColour (juce::TextEditor::highlightColourId, Colours::edge);
        setColour (juce::TextEditor::highlightedTextColourId, Colours::text);
        setColour (juce::CaretComponent::caretColourId, Colours::text);
        setColour (juce::ComboBox::backgroundColourId, Colours::well);
        setColour (juce::ComboBox::outlineColourId, Colours::edge);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::arrowColourId, Colours::dim);
        setColour (juce::ComboBox::focusedOutlineColourId, Colours::edgeHi);
        setColour (juce::PopupMenu::backgroundColourId, Colours::white);
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::bg);
        setColour (juce::BubbleComponent::backgroundColourId, Colours::text);
        setColour (juce::TooltipWindow::outlineColourId, Colours::text);
        setColour (juce::BubbleComponent::outlineColourId, Colours::text);
        setColour (juce::TooltipWindow::backgroundColourId, Colours::text);
        setColour (juce::TooltipWindow::textColourId, Colours::bg);
        setColour (juce::TextButton::buttonColourId, Colours::well);
        setColour (juce::TextButton::buttonOnColourId, Colours::text);
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, Colours::bg);
        setColour (juce::AlertWindow::backgroundColourId, Colours::white);
        setColour (juce::AlertWindow::textColourId, Colours::text);
        setColour (juce::AlertWindow::outlineColourId, Colours::edgeHi);
        setColour (juce::TextEditor::backgroundColourId, Colours::well);
        setColour (juce::TextEditor::textColourId, Colours::text);
        setColour (juce::TextEditor::outlineColourId, Colours::edgeHi);
        setColour (juce::TextEditor::focusedOutlineColourId, Colours::text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
        const bool filled = down || b.getToggleState() || primary;
        g.setColour (filled ? (primary && hover && ! down ? Colours::mid : Colours::text) : Colours::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (filled || (hover && b.isEnabled()) ? Colours::text : Colours::mid.withAlpha (0.55f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        const bool filled = down || b.getToggleState() || (bool) b.getProperties().getWithDefault ("primary", false);
        g.setColour ((filled ? Colours::white : Colours::text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
    }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const float big = (float) b.getProperties().getWithDefault ("fontSize", 13.0);
        return uiFont (juce::jmin (big, (float) h * 0.5f), true);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float thick = juce::jlimit (2.0f, 4.0f, r * 0.085f);
        const float arcR = r - thick * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool on = s.isEnabled() && ! (bool) s.getProperties().getWithDefault ("moduleOff", false);
        const bool hot = s.isMouseOverOrDragging();

        // track + value arc (bipolar params fill from the centre)
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0, startAngle, endAngle, true);
        g.setColour (Colours::track);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (on ? Colours::text : Colours::dim);
        g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        // body
        const float br = r - thick - juce::jmax (3.0f, r * 0.1f);
        g.setColour (on ? Colours::knob : Colours::dim);
        g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
        if (hot) { g.setColour (Colours::text); g.drawEllipse (c.x - br - 2.0f, c.y - br - 2.0f, br * 2 + 4.0f, br * 2 + 4.0f, 1.0f); }

        // pointer
        juce::Path ptr;
        const float pw = juce::jmax (1.6f, br * 0.09f);
        ptr.addRoundedRectangle (-pw * 0.5f, -br * 0.92f, pw, br * 0.5f, pw * 0.5f);
        g.setColour (Colours::white);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const float rad = r.getHeight() * 0.5f;
        g.setColour (on ? Colours::text : Colours::bg);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? Colours::text : (hover ? Colours::text : Colours::mid.withAlpha (0.55f)));
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (on ? Colours::white : Colours::mid);
        g.setFont (uiFont (juce::jmin (11.5f, r.getHeight() * 0.5f), true));
        g.drawText (b.getButtonText().isNotEmpty() ? b.getButtonText() : juce::String (on ? "ON" : "OFF"),
                    r, juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (Colours::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.isMouseOver (true) || box.hasKeyboardFocus (true) ? Colours::text : Colours::mid.withAlpha (0.55f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (Colours::text);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (12.5f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return uiFont (13.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (Colours::edgeHi);
        g.drawRect (0, 0, w, h, 1);
    }

    // Labels: text only, no boxes (an outline appears only while a value is being typed in)
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.isBeingEdited()) { g.setColour (Colours::edgeHi); g.drawRect (label.getLocalBounds()); return; }
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), getLabelBorderSize (label).subtractedFrom (label.getLocalBounds()),
                          label.getJustificationType(), 1, label.getMinimumHorizontalScale());
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (uiFont (12.0f));
        l->setColour (juce::Label::textColourId, Colours::text);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }

    // value bubble (hover + drag): white chip, black text
    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        g.setColour (Colours::text);
        g.fillRoundedRectangle (body, 3.0f);
    }
    juce::Font getSliderPopupFont (juce::Slider&) override { return uiFont (12.5f, true); }
    int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }
    void setComponentEffectForBubbleComponent (juce::BubbleComponent& b) override { b.setComponentEffect (nullptr); }
};
} // namespace xsmp::ui
