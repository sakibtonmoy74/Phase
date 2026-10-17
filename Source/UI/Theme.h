#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine/Attributes.h"

namespace pl::theme
{
    // Dark console palette with an amber accent (MA-style selection colour)
    inline const juce::Colour bg        { 0xff111214 };
    inline const juce::Colour panel     { 0xff1a1c20 };
    inline const juce::Colour panel2    { 0xff23262c };
    inline const juce::Colour line      { 0xff30343b };
    inline const juce::Colour text      { 0xffe8e8e8 };
    inline const juce::Colour textDim   { 0xff8b919a };
    inline const juce::Colour accent    { 0xfff2a33a };
    inline const juce::Colour ok        { 0xff3ccf7a };
    inline const juce::Colour danger    { 0xffe5484d };

    // Touch-friendly sizing (also comfortable with a mouse)
    constexpr int rowHeight   = 36;
    constexpr int buttonH     = 40;
    constexpr int gap         = 8;
    constexpr int topBarH     = 52;

    inline juce::Colour familyColour (Family f)
    {
        switch (f)
        {
            case Family::Dimmer:   return juce::Colour (0xfff2a33a);
            case Family::Position: return juce::Colour (0xff4f8cff);
            case Family::Gobo:     return juce::Colour (0xff3ccf7a);
            case Family::Color:    return juce::Colour (0xffe056c8);
            case Family::Beam:     return juce::Colour (0xff3cc8d8);
            case Family::Focus:    return juce::Colour (0xff9d7bff);
            case Family::Control:  return juce::Colour (0xff9aa0a8);
            case Family::None:     break;
        }
        return textDim;
    }

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

namespace pl
{

class PhaseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhaseLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              theme::bg, theme::panel2, theme::panel, theme::line, theme::text,
              theme::panel2, theme::bg, theme::accent, theme::text })
    {
        using namespace theme;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::TextEditor::backgroundColourId, bg);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::Label::textColourId, text);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::outlineColourId, line);
        setColour (juce::TableHeaderComponent::backgroundColourId, panel2);
        setColour (juce::TableHeaderComponent::textColourId, textDim);
        setColour (juce::TableHeaderComponent::outlineColourId, line);
        setColour (juce::ScrollBar::thumbColourId, line);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, bg);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, bg);
        setColour (juce::AlertWindow::backgroundColourId, panel);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = base;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        else if (down)       c = c.brighter (0.25f);
        else if (highlighted) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::line.withAlpha (b.getToggleState() ? 0.0f : 1.0f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return theme::font (juce::jmin (15.0f, (float) h * 0.42f), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (15.0f); }
    juce::Font getPopupMenuFont() override                 { return theme::font (15.0f); }
    juce::Font getLabelFont (juce::Label& l) override      { return l.getFont(); }

    /** Console-style vertical fader: dark track, amber fill, wide cap. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float minPos, float maxPos, juce::Slider::SliderStyle style,
                           juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }

        auto fill = s.findColour (juce::Slider::trackColourId);
        auto track = juce::Rectangle<float> ((float) x + (float) w * 0.5f - 5.0f, (float) y, 10.0f, (float) h);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (track, 5.0f);
        g.setColour (fill.withAlpha (s.isEnabled() ? 0.9f : 0.3f));
        g.fillRoundedRectangle (track.withTop (pos), 5.0f);

        auto cap = juce::Rectangle<float> ((float) w * 0.8f, 14.0f)
                       .withCentre ({ (float) x + (float) w * 0.5f, pos });
        g.setColour (theme::text);
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (theme::bg);
        g.drawLine (cap.getX() + 4, cap.getCentreY(), cap.getRight() - 4, cap.getCentreY(), 2.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const float size = (float) juce::jmin (w, h) - 6.0f;
        auto c = juce::Rectangle<float> (size, size).withCentre ({ (float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f });
        const float lineW = juce::jmax (3.0f, size * 0.09f);
        const float radius = size * 0.5f - lineW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.getCentreX(), c.getCentreY(), radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::bg);
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc runs from the default position (e.g. 50 % for pan/tilt) to the value
        const auto defVar = s.getProperties()["defaultPos"];
        const float defPos = defVar.isVoid() ? 0.0f : juce::jlimit (0.0f, 1.0f, (float) (double) defVar);
        const float defAngle = startAngle + defPos * (endAngle - startAngle);
        juce::Path arc;
        arc.addCentredArc (c.getCentreX(), c.getCentreY(), radius, radius, 0.0f,
                           juce::jmin (defAngle, angle), juce::jmax (defAngle, angle), true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (! defVar.isVoid())
        {
            // default marker tick outside the arc
            auto centre = c.getCentre();
            g.setColour (theme::text.withAlpha (0.7f));
            g.drawLine ({ centre.getPointOnCircumference (radius + lineW * 0.5f + 1.0f, defAngle),
                          centre.getPointOnCircumference (radius + lineW * 0.5f + 5.0f, defAngle) }, 2.0f);
        }

        auto knob = c.reduced (lineW * 1.8f);
        g.setColour (theme::panel2.brighter (0.15f));
        g.fillEllipse (knob);
        g.setColour (theme::line.brighter (0.2f));
        g.drawEllipse (knob, 1.0f);

        const float r2 = knob.getWidth() * 0.5f;
        juce::Point<float> centre = knob.getCentre();
        juce::Point<float> tip = centre.getPointOnCircumference (r2 - 3.0f, angle);
        juce::Point<float> inner = centre.getPointOnCircumference (r2 * 0.35f, angle);
        g.setColour (theme::text);
        g.drawLine ({ inner, tip }, 2.5f);
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        return s.isVertical() ? 7 : LookAndFeel_V4::getSliderThumbRadius (s);
    }
};

/** Small helpers for building panels. */
namespace ui
{
    inline void styleHeader (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (theme::font (13.0f, true));
        l.setColour (juce::Label::textColourId, theme::textDim);
    }

    inline void styleField (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (theme::font (14.0f));
        l.setColour (juce::Label::textColourId, theme::textDim);
        l.setJustificationType (juce::Justification::centredLeft);
    }

    inline void styleEditor (juce::TextEditor& e, bool numeric)
    {
        e.setFont (theme::font (15.0f));
        e.setIndents (10, 5);
        e.setJustification (juce::Justification::centredLeft);
        if (numeric)
            e.setInputRestrictions (6, "0123456789");
    }

    inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
}

} // namespace pl
