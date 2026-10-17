#pragma once
#include "Theme.h"

namespace pl
{

/** Gel-style quick colours used by the swatch strip and the picker. */
inline const std::vector<std::pair<juce::String, juce::Colour>>& colourSwatches()
{
    static const std::vector<std::pair<juce::String, juce::Colour>> s {
        { "White",      juce::Colour (0xffffffff) }, { "Warm",      juce::Colour (0xffffc98a) },
        { "Red",        juce::Colour (0xffff0000) }, { "Orange",    juce::Colour (0xffff5a00) },
        { "Amber",      juce::Colour (0xffffa000) }, { "Yellow",    juce::Colour (0xffffff00) },
        { "Lime",       juce::Colour (0xff9dff00) }, { "Green",     juce::Colour (0xff00ff00) },
        { "Cyan",       juce::Colour (0xff00ffff) }, { "Light blue",juce::Colour (0xff4aa8ff) },
        { "Blue",       juce::Colour (0xff0000ff) }, { "Congo",     juce::Colour (0xff4b00ff) },
        { "Magenta",    juce::Colour (0xffff00ff) }, { "Pink",      juce::Colour (0xffff4fa0) },
    };
    return s;
}

/** Hue/saturation wheel. Drag to pick; reports continuously. */
class ColourWheel : public juce::Component
{
public:
    std::function<void (float hue, float sat)> onChange;

    void setHueSat (float h, float s) { hue = h; sat = s; repaint(); }

    /** Colours of the selected fixtures, drawn as numbered points (shows fans and gradients). */
    void setFixturePoints (std::vector<juce::Colour> pts)
    {
        if (pts != points) { points = std::move (pts); repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = wheelArea();
        if (cache.getWidth() != (int) area.getWidth())
            renderWheel ((int) area.getWidth());
        g.drawImageAt (cache, (int) area.getX(), (int) area.getY());

        const float r = area.getWidth() * 0.5f;
        auto posOf = [&] (juce::Colour c)
        {
            return area.getCentre().getPointOnCircumference (c.getSaturation() * r, c.getHue() * juce::MathConstants<float>::twoPi);
        };

        // selected fixtures in selection order: a path through their colours
        if (points.size() > 1)
        {
            juce::Path path;
            path.startNewSubPath (posOf (points.front()));
            for (size_t i = 1; i < points.size(); ++i) path.lineTo (posOf (points[i]));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (path, juce::PathStrokeType (3.0f));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }
        for (size_t i = 0; i < points.size(); ++i)
        {
            auto dot = juce::Rectangle<float> (18.0f, 18.0f).withCentre (posOf (points[i]));
            g.setColour (points[i].withBrightness (1.0f));
            g.fillEllipse (dot);
            g.setColour (juce::Colours::black);
            g.drawEllipse (dot, 1.5f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String ((int) i + 1), dot, juce::Justification::centred);
        }

        auto p = area.getCentre().getPointOnCircumference (sat * r, hue * juce::MathConstants<float>::twoPi);
        g.setColour (juce::Colours::black);
        g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p), 3.0f);
        g.setColour (juce::Colours::white);
        g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p), 1.5f);
    }

    void mouseDown (const juce::MouseEvent& e) override { pick (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { pick (e.position); }

private:
    juce::Rectangle<float> wheelArea() const
    {
        const float d = (float) juce::jmin (getWidth(), getHeight()) - 4.0f;
        return juce::Rectangle<float> (d, d).withCentre (getLocalBounds().toFloat().getCentre());
    }

    void renderWheel (int d)
    {
        cache = juce::Image (juce::Image::ARGB, d, d, true);
        const float r = (float) d * 0.5f;
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
            {
                const float dx = (float) x - r + 0.5f, dy = (float) y - r + 0.5f;
                const float dist = std::sqrt (dx * dx + dy * dy);
                if (dist > r) continue;
                float h = std::atan2 (dx, -dy) / juce::MathConstants<float>::twoPi;
                if (h < 0) h += 1.0f;
                auto c = juce::Colour::fromHSV (h, dist / r, 1.0f, 1.0f);
                const float edge = juce::jlimit (0.0f, 1.0f, r - dist);   // anti-alias the rim
                cache.setPixelAt (x, y, c.withAlpha (edge));
            }
    }

    void pick (juce::Point<float> p)
    {
        auto area = wheelArea();
        const auto rel = p - area.getCentre();
        const float r = area.getWidth() * 0.5f;
        sat = juce::jlimit (0.0f, 1.0f, std::hypot (rel.x, rel.y) / r);
        hue = std::atan2 (rel.x, -rel.y) / juce::MathConstants<float>::twoPi;
        if (hue < 0) hue += 1.0f;
        repaint();
        if (onChange) onChange (hue, sat);
    }

    juce::Image cache;
    float hue = 0.0f, sat = 0.0f;
    std::vector<juce::Colour> points;
};

/** Colour picker popup: wheel, brightness, gel swatches. */
class ColorPickerPanel : public juce::Component,
                         private juce::Timer
{
public:
    /** Called with the picked colour (full brightness scaled by the fader). */
    std::function<void (juce::Colour)> onPick;

    /** Returns the selected fixtures' current colours, in selection order. */
    std::function<std::vector<juce::Colour>()> getFixtureColours;

    ColorPickerPanel()
    {
        wheel.onChange = [this] (float h, float s) { hue = h; sat = s; emit(); };
        addAndMakeVisible (wheel);

        brightness.setSliderStyle (juce::Slider::LinearVertical);
        brightness.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        brightness.setRange (0.0, 1.0, 0.0);
        brightness.setValue (1.0, juce::dontSendNotification);
        brightness.setColour (juce::Slider::trackColourId, juce::Colours::white);
        brightness.onValueChange = [this] { emit(); };
        addAndMakeVisible (brightness);

        for (auto& [name, colour] : colourSwatches())
        {
            auto* b = swatchButtons.add (new juce::TextButton());
            b->setColour (juce::TextButton::buttonColourId, colour);
            b->setTooltip (name);
            b->onClick = [this, c = colour]
            {
                hue = c.getHue(); sat = c.getSaturation();
                wheel.setHueSat (hue, sat);
                emit (c.withMultipliedBrightness ((float) brightness.getValue()));
            };
            addAndMakeVisible (b);
        }

        hint.setFont (theme::font (12.5f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setText ("Numbered points are the selected fixtures (selection order). With Fan on, a pick sets the far end of a gradient.",
                      juce::dontSendNotification);
        addAndMakeVisible (hint);
        setSize (440, 420);
        startTimerHz (15);
    }

    void timerCallback() override
    {
        if (getFixtureColours) wheel.setFixturePoints (getFixtureColours());
    }

    void setColour (juce::Colour c)
    {
        hue = c.getHue(); sat = c.getSaturation();
        wheel.setHueSat (hue, sat);
        brightness.setValue (juce::jmax (0.05f, c.getBrightness()), juce::dontSendNotification);
        current = c;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (current);
        g.fillRoundedRectangle (previewArea.toFloat(), 6.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (previewArea.toFloat(), 6.0f, 1.0f);
        g.setColour (current.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        g.setFont (theme::font (13.0f, true));
        g.drawText ("#" + current.toDisplayString (false), previewArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        hint.setBounds (r.removeFromBottom (36));
        r.removeFromBottom (6);
        auto sw = r.removeFromBottom (64);
        const int cols = 7, w = (sw.getWidth() - (cols - 1) * 4) / cols, h = 30;
        for (int i = 0; i < swatchButtons.size(); ++i)
            swatchButtons[i]->setBounds (sw.getX() + (i % cols) * (w + 4), sw.getY() + (i / cols) * (h + 4), w, h);
        r.removeFromBottom (8);
        auto right = r.removeFromRight (70);
        previewArea = right.removeFromTop (40);
        right.removeFromTop (8);
        brightness.setBounds (right.reduced (16, 0));
        r.removeFromRight (8);
        wheel.setBounds (r);
    }

private:
    void emit() { emit (juce::Colour::fromHSV (hue, sat, (float) brightness.getValue(), 1.0f)); }
    void emit (juce::Colour c)
    {
        current = c;
        repaint (previewArea);
        if (onPick) onPick (c);
    }

    ColourWheel wheel;
    juce::Slider brightness;
    juce::OwnedArray<juce::TextButton> swatchButtons;
    juce::Label hint;
    juce::Rectangle<int> previewArea;
    juce::Colour current { juce::Colours::white };
    float hue = 0.0f, sat = 0.0f;
};

} // namespace pl
