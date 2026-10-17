#pragma once
#include "Widgets.h"
#include "Engine/FixtureTemplates.h"

namespace pl
{

/** Mini layout grid for pixel fixtures: drag the pixels where they really are
    (rings, odd shapes, zig-zag wiring). Numbers are the DMX order. The Layout
    view, pixel maps and the 3D view use this shape. */
class CellLayoutEditor : public juce::Component
{
public:
    CellLayoutEditor (const FixtureType& type)
        : cellsX (juce::jmax (1, type.cellsX)), cellsY (juce::jmax (1, type.cellsY)), uniform (type.cellsAreUniform())
    {
        for (int c = 1; c <= type.cellCount(); ++c)
        {
            auto [x, y] = type.cellOffset (c);
            positions.push_back ({ x / FixtureType::cellSpacing, y / FixtureType::cellSpacing });
        }

        struct Tool { const char* name; std::function<void()> fn; const char* tip; };
        const std::vector<Tool> tools {
            { "Grid",       [this] { set (cellLayouts::grid (cellsX, cellsY)); },    "Rows, left to right" },
            { "Snake",      [this] { set (cellLayouts::snake (cellsX, cellsY)); },   "Rows back and forth (zig-zag wiring)" },
            { "Columns",    [this] { set (cellLayouts::columns (cellsX, cellsY)); }, "Columns, top to bottom" },
            { "Ring",       [this] { set (cellLayouts::ring (count(), true)); },     "A circle, from the top, clockwise" },
            { "Ring CCW",   [this] { set (cellLayouts::ring (count(), false)); },    "A circle, from the top, anticlockwise" },
            { "Rings...",   [this] { askRings(); },                                   "Any number of rings, with how many pixels each (outer first)" },
            { "Flip H",     [this] { for (auto& p : positions) p.first = -p.first; changed(); }, "Mirror left-right" },
            { "Flip V",     [this] { for (auto& p : positions) p.second = -p.second; changed(); }, "Mirror top-bottom" },
            { "Rotate 90",  [this] { for (auto& p : positions) p = { -p.second, p.first }; changed(); }, "Turn the shape" },
            { "Reverse",    [this] { std::reverse (positions.begin(), positions.end()); changed(); }, "Pixel 1 becomes the last one" },
        };
        for (auto& t : tools)
        {
            auto* b = buttons.add (new juce::TextButton (t.name));
            b->setTooltip (t.tip);
            b->onClick = t.fn;
            addAndMakeVisible (b);
        }
        hint.setText ("Drag pixels to where they are on the fixture. Numbers are the DMX order. Snaps to half a pixel (hold Shift for free).",
                      juce::dontSendNotification);
        hint.setFont (theme::font (12.5f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (hint);
    }

    /** Called with the new layout (cell units) on every change. */
    std::function<void (const std::vector<std::pair<float, float>>&)> onChange;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        ui::paintPanel (g, canvas);
        // half-pixel grid
        const float step = scale * 0.5f;
        g.setColour (theme::line.withAlpha (0.4f));
        const auto o = toScreen ({ 0.0f, 0.0f });
        for (float x = o.x - std::floor ((o.x - (float) canvas.getX()) / step) * step; x < (float) canvas.getRight(); x += step)
            for (float y = o.y - std::floor ((o.y - (float) canvas.getY()) / step) * step; y < (float) canvas.getBottom(); y += step)
                g.fillRect (x, y, 1.5f, 1.5f);

        // wiring order
        juce::Path wire;
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const auto p = toScreen (positions[i]);
            if (i == 0) wire.startNewSubPath (p); else wire.lineTo (p);
        }
        g.setColour (theme::accent.withAlpha (0.35f));
        g.strokePath (wire, juce::PathStrokeType (1.5f));

        const float r = juce::jlimit (6.0f, 22.0f, scale * 0.42f);
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const auto p = toScreen (positions[i]);
            const bool active = (int) i == dragging;
            g.setColour (i == 0 ? theme::ok : theme::familyColour (Family::Color).withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (p));
            g.setColour (active ? theme::text : theme::bg);
            g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (p), active ? 2.0f : 1.0f);
            g.setColour (theme::bg);
            g.setFont (theme::font (juce::jlimit (8.0f, 13.0f, r * 0.8f), true));
            g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (r * 2, r * 2).withCentre (p), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (theme::gap);
        auto bar = r.removeFromTop (34);
        const int w = (bar.getWidth() - 4 * (buttons.size() - 1)) / juce::jmax (1, buttons.size());
        for (auto* b : buttons) { b->setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (4); }
        r.removeFromTop (6);
        hint.setBounds (r.removeFromBottom (24));
        canvas = r;
        fit();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = -1;
        float best = 1e9f;
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const float d = toScreen (positions[i]).getDistanceFrom (e.position);
            if (d < best && d < scale * 0.6f) { best = d; dragging = (int) i; }
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto p = fromScreen (e.position);
        if (! e.mods.isShiftDown()) p = { std::round (p.first * 2.0f) / 2.0f, std::round (p.second * 2.0f) / 2.0f };
        positions[(size_t) dragging] = p;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0) changed (false);
        dragging = -1;
        repaint();
    }

private:
    int count() const { return (int) positions.size(); }

    void askRings()
    {
        // suggest: outer rings get more pixels
        const int total = count();
        juce::String suggestion;
        if (total >= 6) { const int outer = juce::roundToInt ((float) total * 2.0f / 3.0f); suggestion << outer << ", " << (total - outer); }
        else suggestion << total;

        auto* aw = new juce::AlertWindow ("Rings", "Pixels in each ring, outer ring first, separated by commas.\n"
                                          "The number of entries is the number of rings; a ring of 1 is a centre pixel.\n"
                                          "Example: 24, 16, 8, 1\n\nThis fixture has " + juce::String (total) + " pixels. "
                                          "A different total changes the fixture's pixel count (cell 1's channels are repeated).",
                                          juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor ("rings", suggestion, "Pixels per ring");
        aw->addTextEditor ("start", "1", "Starting at pixel (for a second pixel set, e.g. the white cells)");
        aw->addComboBox ("dir", { "Clockwise", "Anticlockwise" }, "Direction");
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<CellLayoutEditor> (this), aw] (int r)
        {
            if (safe == nullptr || r != 1) return;
            std::vector<int> perRing;
            for (auto& t : juce::StringArray::fromTokens (aw->getTextEditorContents ("rings"), ", ;", {}))
                if (t.getIntValue() > 0) perRing.push_back (juce::jlimit (1, 512, t.getIntValue()));
            if (perRing.empty()) return;
            const bool cw = aw->getComboBoxComponent ("dir") == nullptr || aw->getComboBoxComponent ("dir")->getSelectedItemIndex() == 0;
            auto p = cellLayouts::rings (perRing, cw);
            const int start = juce::jlimit (1, juce::jmax (1, safe->count()), aw->getTextEditorContents ("start").getIntValue());
            if (start > 1 || (! safe->uniform && (int) p.size() < safe->count()))
            {
                // fill just these pixels; the others keep their places
                auto all = safe->positions;
                for (size_t i = 0; i < p.size() && start - 1 + (int) i < (int) all.size(); ++i) all[(size_t) start - 1 + i] = p[i];
                safe->set (std::move (all));
                return;
            }
            if ((int) p.size() != safe->count())
            {
                if (! safe->uniform)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rings",
                        "This fixture has different pixel sets, so its pixel count can't change here: the rings add up to "
                        + juce::String ((int) p.size()) + ", it has " + juce::String (safe->count()) + ".");
                    return;
                }
                safe->cellsX = (int) p.size(); safe->cellsY = 1;
            }
            safe->set (std::move (p));
        }), true);
    }
    void set (std::vector<std::pair<float, float>> p) { positions = std::move (p); changed(); }

    void changed (bool refit = true)
    {
        // keep it centred (same as the fixture type stores it)
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (auto& [x, y] : positions) { x0 = juce::jmin (x0, x); x1 = juce::jmax (x1, x); y0 = juce::jmin (y0, y); y1 = juce::jmax (y1, y); }
        for (auto& [x, y] : positions) { x -= (x0 + x1) * 0.5f; y -= (y0 + y1) * 0.5f; }
        if (refit) fit();
        repaint();
        if (onChange) onChange (positions);
    }

    void fit()
    {
        float ext = 2.0f;
        for (auto& [x, y] : positions) ext = juce::jmax (ext, std::abs (x) * 2.0f + 2.0f, std::abs (y) * 2.0f + 2.0f);
        scale = juce::jmin ((float) canvas.getWidth(), (float) canvas.getHeight()) / ext;
    }

    juce::Point<float> toScreen (std::pair<float, float> p) const
    {
        return { (float) canvas.getCentreX() + p.first * scale, (float) canvas.getCentreY() + p.second * scale };
    }
    std::pair<float, float> fromScreen (juce::Point<float> s) const
    {
        return { (s.x - (float) canvas.getCentreX()) / scale, (s.y - (float) canvas.getCentreY()) / scale };
    }

    int cellsX, cellsY;
    bool uniform = true;          // one pixel set (the rings may change the pixel count)
    std::vector<std::pair<float, float>> positions;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Label hint;
    juce::Rectangle<int> canvas;
    float scale = 30.0f;
    int dragging = -1;
};

} // namespace pl
