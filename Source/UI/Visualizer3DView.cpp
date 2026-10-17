#include "Visualizer3DView.h"

namespace pl
{

//==============================================================================
/** Position and mount of the selected fixtures, plus quick placement tools. */
class Visualizer3DView::PlacePanel : public juce::Component
{
public:
    explicit PlacePanel (Visualizer3DView& o) : owner (o)
    {
        title.setFont (theme::font (13.0f, true));
        title.setColour (juce::Label::textColourId, theme::familyColour (Family::Position));
        addAndMakeVisible (title);
        const char* names[] { "X m", "Height", "Z m", "Tip", "Turn", "Roll" };
        for (int i = 0; i < 6; ++i)
        {
            auto* l = labels.add (new juce::Label ({}, names[i]));
            l->setFont (theme::font (12.0f));
            l->setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (l);
            auto* e = edits.add (new juce::TextEditor());
            ui::styleEditor (*e, false);
            e->setInputRestrictions (8, "-0123456789.");
            e->onReturnKey = [this, i] { apply (i); };
            e->onFocusLost = [this, i] { apply (i); };
            addAndMakeVisible (e);
        }
        struct Tool { const char* name; const char* tip; std::function<void()> fn; };
        auto a = [this] (const char* how) { return [this, how] { owner.arrange3D (how); }; };
        const std::vector<Tool> tools {
            { "Hang",        "Beam points down (truss)",          [this] { owner.applyToSelection ([] (PatchedFixture& f) { f.rx3 = 0; f.rz3 = 0; }); } },
            { "Floor",       "Standing on the floor, beam up",    [this] { owner.applyToSelection ([] (PatchedFixture& f) { f.rx3 = 180; f.rz3 = 0; f.y3 = 0.2f; }); } },
            { "Front",       "Beam towards the audience (+Z)",    [this] { owner.applyToSelection ([] (PatchedFixture& f) { f.rx3 = -90; f.rz3 = 0; }); } },
            { "Align X",     "Same X as the first selected fixture", a ("alignX") },
            { "Align Y",     "Same height as the first selected fixture", a ("alignY") },
            { "Align Z",     "Same Z (depth) as the first selected fixture", a ("alignZ") },
            { "Even X",      "Space evenly left to right (selection order)", a ("evenX") },
            { "Even Y",      "Space evenly in height (selection order)", a ("evenY") },
            { "Even Z",      "Space evenly front to back (selection order)", a ("evenZ") },
            { "Line",        "A straight line from the first to the last selected, evenly", a ("line") },
            { "Circle",      "A circle around the selection's centre (selection order)", a ("circle") },
            { "Arc",         "A half circle, opening towards the audience", a ("arc") },
            { "Spread +",    "Further apart (from the centre)", a ("spread+") },
            { "Spread -",    "Closer together", a ("spread-") },
            { "Mirror X",    "Mirror left-right around the centre", a ("mirrorX") },
            { "Turn +45",    "Turn each fixture 45 degrees", [this] { owner.applyToSelection ([] (PatchedFixture& f) { f.ry3 = std::fmod (f.ry3 + 45.0f, 360.0f); }); } },
            { "Face centre", "Turn each fixture to face the selection's centre", a ("faceCentre") },
            { "From layout", "Use the 2D layout for X / Z",       [this] { owner.applyToSelection ([] (PatchedFixture& f) { f.x3 = f.layoutX / 50.0f; f.z3 = f.layoutY / 50.0f; }); } },
        };
        for (auto& t : tools)
        {
            auto* b = buttons.add (new juce::TextButton (t.name));
            b->setTooltip (t.tip);
            b->onClick = t.fn;
            addAndMakeVisible (b);
        }
        hint.setText ("Drag the arrows to move along X (red), Y (green, height) or Z (blue); drag the ring to turn the selection. "
                      "Or drag fixtures (Shift: up / down). Align / Even use the selection order.",
                      juce::dontSendNotification);
        hint.setFont (theme::font (11.5f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (hint);
    }

    void show (const std::vector<const PatchedFixture*>& sel)
    {
        title.setText (sel.empty() ? juce::String ("PLACE - select fixtures") : "PLACE - " + juce::String ((int) sel.size()) + " selected",
                       juce::dontSendNotification);
        for (int i = 0; i < 6; ++i)
        {
            auto* e = edits[i];
            e->setEnabled (! sel.empty());
            if (e->hasKeyboardFocus (true)) continue;
            if (sel.empty()) { e->clear(); continue; }
            const auto p = stage3d::position (*sel.front());
            const float v[] { p.x, p.y, p.z, sel.front()->rx3, sel.front()->ry3, sel.front()->rz3 };
            bool same = true;
            for (auto* f : sel)
            {
                const auto q = stage3d::position (*f);
                const float w[] { q.x, q.y, q.z, f->rx3, f->ry3, f->rz3 };
                same &= std::abs (w[i] - v[i]) < 0.001f;
            }
            e->setText (same ? juce::String (v[i], 2) : juce::String(), false);
            e->setTextToShowWhenEmpty (same ? juce::String() : juce::String ("mixed"), theme::textDim);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (theme::familyColour (Family::Position));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        for (int i = 0; i < 3; ++i)          // X | Tip, Y | Turn, Z | Roll
        {
            auto row = r.removeFromTop (26);
            auto left = row.removeFromLeft (row.getWidth() / 2 - 3);
            row.removeFromLeft (6);
            labels[i]->setBounds (left.removeFromLeft (54));
            edits[i]->setBounds (left);
            labels[i + 3]->setBounds (row.removeFromLeft (54));
            edits[i + 3]->setBounds (row);
            r.removeFromTop (4);
        }
        r.removeFromTop (2);
        for (int i = 0; i < buttons.size(); i += 3)
        {
            auto row = r.removeFromTop (26);
            const int w = (row.getWidth() - 8) / 3;
            for (int k = 0; k < 3 && i + k < buttons.size(); ++k) { buttons[i + k]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); }
            r.removeFromTop (3);
        }
        hint.setBounds (r);
    }

private:
    void apply (int i)
    {
        const auto text = edits[i]->getText().trim();
        if (text.isEmpty()) return;
        const float v = text.getFloatValue();
        owner.applyToSelection ([i, v] (PatchedFixture& f)
        {
            if (! f.has3D)
            {
                const auto p = stage3d::position (f);
                f.x3 = p.x; f.y3 = p.y; f.z3 = p.z;
            }
            switch (i)
            {
                case 0: f.x3 = v; break;
                case 1: f.y3 = v; break;
                case 2: f.z3 = v; break;
                case 3: f.rx3 = v; break;
                case 4: f.ry3 = v; break;
                default: f.rz3 = v; break;
            }
        });
    }

    Visualizer3DView& owner;
    juce::Label title, hint;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextEditor> edits;
    juce::OwnedArray<juce::TextButton> buttons;
};

//==============================================================================
Visualizer3DView::Visualizer3DView (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    frameButton.onClick = [this] { frameAll(); };
    frontButton.onClick = [this] { cam.yaw = 0.0f; cam.pitch = 8.0f; repaint(); };
    topButton.onClick   = [this] { cam.yaw = 0.0f; cam.pitch = 85.0f; repaint(); };
    sideButton.onClick  = [this] { cam.yaw = 90.0f; cam.pitch = 10.0f; repaint(); };
    placeButton.setClickingTogglesState (true);
    placeButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Position));
    placeButton.setTooltip ("Place fixtures: drag them, or type positions and mounts");
    placeButton.onClick = [this] { panel->setVisible (placeButton.getToggleState()); refreshPanel(); };
    hazeButton.setClickingTogglesState (true);
    hazeButton.setToggleState (true, juce::dontSendNotification);
    hazeButton.setTooltip ("Show beams in the air (haze)");
    beamSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    beamSlider.setRange (0.05, 1.0, 0.0);
    beamSlider.setValue (0.45, juce::dontSendNotification);
    beamSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    beamSlider.setTooltip ("Beam brightness (how thick the haze is)");
    hint.setFont (theme::font (12.0f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::centredRight);
    hint.setText ("Drag: orbit (Select: box)  -  right-drag: pan  -  scroll: zoom  -  click / Shift-click fixtures", juce::dontSendNotification);
    selectButton.setClickingTogglesState (true);
    selectButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    selectButton.setTooltip ("Select tool: drag a box around fixtures (Shift / Cmd adds). Alt-drag orbits. "
                             "Without it, Shift-drag on empty space draws a box too");
    selectButton.onClick = [this] { repaint(); };
    allButton.setTooltip ("Select every fixture");
    allButton.onClick = [this]
    {
        { const juce::ScopedLock sl (engine.dataLock); std::vector<int> all; for (auto& fx : fixtures) all.push_back (fx.f.fid); engine.programmer.setSelection (all); }
        ui.changed(); refreshPanel(); repaint();
    };
    noneButton.onClick = [this] { { const juce::ScopedLock sl (engine.dataLock); engine.programmer.clearSelection(); } ui.changed(); refreshPanel(); repaint(); };
    groupsButton.setTooltip ("Select a group");
    groupsButton.onClick = [this] { groupsMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { &frameButton, &frontButton, &topButton, &sideButton, &placeButton,
                                                            &hazeButton, &beamSlider, &hint, &selectButton, &allButton, &noneButton, &groupsButton })
        addAndMakeVisible (c);
    panel = std::make_unique<PlacePanel> (*this);
    addChildComponent (panel.get());

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    rebuild();
    startTimerHz (30);
}

Visualizer3DView::~Visualizer3DView()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void Visualizer3DView::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &engine) rebuild();
    refreshPanel();
    repaint();
}

void Visualizer3DView::timerCallback()
{
    if (isShowing()) repaint();
}

void Visualizer3DView::rebuild()
{
    fixtures.clear();
    const juce::ScopedLock sl (engine.dataLock);
    for (auto& f : engine.patch.fixtures())
    {
        Fixture3 fx;
        fx.f = f;
        fx.type = engine.library.find (f.typeId);
        fx.pos = stage3d::position (f);
        if (fx.type != nullptr)
            for (auto& a : fx.type->attributes)
            {
                fx.hasPanTilt |= a.attribute == "Pan" || a.attribute == "Tilt";
                fx.hasZoom |= a.attribute == "Zoom";
            }
        fixtures.push_back (fx);
    }
    if (! framed && ! fixtures.empty() && getWidth() > 0) { framed = true; frameAll(); }
}

void Visualizer3DView::frameAll()
{
    if (fixtures.empty()) return;
    V3 lo { 1e9f, 1e9f, 1e9f }, hi { -1e9f, -1e9f, -1e9f };
    for (auto& f : fixtures)
    {
        lo = { juce::jmin (lo.x, f.pos.x), juce::jmin (lo.y, f.pos.y), juce::jmin (lo.z, f.pos.z) };
        hi = { juce::jmax (hi.x, f.pos.x), juce::jmax (hi.y, f.pos.y), juce::jmax (hi.z, f.pos.z) };
    }
    cam.target = { (lo.x + hi.x) * 0.5f, juce::jmax (1.0f, (lo.y + hi.y) * 0.35f), (lo.z + hi.z) * 0.5f };
    const float size = juce::jmax (6.0f, hi.x - lo.x, hi.z - lo.z, hi.y);
    cam.distance = size * 1.5f + 4.0f;
    repaint();
}

//==============================================================================
V3 Visualizer3DView::cameraPosition() const
{
    const float yaw = juce::degreesToRadians (cam.yaw), pitch = juce::degreesToRadians (cam.pitch);
    return cam.target + V3 { std::sin (yaw) * std::cos (pitch), std::sin (pitch), std::cos (yaw) * std::cos (pitch) } * cam.distance;
}

void Visualizer3DView::basis (V3& right, V3& up, V3& forward) const
{
    forward = (cam.target - cameraPosition()).normalised();
    right = forward.cross ({ 0.0f, 1.0f, 0.0f }).normalised();
    up = right.cross (forward);
}

juce::Rectangle<float> Visualizer3DView::viewArea() const
{
    return getLocalBounds().toFloat().reduced ((float) theme::gap).withTrimmedTop ((float) (theme::buttonH + theme::gap));
}

bool Visualizer3DView::project (V3 p, juce::Point<float>& out, float* depth) const
{
    V3 right, up, forward;
    basis (right, up, forward);
    const auto rel = p - cameraPosition();
    const float z = rel.dot (forward);
    if (depth != nullptr) *depth = z;
    if (z < 0.05f) return false;
    const auto area = viewArea();
    const float f = area.getHeight() * 0.5f / std::tan (juce::degreesToRadians (cam.fov) * 0.5f);
    out = { area.getCentreX() + rel.dot (right) / z * f, area.getCentreY() - rel.dot (up) / z * f };
    return true;
}

V3 Visualizer3DView::rayDirection (juce::Point<float> s) const
{
    V3 right, up, forward;
    basis (right, up, forward);
    const auto area = viewArea();
    const float f = area.getHeight() * 0.5f / std::tan (juce::degreesToRadians (cam.fov) * 0.5f);
    return (forward + right * ((s.x - area.getCentreX()) / f) + up * ((area.getCentreY() - s.y) / f)).normalised();
}

//==============================================================================
static juce::Path convexHull (std::vector<juce::Point<float>> pts)
{
    juce::Path path;
    if (pts.size() < 3) return path;
    std::sort (pts.begin(), pts.end(), [] (auto a, auto b) { return a.x < b.x || (! (b.x < a.x) && a.y < b.y); });
    std::vector<juce::Point<float>> h (pts.size() * 2);
    size_t k = 0;
    auto cross = [] (juce::Point<float> o, juce::Point<float> a, juce::Point<float> b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    for (size_t i = 0; i < pts.size(); ++i) { while (k >= 2 && cross (h[k - 2], h[k - 1], pts[i]) <= 0) --k; h[k++] = pts[i]; }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) { while (k >= t && cross (h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k; h[k++] = pts[i - 1]; }
    h.resize (k > 0 ? k - 1 : 0);
    if (h.size() < 3) return path;
    path.startNewSubPath (h[0]);
    for (size_t i = 1; i < h.size(); ++i) path.lineTo (h[i]);
    path.closeSubPath();
    return path;
}

void Visualizer3DView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    const auto area = viewArea();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b0d12), area.getCentreX(), area.getY(),
                                                 juce::Colour (0xff15181f), area.getCentreX(), area.getBottom(), false));
        g.fillRect (area);

        // floor grid (1 m)
        const int extent = 14;
        const V3 c { std::round (cam.target.x), 0.0f, std::round (cam.target.z) };
        for (int i = -extent; i <= extent; ++i)
            for (int axis = 0; axis < 2; ++axis)
            {
                const int steps = 2 * extent;
                juce::Path line;
                bool drawing = false;
                for (int s = 0; s <= steps; ++s)
                {
                    const float a = (float) (s - extent);
                    const V3 p = axis == 0 ? V3 { c.x + (float) i, 0.0f, c.z + a } : V3 { c.x + a, 0.0f, c.z + (float) i };
                    juce::Point<float> sp;
                    if (project (p, sp)) { if (drawing) line.lineTo (sp); else line.startNewSubPath (sp); drawing = true; }
                    else drawing = false;
                }
                g.setColour (theme::line.brighter (0.4f).withAlpha (i == 0 ? 0.6f : 0.3f));
                g.strokePath (line, juce::PathStrokeType (i == 0 ? 1.2f : 0.8f));
            }

        std::vector<int> sel;
        { const juce::ScopedLock sl (engine.dataLock); sel = engine.programmer.selection(); }
        auto isSelected = [&sel] (int fid)
        {
            for (int t : sel) if (targetFid (t) == fid) return true;
            return false;
        };

        const float haze = hazeButton.getToggleState() ? (float) beamSlider.getValue() : 0.0f;

        struct Body { float depth; juce::Point<float> p; float radius; juce::Colour colour; float level; int fid; bool selected; };
        std::vector<Body> bodies;

        for (auto& fx : fixtures)
        {
            if (fx.type == nullptr) continue;
            auto out = engine.getFixtureOutput (fx.f.fid);
            auto get = [&out] (const juce::String& k, float def) { auto i = out.find (k); return i != out.end() ? i->second : def; };
            const float level = juce::jlimit (0.0f, 1.0f, get ("Intensity", get ("Dimmer", 0.0f)));
            auto colourOf = [&] (const juce::String& sfx)
            {
                if (! out.count ("ColorR" + sfx) && ! out.count ("ColorC" + sfx)) return juce::Colours::white;
                float r = get ("ColorR" + sfx, 1.0f - get ("ColorC" + sfx, 0.0f)), gg = get ("ColorG" + sfx, 1.0f - get ("ColorM" + sfx, 0.0f)),
                      b = get ("ColorB" + sfx, 1.0f - get ("ColorY" + sfx, 0.0f)), w = get ("ColorW" + sfx, 0.0f);
                return juce::Colour::fromFloatRGBA (juce::jmin (1.0f, r + w), juce::jmin (1.0f, gg + w), juce::jmin (1.0f, b + w), 1.0f);
            };

            if (fx.type->hasCells())
            {
                // pixel fixtures: each pixel glows in the fixture's plane (no beams)
                for (int cell = 1; cell <= fx.type->cellCount(); ++cell)
                {
                    auto [dx, dy] = fx.type->cellOffset (cell);
                    const V3 local { dx / 100.0f * (fx.f.layoutMirror ? -1.0f : 1.0f), 0.0f, dy / 100.0f };
                    const V3 p = fx.pos + stage3d::mount (fx.f, stage3d::rotateY (local, -fx.f.layoutRotation));
                    juce::Point<float> sp;
                    float depth = 0.0f;
                    if (! project (p, sp, &depth)) continue;
                    const float cl = juce::jlimit (0.0f, 1.0f, get ("Dimmer@" + juce::String (cell), 1.0f) * level);
                    const float size = juce::jmax (2.0f, 0.12f / depth * area.getHeight());
                    bodies.push_back ({ depth, sp, size * 0.5f, colourOf ("@" + juce::String (cell)), cl, fx.f.fid, isSelected (fx.f.fid) });
                }
                continue;
            }

            const float pan = fx.hasPanTilt ? get ("Pan", 0.5f) : 0.5f, tilt = fx.hasPanTilt ? get ("Tilt", 0.5f) : 0.5f;
            const V3 dir = stage3d::beamDirection (fx.f, *fx.type, pan, tilt);
            const float angle = stage3d::beamAngle (*fx.type, fx.hasZoom ? get ("Zoom", 0.5f) : -1.0f);
            const auto colour = colourOf ({});
            juce::Point<float> apex;
            float depth = 0.0f;
            const bool apexVisible = project (fx.pos, apex, &depth);

            if (level > 0.01f)
            {
                const V3 end = stage3d::floorHit (fx.pos, dir, 25.0f);
                const float length = (end - fx.pos).length();
                const float radius = length * std::tan (juce::degreesToRadians (angle) * 0.5f);
                // two axes across the beam
                V3 a = dir.cross ({ 0.0f, 1.0f, 0.0f });
                if (a.length() < 0.01f) a = dir.cross ({ 1.0f, 0.0f, 0.0f });
                a = a.normalised();
                const V3 b = dir.cross (a).normalised();
                std::vector<juce::Point<float>> ring;
                juce::Path pool;
                bool poolStarted = false;
                for (int k = 0; k < 20; ++k)
                {
                    const float t = juce::MathConstants<float>::twoPi * (float) k / 20.0f;
                    const V3 edge = end + (a * std::cos (t) + b * std::sin (t)) * radius;
                    juce::Point<float> sp;
                    if (project (edge, sp)) ring.push_back (sp);
                    // the pool on the floor: the edge pushed along its own ray down to y = 0
                    const V3 rayDir = (edge - fx.pos).normalised();
                    const V3 floorPt = stage3d::floorHit (fx.pos, rayDir, 25.0f);
                    if (std::abs (floorPt.y) < 0.01f && project (floorPt, sp))
                    {
                        if (! poolStarted) { pool.startNewSubPath (sp); poolStarted = true; }
                        else pool.lineTo (sp);
                    }
                }
                juce::Point<float> endP;
                const bool endVisible = project (end, endP);
                if (poolStarted && std::abs (end.y) < 0.01f)
                {
                    pool.closeSubPath();
                    const auto pb = pool.getBounds();
                    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f * level), pb.getCentreX(), pb.getCentreY(),
                                                             colour.withAlpha (0.0f), pb.getRight(), pb.getCentreY(), true));
                    g.fillPath (pool);
                }
                if (haze > 0.0f && apexVisible && ring.size() > 2)
                {
                    ring.push_back (apex);
                    auto cone = convexHull (ring);
                    g.setGradientFill (juce::ColourGradient (colour.withAlpha (juce::jmin (0.9f, haze * level)), apex.x, apex.y,
                                                             colour.withAlpha (haze * level * 0.12f),
                                                             endVisible ? endP.x : apex.x, endVisible ? endP.y : apex.y, false));
                    g.fillPath (cone);
                }
            }
            if (apexVisible)
                bodies.push_back ({ depth, apex, juce::jlimit (3.0f, 18.0f, 0.22f / depth * area.getHeight()), colour, level,
                                    fx.f.fid, isSelected (fx.f.fid) });
        }

        // fixtures and pixels, far to near
        std::sort (bodies.begin(), bodies.end(), [] (const Body& x, const Body& y) { return x.depth > y.depth; });
        for (auto& b : bodies)
        {
            auto r = juce::Rectangle<float> (b.radius * 2.0f, b.radius * 2.0f).withCentre (b.p);
            g.setColour (juce::Colour (0xff2a2e38));
            g.fillEllipse (r);
            g.setColour (juce::Colour (0xff0b0d12).interpolatedWith (b.colour, b.level));
            g.fillEllipse (r.reduced (b.radius * 0.25f));
            if (b.selected)
            {
                g.setColour (theme::accent);
                g.drawEllipse (r.expanded (2.0f), 1.6f);
            }
        }
        // labels for whole fixtures (not every pixel)
        g.setFont (theme::font (10.5f, true));
        for (auto& fx : fixtures)
        {
            juce::Point<float> sp;
            if (! project (fx.pos, sp)) continue;
            g.setColour (isSelected (fx.f.fid) ? theme::accent : theme::textDim);
            g.drawText (juce::String (fx.f.fid), juce::Rectangle<float> (60.0f, 14.0f).withCentre (sp.translated (0.0f, -16.0f)),
                        juce::Justification::centred);
        }
        if (fixtures.empty())
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (15.0f));
            g.drawText ("Patch fixtures to see them here", area, juce::Justification::centred);
        }
    }
    if (drag == Drag::Box)
    {
        const auto box = juce::Rectangle<float> (downPos, last);
        g.setColour (theme::accent.withAlpha (0.15f));
        g.fillRect (box);
        g.setColour (theme::accent);
        g.drawRect (box, 1.5f);
    }
    if (placeButton.getToggleState())
    {
        paintGizmo (g);
        g.setColour (theme::familyColour (Family::Position));
        g.drawRoundedRectangle (area.reduced (1.0f), 8.0f, 2.0f);
    }
}

bool Visualizer3DView::gizmoCentre (V3& c) const
{
    const auto sel = selectedFixtures();
    if (sel.empty()) return false;
    V3 sum; int n = 0;
    for (auto& fx : fixtures)
        if (std::find (sel.begin(), sel.end(), fx.f.fid) != sel.end()) { sum = sum + fx.pos; ++n; }
    if (n == 0) return false;
    c = sum * (1.0f / (float) n);
    return true;
}

static const V3 gizmoAxes[3] { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

int Visualizer3DView::gizmoHit (juce::Point<float> p) const
{
    if (! placeButton.getToggleState()) return 0;
    V3 c;
    if (! gizmoCentre (c)) return 0;
    juce::Point<float> s0;
    if (! project (c, s0)) return 0;
    const float L = gizmoLength();
    for (int i = 0; i < 3; ++i)
    {
        juce::Point<float> s1;
        if (! project (c + gizmoAxes[i] * L, s1)) continue;
        const juce::Line<float> line (s0 + (s1 - s0) * 0.25f, s1);
        juce::Point<float> nearest;
        if (line.getDistanceFromPoint (p, nearest) < 7.0f || p.getDistanceFrom (s1) < 11.0f) return i + 1;
    }
    for (int k = 0; k < 48; ++k)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) k / 48.0f;
        juce::Point<float> sp;
        if (project (c + V3 { std::cos (a), 0.0f, std::sin (a) } * (L * 0.8f), sp) && sp.getDistanceFrom (p) < 7.0f) return 4;
    }
    return 0;
}

void Visualizer3DView::paintGizmo (juce::Graphics& g)
{
    V3 c;
    if (! gizmoCentre (c)) return;
    juce::Point<float> s0;
    if (! project (c, s0)) return;
    const float L = gizmoLength();
    const auto mouse = getMouseXYRelative().toFloat();
    const int hot = drag == Drag::Axis ? dragAxis : drag == Drag::Turn ? 4 : gizmoHit (mouse);

    // ring (turn)
    juce::Path ring;
    bool started = false;
    for (int k = 0; k <= 64; ++k)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) k / 64.0f;
        juce::Point<float> sp;
        if (! project (c + V3 { std::cos (a), 0.0f, std::sin (a) } * (L * 0.8f), sp)) { started = false; continue; }
        if (! started) { ring.startNewSubPath (sp); started = true; } else ring.lineTo (sp);
    }
    g.setColour (juce::Colour (0xfff2d23a).withAlpha (hot == 4 ? 1.0f : 0.6f));
    g.strokePath (ring, juce::PathStrokeType (hot == 4 ? 3.0f : 1.8f));

    const juce::Colour colours[3] { juce::Colour (0xffe5484d), juce::Colour (0xff3ccf7a), juce::Colour (0xff4f8cff) };
    const char* names[3] { "X", "Y", "Z" };
    for (int i = 0; i < 3; ++i)
    {
        juce::Point<float> s1;
        if (! project (c + gizmoAxes[i] * L, s1)) continue;
        const bool on = hot == i + 1;
        g.setColour (colours[i].withAlpha (on ? 1.0f : 0.85f));
        juce::Path arrow;
        arrow.addArrow ({ s0, s1 }, on ? 4.0f : 2.6f, 14.0f, 12.0f);
        g.fillPath (arrow);
        g.setFont (theme::font (11.0f, true));
        g.drawText (names[i], juce::Rectangle<float> (16.0f, 14.0f).withCentre (s1 + (s1 - s0) * 0.15f), juce::Justification::centred);
    }
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (s0));
}

void Visualizer3DView::arrange3D (const juce::String& how)
{
    const auto sel = selectedFixtures();
    if (sel.empty()) return;
    std::map<int, V3> pos;
    std::map<int, float> turn;
    for (auto& fx : fixtures)
        if (std::find (sel.begin(), sel.end(), fx.f.fid) != sel.end()) { pos[fx.f.fid] = fx.pos; turn[fx.f.fid] = fx.f.ry3; }
    std::vector<int> order;
    for (int fid : sel) if (pos.count (fid)) order.push_back (fid);
    const int n = (int) order.size();
    if (n == 0) return;
    V3 c;
    for (auto& [fid, p] : pos) c = c + p;
    c = c * (1.0f / (float) n);
    const V3 first = pos[order.front()], lastP = pos[order.back()];
    auto even = [&] (float V3::* axis)
    {
        if (n < 2) return;
        float lo = 1e9f, hi = -1e9f;
        for (auto& [fid, p] : pos) { lo = juce::jmin (lo, p.*axis); hi = juce::jmax (hi, p.*axis); }
        for (int i = 0; i < n; ++i) pos[order[(size_t) i]].*axis = lo + (hi - lo) * (float) i / (float) (n - 1);
    };
    float radius = 0.0f;
    for (auto& [fid, p] : pos) radius = juce::jmax (radius, std::hypot (p.x - c.x, p.z - c.z));
    radius = juce::jmax (radius, 0.5f * (float) n * 0.6f / juce::MathConstants<float>::pi);

    if (how == "alignX") for (auto& [fid, p] : pos) p.x = first.x;
    else if (how == "alignY") for (auto& [fid, p] : pos) p.y = first.y;
    else if (how == "alignZ") for (auto& [fid, p] : pos) p.z = first.z;
    else if (how == "evenX") even (&V3::x);
    else if (how == "evenY") even (&V3::y);
    else if (how == "evenZ") even (&V3::z);
    else if (how == "line" && n >= 2)
        for (int i = 0; i < n; ++i) pos[order[(size_t) i]] = first + (lastP - first) * ((float) i / (float) (n - 1));
    else if (how == "circle" || how == "arc")
    {
        const bool arc = how == "arc";
        for (int i = 0; i < n; ++i)
        {
            const float t = arc ? (n > 1 ? (float) i / (float) (n - 1) : 0.5f) : (float) i / (float) n;
            const float a = arc ? juce::MathConstants<float>::pi * (1.0f + t)            // left → back → right
                                : juce::MathConstants<float>::twoPi * t - juce::MathConstants<float>::halfPi;
            auto& p = pos[order[(size_t) i]];
            p.x = c.x + radius * std::cos (a);
            p.z = c.z + radius * std::sin (a);
        }
    }
    else if (how == "spread+" || how == "spread-")
    {
        const float k = how == "spread+" ? 1.15f : 1.0f / 1.15f;
        for (auto& [fid, p] : pos) { p.x = c.x + (p.x - c.x) * k; p.z = c.z + (p.z - c.z) * k; }
    }
    else if (how == "mirrorX")
        for (auto& [fid, p] : pos) p.x = c.x - (p.x - c.x);
    else if (how == "faceCentre")
        for (auto& [fid, p] : pos)
            if (std::hypot (c.x - p.x, c.z - p.z) > 0.01f)
                turn[fid] = juce::radiansToDegrees (std::atan2 (c.x - p.x, c.z - p.z));

    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& [fid, p] : pos)
            if (auto* f = engine.patch.find (fid))
                engine.patch.set3D (fid, std::round (p.x * 100.0f) / 100.0f, juce::jmax (0.0f, std::round (p.y * 100.0f) / 100.0f),
                                    std::round (p.z * 100.0f) / 100.0f, f->rx3, turn[fid], f->rz3);
    }
    rebuild();
    refreshPanel();
    repaint();
}

void Visualizer3DView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    auto bar = r.removeFromTop (theme::buttonH);
    for (auto* b : { &frameButton, &frontButton, &topButton, &sideButton })
    {
        b->setBounds (bar.removeFromLeft (bar.getWidth() > 1100 ? 70 : 58));
        bar.removeFromLeft (4);
    }
    bar.removeFromLeft (12);
    selectButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    allButton.setBounds (bar.removeFromLeft (46));
    bar.removeFromLeft (4);
    noneButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (4);
    groupsButton.setBounds (bar.removeFromLeft (72));
    bar.removeFromLeft (12);
    placeButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (12);
    hazeButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    beamSlider.setBounds (bar.removeFromLeft (140));
    bar.removeFromLeft (12);
    hint.setBounds (bar);
    hint.setVisible (bar.getWidth() > 220);
    const auto area = viewArea().toNearestInt();
    panel->setBounds (area.getRight() - 290, area.getY() + 10, 280, juce::jmin (area.getHeight() - 20, 560));
    if (! framed && ! fixtures.empty() && getWidth() > 100) { framed = true; frameAll(); }
}

//==============================================================================
int Visualizer3DView::fixtureAt (juce::Point<float> s) const
{
    int best = -1;
    float bestD = 16.0f;
    for (auto& fx : fixtures)
    {
        juce::Point<float> sp;
        if (! project (fx.pos, sp)) continue;
        const float d = sp.getDistanceFrom (s);
        if (d < bestD) { bestD = d; best = fx.f.fid; }
    }
    return best;
}

std::vector<int> Visualizer3DView::selectedFixtures() const
{
    std::vector<int> sel, out;
    { const juce::ScopedLock sl (engine.dataLock); sel = engine.programmer.selection(); }
    for (int t : sel)
        if (std::find (out.begin(), out.end(), targetFid (t)) == out.end()) out.push_back (targetFid (t));
    return out;
}

void Visualizer3DView::applyToSelection (std::function<void (PatchedFixture&)> fn)
{
    const auto sel = selectedFixtures();
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : sel)
            if (auto* f = engine.patch.find (fid))
            {
                auto copy = *f;
                if (! copy.has3D)
                {
                    const auto p = stage3d::position (copy);
                    copy.x3 = p.x; copy.y3 = p.y; copy.z3 = p.z;
                }
                fn (copy);
                engine.patch.set3D (fid, copy.x3, copy.y3, copy.z3, copy.rx3, copy.ry3, copy.rz3);
            }
    }
    rebuild();
    refreshPanel();
    repaint();
}

void Visualizer3DView::refreshPanel()
{
    if (! panel->isVisible()) return;
    const auto sel = selectedFixtures();
    std::vector<const PatchedFixture*> ptrs;
    for (auto& fx : fixtures)
        if (std::find (sel.begin(), sel.end(), fx.f.fid) != sel.end()) ptrs.push_back (&fx.f);
    panel->show (ptrs);
}

void Visualizer3DView::mouseDown (const juce::MouseEvent& e)
{
    last = downPos = e.position;
    clickAdd = e.mods.isShiftDown() || e.mods.isCommandDown();
    clickedFixture = fixtureAt (e.position);
    drag = Drag::Orbit;
    if (e.mods.isPopupMenu() || e.mods.isMiddleButtonDown()) { drag = Drag::Pan; return; }
    if (e.mods.isAltDown()) { drag = selectButton.getToggleState() ? Drag::Orbit : Drag::Pan; return; }

    if (const int h = gizmoHit (e.position); h != 0)
    {
        gizmoCentre (dragCentre);
        moveStart.clear();
        turnStart.clear();
        const auto sel = selectedFixtures();
        for (auto& fx : fixtures)
            if (std::find (sel.begin(), sel.end(), fx.f.fid) != sel.end()) { moveStart[fx.f.fid] = fx.pos; turnStart[fx.f.fid] = fx.f.ry3; }
        drag = h == 4 ? Drag::Turn : Drag::Axis;
        dragAxis = h;
        clickedFixture = -2;                 // not a click on a fixture
        return;
    }

    // box select: Select tool on empty space, or Shift / Cmd-drag on empty space
    if (clickedFixture < 0 && (selectButton.getToggleState() || clickAdd))
    {
        drag = Drag::Box;
        return;
    }

    if (placeButton.getToggleState() && clickedFixture >= 0)
    {
        auto sel = selectedFixtures();
        if (std::find (sel.begin(), sel.end(), clickedFixture) == sel.end())
        {
            { const juce::ScopedLock sl (engine.dataLock); engine.programmer.select (clickedFixture, clickAdd); }
            ui.changed();
            sel = selectedFixtures();
        }
        moveStart.clear();
        for (auto& fx : fixtures)
            if (std::find (sel.begin(), sel.end(), fx.f.fid) != sel.end()) moveStart[fx.f.fid] = fx.pos;
        drag = e.mods.isShiftDown() ? Drag::MoveY : Drag::Move;
        dragPlaneY = moveStart.count (clickedFixture) ? moveStart[clickedFixture].y : 0.0f;
        const auto o = cameraPosition(), d = rayDirection (e.position);
        dragPlanePoint = std::abs (d.y) > 1e-4f ? o + d * ((dragPlaneY - o.y) / d.y) : o;
    }
}

void Visualizer3DView::mouseDrag (const juce::MouseEvent& e)
{
    const auto delta = e.position - last;
    last = e.position;
    if (drag == Drag::Orbit)
    {
        cam.yaw -= delta.x * 0.4f;
        cam.pitch = juce::jlimit (-5.0f, 89.0f, cam.pitch + delta.y * 0.3f);
    }
    else if (drag == Drag::Pan)
    {
        V3 right, up, forward;
        basis (right, up, forward);
        const float k = cam.distance / juce::jmax (100.0f, viewArea().getHeight()) * 1.2f;
        cam.target = cam.target - right * (delta.x * k) + up * (delta.y * k);
    }
    else if (drag == Drag::Axis || drag == Drag::Turn)
    {
        juce::Point<float> s0;
        if (project (dragCentre, s0))
        {
            const juce::ScopedLock sl (engine.dataLock);
            if (drag == Drag::Axis)
            {
                const V3 axis = gizmoAxes[dragAxis - 1];
                const float L = gizmoLength();
                juce::Point<float> s1;
                if (project (dragCentre + axis * L, s1) && s1.getDistanceFrom (s0) > 2.0f)
                {
                    const auto dir = (s1 - s0) / s1.getDistanceFrom (s0);
                    const float metres = ((e.position - downPos).getDotProduct (dir)) / (s1.getDistanceFrom (s0) / L);
                    for (auto& [fid, start] : moveStart)
                        if (auto* f = engine.patch.find (fid))
                        {
                            const auto p = start + axis * metres;
                            engine.patch.set3D (fid, std::round (p.x * 20.0f) / 20.0f, juce::jmax (0.0f, std::round (p.y * 20.0f) / 20.0f),
                                                std::round (p.z * 20.0f) / 20.0f, f->rx3, f->ry3, f->rz3);
                        }
                }
            }
            else
            {
                const float a0 = std::atan2 (downPos.y - s0.y, downPos.x - s0.x), a1 = std::atan2 (e.position.y - s0.y, e.position.x - s0.x);
                float deg = juce::radiansToDegrees (a1 - a0);
                if (! e.mods.isShiftDown()) deg = std::round (deg / 5.0f) * 5.0f;          // 5 degree steps (Shift: free)
                for (auto& [fid, start] : moveStart)
                    if (auto* f = engine.patch.find (fid))
                    {
                        const auto p = dragCentre + stage3d::rotateY (start - dragCentre, -deg);
                        engine.patch.set3D (fid, p.x, p.y, p.z, f->rx3, std::fmod (turnStart[fid] - deg + 720.0f, 360.0f), f->rz3);
                    }
            }
        }
        rebuild();
        refreshPanel();
    }
    else if (drag == Drag::Move || drag == Drag::MoveY)
    {
        V3 offset;
        if (drag == Drag::MoveY)
            offset = { 0.0f, -(e.position.y - downPos.y) * cam.distance / juce::jmax (100.0f, viewArea().getHeight()), 0.0f };
        else
        {
            const auto o = cameraPosition(), d = rayDirection (e.position);
            if (std::abs (d.y) > 1e-4f) offset = o + d * ((dragPlaneY - o.y) / d.y) - dragPlanePoint;
            offset.y = 0.0f;
        }
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& [fid, start] : moveStart)
                if (auto* f = engine.patch.find (fid))
                {
                    const auto p = start + offset;
                    // snap to 5 cm
                    engine.patch.set3D (fid, std::round (p.x * 20.0f) / 20.0f, juce::jmax (0.0f, std::round (p.y * 20.0f) / 20.0f),
                                        std::round (p.z * 20.0f) / 20.0f, f->rx3, f->ry3, f->rz3);
                }
        }
        rebuild();
        refreshPanel();
    }
    repaint();
}

void Visualizer3DView::mouseUp (const juce::MouseEvent& e)
{
    const bool click = e.position.getDistanceFrom (downPos) < 4.0f;
    if (drag == Drag::Box && ! click)
    {
        boxSelect (juce::Rectangle<float> (downPos, e.position), clickAdd, e.position.x < downPos.x, e.position.y < downPos.y);
        drag = Drag::None;
        repaint();
        return;
    }
    if (click && drag != Drag::Pan && drag != Drag::Axis && drag != Drag::Turn)
    {
        {
            const juce::ScopedLock sl (engine.dataLock);
            if (clickedFixture >= 0)
            {
                if (clickAdd) engine.programmer.toggle (clickedFixture);
                else          engine.programmer.select (clickedFixture, false);
            }
            else if (! clickAdd) engine.programmer.clearSelection();
        }
        ui.changed();
    }
    drag = Drag::None;
}

void Visualizer3DView::boxSelect (juce::Rectangle<float> box, bool add, bool rightToLeft, bool bottomToTop)
{
    // fixtures inside the box, ordered along the drag (left to right unless dragged leftwards)
    std::vector<std::pair<juce::Point<float>, int>> hits;
    for (auto& fx : fixtures)
    {
        juce::Point<float> sp;
        if (project (fx.pos, sp) && box.contains (sp)) hits.push_back ({ sp, fx.f.fid });
    }
    const bool horizontal = box.getWidth() >= box.getHeight();
    std::sort (hits.begin(), hits.end(), [&] (auto& a, auto& b)
    {
        const float ka = horizontal ? a.first.x : a.first.y, kb = horizontal ? b.first.x : b.first.y;
        const bool reverse = horizontal ? rightToLeft : bottomToTop;
        return reverse ? ka > kb : ka < kb;
    });
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto sel = add ? engine.programmer.selection() : std::vector<int> {};
        for (auto& [p, fid] : hits)
            if (std::find (sel.begin(), sel.end(), fid) == sel.end()) sel.push_back (fid);
        engine.programmer.setSelection (sel);
    }
    ui.changed();
    refreshPanel();
}

void Visualizer3DView::groupsMenu()
{
    juce::PopupMenu m;
    std::vector<int> numbers;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& [n, p] : engine.presets.pool (PresetPool::Group))
        {
            numbers.push_back (n);
            m.addItem ((int) numbers.size(), (p.name.isNotEmpty() ? p.name : "Group " + juce::String (n)) + "  (" + juce::String ((int) p.fixtures.size()) + ")");
        }
    }
    if (numbers.empty()) m.addItem (-1, "No groups yet (make them in the Layout or Presets)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&groupsButton),
                     [safe = juce::Component::SafePointer<Visualizer3DView> (this), numbers] (int r)
    {
        if (safe == nullptr || r < 1 || r > (int) numbers.size()) return;
        {
            const juce::ScopedLock sl (safe->engine.dataLock);
            safe->engine.presets.recall (PresetPool::Group, numbers[(size_t) r - 1], safe->engine.programmer, safe->engine.patch, safe->engine.library,
                                         juce::ModifierKeys::currentModifiers.isShiftDown());
        }
        safe->ui.changed();
        safe->refreshPanel();
        safe->repaint();
    });
}

void Visualizer3DView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    cam.distance = juce::jlimit (2.0f, 120.0f, cam.distance * (1.0f - w.deltaY * 0.8f));
    repaint();
}

void Visualizer3DView::mouseMagnify (const juce::MouseEvent&, float scale)
{
    cam.distance = juce::jlimit (2.0f, 120.0f, cam.distance / juce::jmax (0.2f, scale));
    repaint();
}

} // namespace pl
