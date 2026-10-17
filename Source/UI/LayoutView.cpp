#include "LayoutView.h"
#include "EditOps.h"
#include "ColorPicker.h"

namespace pl
{

static constexpr float kFixtureRadius = 22.0f;
static constexpr float kSpacing = 64.0f;
static constexpr float kGrid = 8.0f;

static void styleSwatch (juce::TextButton& b, juce::uint32 argb)
{
    const juce::Colour c (argb);
    b.setColour (juce::TextButton::buttonColourId, c);
    b.setColour (juce::TextButton::textColourOffId, c.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
}

//==============================================================================
/** Settings for the selected pixel map: source (video, test pattern or effect),
    the effect's settings, intensity, colour only, delete. */
class LayoutView::MapPanel : public juce::Component
{
public:
    MapPanel (LayoutView& o) : owner (o)
    {
        title.setFont (pl::theme::font (13.0f, true));
        addAndMakeVisible (title);
        source.onChange = [this]
        {
            if (source.getText() == kByName)
            {
                // an NDI source that isn't discovered (other subnet): type its name
                ui::askForText (this, "NDI source", "NDI source name, as shown in NDI Studio Monitor (MACHINE (Source))", {},
                                [safe = juce::Component::SafePointer<MapPanel> (this)] (const juce::String& name)
                {
                    if (safe == nullptr || name.isEmpty()) return;
                    const auto full = name.startsWith ("NDI: ") ? name : "NDI: " + name;
                    safe->sources.insert (safe->sources.size() - 1, full);
                    safe->rebuildSourceCombo (full);
                    safe->push();
                    safe->refreshVisibility();
                });
                return;
            }
            push();
            refreshVisibility();
        };
        addAndMakeVisible (source);
        ndiLabel.setFont (pl::theme::font (11.5f));
        ndiLabel.setColour (juce::Label::textColourId, pl::theme::textDim);
        ndiLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (ndiLabel);

        auto slider = [this] (juce::Slider& s, double lo, double hi, double step, int decimals)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setRange (lo, hi, step);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 22);
            s.setNumDecimalPlacesToDisplay (decimals);
            s.onValueChange = [this] { push(); };
            addAndMakeVisible (s);
        };
        slider (intensity, 0.0, 1.0, 0.0, 2);
        slider (bpm, 1.0, 240.0, 1.0, 0);
        slider (waves, 0.25, 8.0, 0.05, 2);
        slider (width, 0.05, 1.0, 0.0, 2);
        bpm.setSkewFactorFromMidPoint (60.0);

        for (int i = 0; i < 2; ++i) kind.addItem (i == 0 ? "Colour effect" : "Dimmer effect", i + 1);
        kind.onChange = [this] { push(); refreshForms(); };
        form.onChange = [this] { push(); };
        for (int i = 0; i < pixelmap::directions().size(); ++i) direction.addItem (pixelmap::directions()[i], i + 1);
        direction.onChange = [this] { push(); };
        colourA.onClick = [this] { pickColour (true); };
        colourB.onClick = [this] { pickColour (false); };
        colourA.setTooltip ("Colour A");
        colourB.setTooltip ("Colour B");
        for (auto* c : std::initializer_list<juce::Component*> { &kind, &form, &direction, &colourA, &colourB })
            addAndMakeVisible (c);
        for (auto* l : { &bpmLabel, &wavesLabel, &widthLabel, &intensityLabel })
        {
            l->setFont (pl::theme::font (12.0f));
            l->setColour (juce::Label::textColourId, pl::theme::textDim);
            addAndMakeVisible (l);
        }

        colourOnly.setTooltip ("Only set colour; keep the fixtures' intensity from playback");
        colourOnly.onClick = [this] { push(); };
        enabled.onClick = [this] { push(); };
        addAndMakeVisible (colourOnly);
        addAndMakeVisible (enabled);
        remove.setColour (juce::TextButton::buttonColourId, pl::theme::danger.withAlpha (0.8f));
        remove.onClick = [this]
        {
            {
                const juce::ScopedLock sl (owner.engine.dataLock);
                auto& maps = owner.engine.pixelMaps;
                maps.erase (std::remove_if (maps.begin(), maps.end(), [this] (const PixelMap& m) { return m.id == owner.selectedMap; }), maps.end());
            }
            owner.selectMap (0);
        };
        addAndMakeVisible (remove);
    }

    void load (const PixelMap& m)
    {
        loading = true;
        title.setText ("PIXEL MAP " + juce::String (m.id), juce::dontSendNotification);
        sources = owner.ui.videoSources ? owner.ui.videoSources() : pixelmap::testSources();
        sources.insert (0, pixelmap::effectSource());
        if (! sources.contains (m.source)) sources.add (m.source);
        sources.add (kByName);
        rebuildSourceCombo (m.source);
        ndiLabel.setText (owner.ui.ndiStatus ? owner.ui.ndiStatus() : juce::String(), juce::dontSendNotification);
        intensity.setValue (m.intensity, juce::dontSendNotification);
        colourOnly.setToggleState (! m.driveDimmer, juce::dontSendNotification);
        enabled.setToggleState (m.enabled, juce::dontSendNotification);

        kind.setSelectedItemIndex (m.effect.kind == EffectKind::Color ? 0 : 1, juce::dontSendNotification);
        effectForm = m.effect.form;
        refreshForms();
        direction.setSelectedItemIndex (juce::jmax (0, pixelmap::directions().indexOf (m.direction)), juce::dontSendNotification);
        bpm.setValue (m.effect.bpm, juce::dontSendNotification);
        waves.setValue (m.waves, juce::dontSendNotification);
        width.setValue (m.effect.width, juce::dontSendNotification);
        colA = m.effect.colourA; colB = m.effect.colourB;
        styleSwatch (colourA, colA);
        styleSwatch (colourB, colB);
        loading = false;
        refreshVisibility();
    }

    bool showsEffect() const { return source.getText() == pixelmap::effectSource(); }
    int preferredHeight() const { return showsEffect() ? 372 : 196; }

    /** Refresh the list when NDI sources come and go. */
    void refreshSources()
    {
        if (! isVisible() || source.isPopupActive()) return;
        auto fresh = owner.ui.videoSources ? owner.ui.videoSources() : pixelmap::testSources();
        fresh.insert (0, pixelmap::effectSource());
        const auto current = source.getText();
        for (auto& x : sources) if (x.startsWith ("NDI: ") && ! fresh.contains (x)) fresh.add (x);   // typed names
        if (! fresh.contains (current) && current.isNotEmpty()) fresh.add (current);
        fresh.add (kByName);
        if (fresh != sources) { sources = fresh; rebuildSourceCombo (current); }
        ndiLabel.setText (owner.ui.ndiStatus ? owner.ui.ndiStatus() : juce::String(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pl::theme::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (pl::theme::familyColour (Family::Beam));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        source.setBounds (r.removeFromTop (28));
        ndiLabel.setBounds (r.removeFromTop (18));
        r.removeFromTop (4);
        auto labelled = [&r] (juce::Label& l, juce::Component& c)
        {
            auto row = r.removeFromTop (26);
            l.setBounds (row.removeFromLeft (62));
            c.setBounds (row);
            r.removeFromTop (4);
        };
        if (showsEffect())
        {
            kind.setBounds (r.removeFromTop (26));
            r.removeFromTop (4);
            auto row = r.removeFromTop (26);
            form.setBounds (row.removeFromLeft (row.getWidth() - 76));
            row.removeFromLeft (4);
            colourA.setBounds (row.removeFromLeft (34));
            row.removeFromLeft (4);
            colourB.setBounds (row);
            r.removeFromTop (4);
            direction.setBounds (r.removeFromTop (26));
            r.removeFromTop (4);
            labelled (bpmLabel, bpm);
            labelled (wavesLabel, waves);
            labelled (widthLabel, width);
        }
        labelled (intensityLabel, intensity);
        auto row = r.removeFromTop (26);
        enabled.setBounds (row.removeFromLeft (90));
        colourOnly.setBounds (row);
        r.removeFromTop (6);
        remove.setBounds (r.removeFromTop (28));
    }

private:
    static constexpr const char* kByName = "NDI source by name...";

    void rebuildSourceCombo (const juce::String& select)
    {
        source.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& x : sources)
        {
            if (x == kByName) source.addSeparator();
            source.addItem (x, id++);
        }
        source.setSelectedItemIndex (sources.indexOf (select), juce::dontSendNotification);
    }

    void refreshVisibility()
    {
        const bool fx = showsEffect();
        for (auto* c : std::initializer_list<juce::Component*> { &kind, &form, &direction, &colourA, &colourB,
                                                                &bpm, &waves, &width, &bpmLabel, &wavesLabel, &widthLabel })
            c->setVisible (fx);
        owner.resized();
    }

    void refreshForms()
    {
        const auto k = kind.getSelectedItemIndex() == 0 ? EffectKind::Color : EffectKind::Dimmer;
        const auto& forms = effects::formsFor (k);
        form.clear (juce::dontSendNotification);
        for (int i = 0; i < forms.size(); ++i) form.addItem (forms[i], i + 1);
        form.setSelectedItemIndex (juce::jmax (0, forms.indexOf (effectForm)), juce::dontSendNotification);
        colourB.setEnabled (k == EffectKind::Color);
    }

    void pickColour (bool a)
    {
        auto panel = std::make_unique<ColorPickerPanel>();
        panel->setColour (juce::Colour (a ? colA : colB));
        panel->onPick = [safe = juce::Component::SafePointer<MapPanel> (this), a] (juce::Colour c)
        {
            if (safe == nullptr) return;
            (a ? safe->colA : safe->colB) = c.getARGB();
            styleSwatch (a ? safe->colourA : safe->colourB, c.getARGB());
            safe->push();
        };
        juce::CallOutBox::launchAsynchronously (std::move (panel), (a ? colourA : colourB).getScreenBounds(), nullptr);
    }

    void push()
    {
        if (loading) return;
        effectForm = form.getText();
        const juce::ScopedLock sl (owner.engine.dataLock);
        for (auto& m : owner.engine.pixelMaps)
            if (m.id == owner.selectedMap)
            {
                const int i = source.getSelectedItemIndex();
                if (i >= 0 && i < sources.size() && sources[i] != kByName) m.source = sources[i];
                m.intensity = (float) intensity.getValue();
                m.driveDimmer = ! colourOnly.getToggleState();
                m.enabled = enabled.getToggleState();
                m.effect.kind = kind.getSelectedItemIndex() == 0 ? EffectKind::Color : EffectKind::Dimmer;
                m.effect.updateFamily();
                if (effects::formsFor (m.effect.kind).contains (effectForm)) m.effect.form = effectForm;
                else m.effect.form = effects::formsFor (m.effect.kind)[0];
                m.effect.bpm = (float) bpm.getValue();
                m.effect.width = (float) width.getValue();
                m.effect.colourA = colA;
                m.effect.colourB = colB;
                m.direction = direction.getText().isNotEmpty() ? direction.getText() : juce::String ("Left to right");
                m.waves = (float) waves.getValue();
            }
    }

    LayoutView& owner;
    bool loading = false;
    juce::StringArray sources;
    juce::String effectForm;
    juce::uint32 colA = 0xffff0000u, colB = 0xff0000ffu;
    juce::Label title, bpmLabel { {}, "BPM" }, wavesLabel { {}, "Waves" }, widthLabel { {}, "Width" },
                intensityLabel { {}, "Intensity" };
    juce::ComboBox source, kind, form, direction;
    juce::Label ndiLabel;
    juce::TextButton colourA { "A" }, colourB { "B" };
    juce::Slider intensity, bpm, waves, width;
    juce::ToggleButton colourOnly { "Colour only" }, enabled { "Enabled" };
    juce::TextButton remove { "Delete map" };
};

//==============================================================================
juce::Point<float> LayoutView::Item::cellOffset (int cell) const
{
    if (base.empty()) return {};
    auto p = base[(size_t) juce::jlimit (0, (int) base.size() - 1, cell - 1)];
    if (mirror) p.x = -p.x;
    const float a = juce::degreesToRadians (rotation), c = std::cos (a), s = std::sin (a);
    return { p.x * c - p.y * s, p.x * s + p.y * c };
}

class LayoutView::GroupButton : public juce::TextButton
{
public:
    GroupButton (const juce::String& name, int n) : juce::TextButton (name), number (n) {}
    using juce::TextButton::clicked;
    std::function<void (int, bool popup, juce::Component*)> onTap;
    const int number;

private:
    void clicked (const juce::ModifierKeys& m) override { if (onTap) onTap (number, m.isPopupMenu(), this); }
};

LayoutView::LayoutView (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    newGroupButton.setTooltip ("Make a group from the selected fixtures (or right-click the rig)");
    newGroupButton.onClick = [this] { createGroupFromSelection(); };
    addAndMakeVisible (newGroupButton);

    editButton.setClickingTogglesState (true);
    editButton.setTooltip ("Drag fixtures to match the real rig; tools to arrange, rotate, flip, align and space them");
    editButton.onClick = [this]
    {
        // the tools row pushes the canvas down: keep the rig where it was on screen
        origin.y += editing() ? -(float) (theme::buttonH - 4 + theme::gap) : (float) (theme::buttonH - 4 + theme::gap);
        resized();
        repaint();
    };
    fitButton.onClick    = [this] { fitToView(); };
    allButton.onClick    = [this] { selectAll(); };

    struct Tool { const char* name; const char* tip; std::function<void (const juce::ModifierKeys&)> fn; };
    const std::vector<Tool> tools {
        { "Line",     "Arrange the selection in a line, in selection order", [this] (auto&) { arrange ("line"); } },
        { "Grid",     "Arrange the selection in a grid, in selection order", [this] (auto&) { arrange ("grid"); } },
        { "Circle",   "Arrange the selection in a circle, in selection order", [this] (auto&) { arrange ("circle"); } },
        { "Auto",     "Place every fixture automatically (by FID)", [this] (auto&) { autoPlaceAll (false); fitToView(); } },
        { "",         "", nullptr },
        { "Rot -90",  "Rotate the selection 90 degrees anticlockwise (Shift: 15)", [this] (auto& m) { rotateSelection (m.isShiftDown() ? -15.0f : -90.0f); } },
        { "Rot +90",  "Rotate the selection 90 degrees clockwise (Shift: 15)", [this] (auto& m) { rotateSelection (m.isShiftDown() ? 15.0f : 90.0f); } },
        { "Flip H",   "Mirror the selection left-right", [this] (auto&) { flipSelection (true); } },
        { "Flip V",   "Mirror the selection top-bottom", [this] (auto&) { flipSelection (false); } },
        { "",         "", nullptr },
        { "Left",     "Align left edges", [this] (auto&) { alignSelection ("left"); } },
        { "Centre",   "Align on a vertical centre line", [this] (auto&) { alignSelection ("centre"); } },
        { "Right",    "Align right edges", [this] (auto&) { alignSelection ("right"); } },
        { "Top",      "Align tops", [this] (auto&) { alignSelection ("top"); } },
        { "Middle",   "Align on a horizontal centre line", [this] (auto&) { alignSelection ("middle"); } },
        { "Bottom",   "Align bottoms", [this] (auto&) { alignSelection ("bottom"); } },
        { "",         "", nullptr },
        { "Even H",   "Space the selection evenly left to right", [this] (auto&) { distributeSelection (true); } },
        { "Even V",   "Space the selection evenly top to bottom", [this] (auto&) { distributeSelection (false); } },
        { "Tighter",  "Move the selection closer together", [this] (auto&) { scaleSpacing (0.8f); } },
        { "Wider",    "Spread the selection further apart", [this] (auto&) { scaleSpacing (1.25f); } },
    };
    for (auto& t : tools)
    {
        auto* b = editTools.add (new juce::TextButton (t.name));
        if (t.fn == nullptr) continue;             // spacer
        b->setTooltip (t.tip);
        b->onClick = [fn = t.fn] { fn (juce::ModifierKeys::getCurrentModifiers()); };
        addChildComponent (b);
    }

    pixelsButton.setClickingTogglesState (true);
    pixelsButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Color));
    pixelsButton.setTooltip ("Select single pixels (cells) of pixel fixtures; program them and run effects across them like fixtures");
    pixelsButton.onClick = [this] { repaint(); };

    mapButton.setClickingTogglesState (true);
    mapButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Beam));
    mapButton.setTooltip ("Pixel map mode: drag to draw a map, click a map to edit, drag to move, drag its corner to resize");
    mapButton.onClick = [this] { if (! mapButton.getToggleState()) selectMap (0); repaint(); };
    mapPanel = std::make_unique<MapPanel> (*this);
    addChildComponent (mapPanel.get());

    orderButton.setClickingTogglesState (true);
    orderButton.setTooltip ("Selection order for lasso selection: the drag direction, or fixture ID");
    orderButton.onClick = [this] { orderButton.setButtonText (orderButton.getToggleState() ? "Order: FID" : "Order: Drag"); };
    sortButton.setTooltip ("Re-order the current selection by fixture ID (pixels by cell)");
    sortButton.onClick = [this]
    {
        {
            const juce::ScopedLock sl (engine.dataLock);
            auto sel = engine.programmer.selection();
            std::stable_sort (sel.begin(), sel.end(), [] (int a, int b)
            {
                return std::make_pair (targetFid (a), targetCell (a)) < std::make_pair (targetFid (b), targetCell (b));
            });
            engine.programmer.setSelection (sel);
        }
        ui.changed();
    };

    hint.setFont (theme::font (12.5f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::centredRight);
    hint.setText ("Drag across fixtures to select in that order  -  Cmd/Shift adds  -  scroll to pan, pinch or Cmd+scroll to zoom",
                  juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> {
             &allButton, &editButton, &fitButton, &orderButton, &sortButton, &pixelsButton, &mapButton, &hint })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    rebuildItems();
    rebuildGroupButtons();
    startTimerHz (25);
}

LayoutView::~LayoutView()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void LayoutView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine) rebuildItems();
    rebuildGroupButtons();
    repaint();
}

void LayoutView::timerCallback()
{
    if (isShowing()) repaint (canvasArea());
    if (++timerTicks % 25 == 0 && mapPanel->isVisible()) mapPanel->refreshSources();
}

//==============================================================================
void LayoutView::rebuildItems()
{
    const bool hadItems = ! items.empty();
    items.clear();
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& f : engine.patch.fixtures())
        {
            if (f.hiddenInLayout) continue;
            bool panTilt = false;
            int cx = 1, cy = 1;
            if (auto* t = engine.library.find (f.typeId))
            {
                for (auto& a : t->attributes)
                    panTilt |= (a.attribute == "Pan" || a.attribute == "Tilt");
                cx = t->cellsX; cy = t->cellsY;
            }
            std::vector<juce::Point<float>> base;
            bool custom = false;
            if (auto* t = engine.library.find (f.typeId); t != nullptr && t->hasCells())
            {
                custom = t->hasCustomLayout();
                for (int c = 1; c <= t->cellCount(); ++c)
                {
                    auto [ox, oy] = t->cellOffset (c);
                    base.push_back ({ ox, oy });
                }
            }
            Item it;
            it.fid = f.fid; it.name = f.name; it.pos = { f.layoutX, f.layoutY };
            it.hasPanTilt = panTilt; it.saved = f.hasLayoutPosition;
            it.cellsX = juce::jmax (1, cx); it.cellsY = juce::jmax (1, cy);
            it.base = base;
            it.custom = custom;
            if (! base.empty())
            {
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                for (auto& p : base) { x0 = juce::jmin (x0, p.x); x1 = juce::jmax (x1, p.x); y0 = juce::jmin (y0, p.y); y1 = juce::jmax (y1, p.y); }
                it.extentW = (x1 - x0) / FixtureType::cellSpacing + 1.0f;
                it.extentH = (y1 - y0) / FixtureType::cellSpacing + 1.0f;
            }
            it.rotation = f.layoutRotation;
            it.mirror = f.layoutMirror;
            items.push_back (it);
        }
    }

    // place fixtures that have never been positioned (not saved until edited)
    float maxY = 0.0f;
    for (auto& it : items)
        if (it.saved) maxY = juce::jmax (maxY, it.pos.y);
    const bool anySaved = std::any_of (items.begin(), items.end(), [] (const Item& i) { return i.saved; });
    float y = anySaved ? maxY + kSpacing * 1.5f : kSpacing * 0.75f;
    float x = kSpacing * 0.75f;
    juce::String lastPrefix;
    for (auto& it : items)
    {
        if (it.saved) continue;
        const auto prefix = juce::String (it.fid / 100);
        if (lastPrefix.isNotEmpty() && prefix != lastPrefix && x > kSpacing) { x = kSpacing * 0.75f; y += juce::jmax (kSpacing * 1.3f, it.extentH * FixtureType::cellSpacing + 40.0f); }
        lastPrefix = prefix;
        it.pos = { x, y };
        x += juce::jmax (kSpacing, it.extentW * FixtureType::cellSpacing + 24.0f);
        if (x > kSpacing * 16) { x = kSpacing * 0.75f; y += juce::jmax (kSpacing * 1.3f, it.extentH * FixtureType::cellSpacing + 40.0f); }
    }

    if (! hadItems && ! items.empty() && getWidth() > 0)
        fitToView();
}

juce::Rectangle<int> LayoutView::canvasArea() const
{
    auto r = getLocalBounds().reduced (theme::gap).withTrimmedTop (theme::buttonH + theme::gap);
    if (editing()) r.removeFromTop (theme::buttonH - 4 + theme::gap);
    r.removeFromTop (28 + theme::gap);          // group buttons
    return r;
}

juce::Rectangle<int> LayoutView::groupRow() const
{
    auto r = getLocalBounds().reduced (theme::gap).withTrimmedTop (theme::buttonH + theme::gap);
    if (editing()) r.removeFromTop (theme::buttonH - 4 + theme::gap);
    return r.removeFromTop (28);
}

juce::Point<float> LayoutView::toScreen (juce::Point<float> p) const
{
    auto c = canvasArea().toFloat();
    return { c.getX() + origin.x + p.x * zoom, c.getY() + origin.y + p.y * zoom };
}

juce::Point<float> LayoutView::toLayout (juce::Point<float> s) const
{
    auto c = canvasArea().toFloat();
    return { (s.x - c.getX() - origin.x) / zoom, (s.y - c.getY() - origin.y) / zoom };
}

juce::Point<float> LayoutView::cellCentre (const Item& it, int cell) const
{
    return toScreen (it.pos + it.cellOffset (cell));
}

juce::Rectangle<float> LayoutView::bodyOf (const Item& it) const
{
    if (it.hasCells())
    {
        // every cell bounds the (possibly rotated, any shape) fixture
        float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
        for (int c = 1; c <= it.cellCount(); ++c)
        {
            const auto p = cellCentre (it, c);
            x0 = juce::jmin (x0, p.x); y0 = juce::jmin (y0, p.y); x1 = juce::jmax (x1, p.x); y1 = juce::jmax (y1, p.y);
        }
        auto r = juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1);
        return r.expanded (FixtureType::cellSpacing * zoom * 0.5f);
    }
    const float r = kFixtureRadius * zoom;
    return juce::Rectangle<float> (r * 2, r * 2).withCentre (toScreen (it.pos));
}

int LayoutView::cellAt (const Item& it, juce::Point<float> screen) const
{
    if (! it.hasCells()) return 0;
    // the nearest pixel within about half a pixel
    const float reach = FixtureType::cellSpacing * zoom * 0.62f;
    int best = 0;
    float bestD = reach * reach;
    for (int c = 1; c <= it.cellCount(); ++c)
    {
        const auto d = cellCentre (it, c) - screen;
        const float d2 = d.x * d.x + d.y * d.y;
        if (d2 < bestD) { bestD = d2; best = c; }
    }
    return best;
}

int LayoutView::fixtureAt (juce::Point<float> screen) const
{
    for (int i = (int) items.size() - 1; i >= 0; --i)
    {
        auto& it = items[(size_t) i];
        if (it.hasCells() ? cellAt (it, screen) > 0 : bodyOf (it).expanded (2.0f).contains (screen))
            return it.fid;
    }
    return -1;
}

int LayoutView::targetAt (juce::Point<float> screen) const
{
    const int fid = fixtureAt (screen);
    if (fid < 0 || ! pixelsButton.getToggleState()) return fid;
    for (auto& it : items)
        if (it.fid == fid && it.hasCells())
            if (const int c = cellAt (it, screen); c > 0) return cellTarget (fid, c);
    return fid;
}

//==============================================================================
void LayoutView::paint (juce::Graphics& g)
{
    auto canvas = canvasArea();
    ui::paintPanel (g, canvas);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (canvas.reduced (1));

        // grid
        const float step = kSpacing * zoom * 0.5f;
        if (step > 6.0f)
        {
            g.setColour (theme::line.withAlpha (0.35f));
            auto o = toScreen ({ 0, 0 });
            const float startX = o.x - std::ceil ((o.x - (float) canvas.getX()) / step) * step;
            const float startY = o.y - std::ceil ((o.y - (float) canvas.getY()) / step) * step;
            for (float x = startX; x < (float) canvas.getRight(); x += step)
                for (float y = startY; y < (float) canvas.getBottom(); y += step)
                    g.fillRect (x, y, 1.5f, 1.5f);
        }

        paintMaps (g);

        std::vector<int> selection;
        {
            const juce::ScopedLock sl (engine.dataLock);
            selection = engine.programmer.selection();
        }
        for (auto& it : items)
            paintFixture (g, it, selection);

        if (dragMode == DragMode::Lasso)
        {
            auto rect = juce::Rectangle<float> (dragStart, dragNow);
            g.setColour (theme::accent.withAlpha (0.12f));
            g.fillRect (rect);
            g.setColour (theme::accent);
            g.drawRect (rect, 1.0f);
            // direction arrow: selection order follows the drag
            g.drawArrow ({ dragStart, dragNow }, 1.5f, 10.0f, 10.0f);
        }

        if (items.empty())
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (15.0f));
            g.drawText ("Patch fixtures to see them here", canvas, juce::Justification::centred);
        }
    }

    if (editing())
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (canvas.toFloat().reduced (1.0f), 8.0f, 2.0f);
    }
    else if (pixelsButton.getToggleState())
    {
        g.setColour (theme::familyColour (Family::Color));
        g.drawRoundedRectangle (canvas.toFloat().reduced (1.0f), 8.0f, 1.5f);
    }
}

static void drawBadge (juce::Graphics& g, juce::Point<float> centre, int number, float size = 18.0f)
{
    auto badge = juce::Rectangle<float> (size, size).withCentre (centre);
    g.setColour (theme::accent);
    g.fillEllipse (badge);
    g.setColour (theme::bg);
    g.setFont (theme::font (size * 0.58f, true));
    g.drawText (juce::String (number), badge, juce::Justification::centred);
}

void LayoutView::paintFixture (juce::Graphics& g, const Item& it, const std::vector<int>& selection)
{
    const float r = kFixtureRadius * zoom;
    auto out = engine.getFixtureOutput (it.fid);
    auto get = [&out] (const juce::String& k, float def) { auto i = out.find (k); return i != out.end() ? i->second : def; };

    const float dim = get ("Intensity", get ("Dimmer", 0.0f));
    juce::Colour lightColour = juce::Colours::white;
    const bool hasColour = out.count ("ColorR") || out.count ("ColorG") || out.count ("ColorB");
    if (hasColour)
    {
        const float w = get ("ColorW", 0.0f);
        float rr = get ("ColorR", 0.0f), gg = get ("ColorG", 0.0f), bb = get ("ColorB", 0.0f);
        rr = juce::jmin (1.0f, rr + w); gg = juce::jmin (1.0f, gg + w); bb = juce::jmin (1.0f, bb + w);
        lightColour = juce::Colour::fromFloatRGBA (rr, gg, bb, 1.0f);
    }

    const auto c = toScreen (it.pos);
    const auto body = bodyOf (it);
    const auto selIt = std::find (selection.begin(), selection.end(), it.fid);
    const bool selected = selIt != selection.end();

    if (it.hasCells())
    {
        // pixel fixture: one square per cell, coloured by its output, turned with the fixture
        const float cs = FixtureType::cellSpacing * zoom;
        const float angle = juce::degreesToRadians (it.rotation);
        const bool square = std::abs (std::fmod (it.rotation, 90.0f)) < 0.01f;

        auto cellShape = [&] (juce::Point<float> centre, float size)
        {
            juce::Path p;
            p.addRectangle (juce::Rectangle<float> (size, size).withCentre (centre));
            if (! square) p.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));
            return p;
        };
        {
            // housing
            juce::Path housing;
            auto local = juce::Rectangle<float> (it.extentW * cs + 4.0f, it.extentH * cs + 4.0f).withCentre (c);
            if (it.custom && std::abs (it.extentW - it.extentH) < 1.0f) housing.addEllipse (local.expanded (cs * 0.15f));   // rings
            else housing.addRoundedRectangle (local, it.custom ? cs * 0.5f : 3.0f);
            housing.applyTransform (juce::AffineTransform::rotation (angle, c.x, c.y));
            g.setColour (theme::panel2);
            g.fillPath (housing);
            g.setColour (selected ? theme::accent : theme::line.brighter (0.3f));
            g.strokePath (housing, juce::PathStrokeType (selected ? 2.5f : 1.0f));
        }

        for (int cell = 1; cell <= it.cellCount(); ++cell)
        {
            const auto centre = cellCentre (it, cell);
            const auto suffix = "@" + juce::String (cell);
            const float cr = get ("ColorR" + suffix, 1.0f), cg = get ("ColorG" + suffix, 1.0f),
                        cb = get ("ColorB" + suffix, 1.0f), cw = get ("ColorW" + suffix, 0.0f);
            const float cd = get ("Dimmer" + suffix, 1.0f) * dim;
            const auto cellColour = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, cr + cw), juce::jmin (1.0f, cg + cw),
                                                                 juce::jmin (1.0f, cb + cw), 1.0f);
            g.setColour (theme::bg.interpolatedWith (cellColour, juce::jlimit (0.0f, 1.0f, cd)));
            if (it.custom) g.fillEllipse (juce::Rectangle<float> (cs, cs).withCentre (centre).reduced (1.5f));
            else if (square) g.fillRect (juce::Rectangle<float> (cs, cs).withCentre (centre).reduced (1.0f));
            else        g.fillPath (cellShape (centre, cs - 2.0f));

            // single pixel selected
            const auto cellSel = std::find (selection.begin(), selection.end(), cellTarget (it.fid, cell));
            if (cellSel != selection.end())
            {
                g.setColour (theme::accent);
                g.strokePath (cellShape (centre, cs - 1.0f), juce::PathStrokeType (2.0f));
                if (cs >= 15.0f)
                    drawBadge (g, centre, (int) (cellSel - selection.begin()) + 1, juce::jmin (16.0f, cs * 0.8f));
            }
        }
        if (pixelsButton.getToggleState() && cs >= 22.0f)
        {
            // cell numbers help when picking pixels
            g.setFont (theme::font (9.0f));
            g.setColour (theme::text.withAlpha (0.55f));
            for (int cell = 1; cell <= it.cellCount(); ++cell)
                g.drawText (juce::String (cell), juce::Rectangle<float> (cs, cs).withCentre (cellCentre (it, cell)).reduced (2.0f),
                            juce::Justification::bottomRight);
        }

        g.setColour (selected ? theme::accent : theme::text);
        g.setFont (theme::font (juce::jlimit (9.0f, 14.0f, 12.0f * zoom), true));
        g.drawText (juce::String (it.fid), juce::Rectangle<float> (body.getX(), body.getBottom() + 3.0f, body.getWidth(), 15.0f),
                    juce::Justification::centred);
        if (selected)
            drawBadge (g, body.getTopRight().translated (2.0f, -2.0f), (int) (selIt - selection.begin()) + 1);
        return;
    }

    // glow
    if (dim > 0.01f)
    {
        g.setColour (lightColour.withAlpha (0.18f * dim));
        g.fillEllipse (body.expanded (r * 0.45f * dim));
    }

    g.setColour (theme::panel2);
    if (it.hasPanTilt) g.fillEllipse (body);
    else               g.fillRoundedRectangle (body, r * 0.3f);

    // colour ring: shows the fixture's colour even when its intensity is at zero
    if (hasColour)
    {
        g.setColour (lightColour.withAlpha (0.9f));
        const float ringW = juce::jmax (2.5f, r * 0.16f);
        if (it.hasPanTilt) g.drawEllipse (body.reduced (ringW * 0.5f + 1.0f), ringW);
        else               g.drawRoundedRectangle (body.reduced (ringW * 0.5f + 1.0f), r * 0.26f, ringW);
    }

    auto lens = body.reduced (r * 0.28f);
    g.setColour (theme::bg.interpolatedWith (lightColour, juce::jlimit (0.0f, 1.0f, dim)));
    if (it.hasPanTilt) g.fillEllipse (lens);
    else               g.fillRoundedRectangle (lens, r * 0.2f);

    if (it.hasPanTilt)
    {
        // pan/tilt indicator: dot offset from centre
        const float pan = get ("Pan", 0.5f), tilt = get ("Tilt", 0.5f);
        // more tilt = beam up = the dot moves up the screen
        auto p = c + juce::Point<float> ((pan - 0.5f) * 2.0f, (0.5f - tilt) * 2.0f) * (r * 0.8f);
        g.setColour (theme::familyColour (Family::Position));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
    }

    g.setColour (selected ? theme::accent : theme::line.brighter (0.3f));
    if (it.hasPanTilt) g.drawEllipse (body, selected ? 2.5f : 1.0f);
    else               g.drawRoundedRectangle (body, r * 0.3f, selected ? 2.5f : 1.0f);

    g.setColour (selected ? theme::accent : theme::text);
    g.setFont (theme::font (juce::jlimit (9.0f, 14.0f, 12.0f * zoom), true));
    g.drawText (juce::String (it.fid),
                juce::Rectangle<float> (body.getX() - 12.0f, body.getBottom() + 2.0f, body.getWidth() + 24.0f, 15.0f),
                juce::Justification::centred);

    if (selected)   // selection order badge (phase order for effects)
        drawBadge (g, body.getTopRight().translated (-3.0f, 3.0f), (int) (selIt - selection.begin()) + 1);
}

void LayoutView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    auto bar = r.removeFromTop (theme::buttonH);
    allButton.setBounds (bar.removeFromLeft (60));
    bar.removeFromLeft (6);
    editButton.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (6);
    fitButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (12);
    orderButton.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (4);
    sortButton.setBounds (bar.removeFromLeft (104));
    bar.removeFromLeft (12);
    pixelsButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (4);
    mapButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (12);
    hint.setBounds (bar);
    hint.setVisible (bar.getWidth() > 200);

    // edit tools row
    r.removeFromTop (theme::gap);
    auto tools = r.removeFromTop (theme::buttonH - 4);
    const int spacers = (int) std::count_if (editTools.begin(), editTools.end(), [] (juce::TextButton* b) { return b->getButtonText().isEmpty(); });
    const int buttons = editTools.size() - spacers;
    const int w = juce::jlimit (44, 76, (tools.getWidth() - spacers * 14) / juce::jmax (1, buttons) - 3);
    for (auto* b : editTools)
    {
        if (b->getButtonText().isEmpty()) { tools.removeFromLeft (14); continue; }
        b->setBounds (tools.removeFromLeft (w));
        tools.removeFromLeft (3);
        b->setVisible (editing());
    }

    {
        auto g = groupRow();
        for (auto* b : groupButtons)
        {
            const int bw = juce::jlimit (60, 150, juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (13.0f), b->getButtonText())) + 26);
            b->setVisible (g.getWidth() >= bw + 90);
            if (b->isVisible()) { b->setBounds (g.removeFromLeft (bw)); g.removeFromLeft (4); }
        }
        newGroupButton.setBounds (g.removeFromLeft (80));
    }

    auto c = canvasArea();
    mapPanel->setBounds (c.getRight() - 290, c.getY() + 10, 280, juce::jmin (mapPanel->preferredHeight(), c.getHeight() - 20));

    // frame the rig the first time the view gets a real size
    if (! fittedOnce && ! items.empty() && c.getWidth() > 100 && c.getHeight() > 100)
    {
        fittedOnce = true;
        fitToView();
    }
}

//==============================================================================
void LayoutView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    dragStart = dragNow = p;
    if (! canvasArea().toFloat().contains (p)) return;

    const int fid = fixtureAt (p);
    const bool add = e.mods.isCommandDown() || e.mods.isShiftDown() || e.mods.isCtrlDown();

    if (e.mods.isMiddleButtonDown() || (e.mods.isAltDown() && fid < 0))
    {
        dragMode = DragMode::Pan;
        return;
    }

    if (ui.editMode == UiState::EditMode::Delete && fid >= 0)
    {
        bool inSel = false;
        { const juce::ScopedLock sl (engine.dataLock); inSel = engine.programmer.isSelected (fid); }
        deleteFixturesMenu (inSel ? selectedFixtures() : std::vector<int> { fid });
        return;
    }
    if (ui.editMode != UiState::EditMode::None && fid >= 0)
    {
        ui.say ("Copy / Move work on executors, presets, groups and cues - drag fixtures with Edit layout", true);
        return;
    }

    if (e.mods.isPopupMenu() && ! mapButton.getToggleState())
    {
        if (fid >= 0)
        {
            const juce::ScopedLock sl (engine.dataLock);
            if (! engine.programmer.isSelected (fid)) engine.programmer.select (fid, false);
        }
        ui.changed();
        canvasMenu();
        dragMode = DragMode::None;
        return;
    }

    if (mapButton.getToggleState())
    {
        bool onHandle = false;
        const int id = mapAt (p, onHandle);
        if (id != 0)
        {
            selectMap (id);
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& m : engine.pixelMaps) if (m.id == id) mapStart = m;
            dragMode = onHandle ? DragMode::ResizeMap : DragMode::MoveMap;
        }
        else
        {
            selectMap (0);
            dragMode = DragMode::NewMap;
            autoPlaceAll (true);       // pixel mapping needs real fixture positions
        }
        return;
    }

    if (fid >= 0)
    {
        const int target = editing() ? fid : targetAt (p);
        {
            const juce::ScopedLock sl (engine.dataLock);
            auto& prog = engine.programmer;
            if (editing())
            {
                if (! prog.isSelected (fid)) prog.select (fid, add);
            }
            else if (add) prog.toggle (target);
            else          prog.select (target, false);
        }
        ui.changed();

        if (editing())
        {
            dragMode = DragMode::Move;
            moveStart.clear();
            for (int f : selectedFixtures())
                if (auto* it = itemFor (f)) moveStart[f] = it->pos;
        }
        return;
    }

    dragMode = DragMode::Lasso;
    lassoAdd = add;
}

void LayoutView::mouseDrag (const juce::MouseEvent& e)
{
    const auto previous = dragNow;
    dragNow = e.position;

    if (dragMode == DragMode::Pan)
    {
        origin += dragNow - previous;
        repaint();
    }
    else if (dragMode == DragMode::Move)
    {
        const auto delta = (dragNow - dragStart) / zoom;
        for (auto& it : items)
        {
            auto s = moveStart.find (it.fid);
            if (s == moveStart.end()) continue;
            auto np = s->second + delta;
            it.pos = { std::round (np.x / kGrid) * kGrid, std::round (np.y / kGrid) * kGrid };
        }
        repaint();
    }
    else if (dragMode == DragMode::Lasso || dragMode == DragMode::NewMap)
    {
        repaint();
    }
    else if (dragMode == DragMode::MoveMap || dragMode == DragMode::ResizeMap)
    {
        const auto d = (dragNow - dragStart) / zoom;
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& m : engine.pixelMaps)
            if (m.id == selectedMap)
            {
                if (dragMode == DragMode::MoveMap) { m.x = mapStart.x + d.x; m.y = mapStart.y + d.y; }
                else { m.w = juce::jmax (16.0f, mapStart.w + d.x); m.h = juce::jmax (16.0f, mapStart.h + d.y); }
            }
        repaint();
    }
}

void LayoutView::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::NewMap)
    {
        auto a = toLayout (dragStart), b = toLayout (dragNow);
        auto r = juce::Rectangle<float> (a, b);
        if (r.getWidth() > 8.0f && r.getHeight() > 8.0f)
        {
            int id = 0;
            {
                const juce::ScopedLock sl (engine.dataLock);
                for (auto& m : engine.pixelMaps) id = juce::jmax (id, m.id);
                PixelMap m;
                m.id = ++id;
                m.name = "Map " + juce::String (id);
                m.x = r.getX(); m.y = r.getY(); m.w = r.getWidth(); m.h = r.getHeight();
                engine.pixelMaps.push_back (m);
            }
            selectMap (id);
        }
        dragMode = DragMode::None;
        repaint();
        return;
    }

    if (dragMode == DragMode::Move)
    {
        std::vector<int> moved;
        for (auto& [fid, p] : moveStart) moved.push_back (fid);
        commitPositions (moved);
    }
    else if (dragMode == DragMode::Lasso)
    {
        auto rect = juce::Rectangle<float> (dragStart, dragNow);
        const auto dir = dragNow - dragStart;
        const float len = std::hypot (dir.x, dir.y);

        if (len > 4.0f)
        {
            // fixtures (or pixels) inside, ordered by their position along the drag direction
            const bool pixels = pixelsButton.getToggleState();
            std::vector<std::pair<float, int>> hits;
            auto consider = [&] (juce::Point<float> s, int target, float slack)
            {
                if (! rect.expanded (slack).contains (s)) return;
                const auto rel = s - dragStart;
                hits.push_back ({ (rel.x * dir.x + rel.y * dir.y) / len, target });
            };
            for (auto& it : items)
            {
                if (pixels && it.hasCells())
                    for (int c = 1; c <= it.cellCount(); ++c)
                        consider (cellCentre (it, c), cellTarget (it.fid, c), 0.0f);
                else
                    consider (toScreen (it.pos), it.fid, kFixtureRadius * zoom * 0.5f);
            }
            if (orderButton.getToggleState())
                for (auto& h : hits) h.first = (float) targetFid (h.second) * 1000.0f + (float) targetCell (h.second);
            std::stable_sort (hits.begin(), hits.end());

            {
                const juce::ScopedLock sl (engine.dataLock);
                auto& prog = engine.programmer;
                std::vector<int> sel = lassoAdd ? prog.selection() : std::vector<int>();
                for (auto& [d, t] : hits)
                    if (std::find (sel.begin(), sel.end(), t) == sel.end())
                        sel.push_back (t);
                prog.setSelection (sel);
            }
            ui.changed();
        }
        else if (! lassoAdd)
        {
            { const juce::ScopedLock sl (engine.dataLock); engine.programmer.clearSelection(); }
            ui.changed();
        }
    }
    dragMode = DragMode::None;
    repaint();
}

void LayoutView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        mouseMagnify (e, 1.0f + w.deltaY * 0.6f);
        return;
    }
    origin += juce::Point<float> (w.deltaX, w.deltaY) * 220.0f;
    repaint();
}

void LayoutView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    const auto anchor = toLayout (e.position);
    zoom = juce::jlimit (0.3f, 3.0f, zoom * scale);
    const auto after = toScreen (anchor);
    origin += e.position - after;
    repaint();
}

//==============================================================================
juce::Rectangle<float> LayoutView::mapScreenRect (const PixelMap& m) const
{
    return juce::Rectangle<float> (toScreen ({ m.x, m.y }), toScreen ({ m.x + m.w, m.y + m.h }));
}

int LayoutView::mapAt (juce::Point<float> screen, bool& onResizeHandle) const
{
    const juce::ScopedLock sl (engine.dataLock);
    for (auto it = engine.pixelMaps.rbegin(); it != engine.pixelMaps.rend(); ++it)
    {
        auto r = mapScreenRect (*it);
        if (r.getBottomRight().getDistanceFrom (screen) < 14.0f) { onResizeHandle = true; return it->id; }
        if (r.contains (screen)) { onResizeHandle = false; return it->id; }
    }
    return 0;
}

void LayoutView::selectMap (int id)
{
    selectedMap = id;
    PixelMap copy;
    bool found = false;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& m : engine.pixelMaps) if (m.id == id) { copy = m; found = true; }
    }
    if (found) mapPanel->load (copy);
    mapPanel->setVisible (found && mapButton.getToggleState());
    resized();
    repaint();
}

void LayoutView::paintMaps (juce::Graphics& g)
{
    std::vector<PixelMap> maps;
    { const juce::ScopedLock sl (engine.dataLock); maps = engine.pixelMaps; }
    const double t = DmxEngine::now();
    const auto mapColour = theme::familyColour (Family::Beam);
    const float speed = Masters::speedMultiplier (engine.masters.dimmerSpeed.load());

    for (auto& m : maps)
    {
        auto r = mapScreenRect (m);
        // live preview of the source, faint, under the fixtures (effects: phase estimated from the clock)
        const double cycles = t * (double) m.effect.bpm / 60.0 * (double) speed;
        const int nx = 32, ny = juce::jlimit (4, 24, (int) (32.0f * m.h / juce::jmax (1.0f, m.w)));
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
            {
                RGBf c;
                if (! pixelmap::sampleMap (m, &engine.video, ((float) x + 0.5f) / (float) nx, ((float) y + 0.5f) / (float) ny, t, c,
                                           cycles, engine.masters.size.load())) continue;
                g.setColour (juce::Colour::fromFloatRGBA (c.r, c.g, c.b, m.enabled ? 0.28f : 0.08f));
                g.fillRect (r.getX() + r.getWidth() * (float) x / (float) nx, r.getY() + r.getHeight() * (float) y / (float) ny,
                            r.getWidth() / (float) nx + 0.5f, r.getHeight() / (float) ny + 0.5f);
            }
        const bool sel = m.id == selectedMap && mapButton.getToggleState();
        juce::Path outline;
        outline.addRectangle (r);
        juce::Path dashed;
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (sel ? 2.0f : 1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (sel ? mapColour : mapColour.withAlpha (0.7f));
        g.fillPath (dashed);
        g.setFont (theme::font (11.5f, true));
        const auto label = m.source == pixelmap::effectSource() ? "Effect: " + m.effect.form : m.source;
        g.drawText (m.name + "  -  " + label, r.withHeight (16.0f).translated (4.0f, -17.0f).withWidth (juce::jmax (r.getWidth(), 220.0f)),
                    juce::Justification::centredLeft, true);
        if (sel)
            g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre (r.getBottomRight()));
    }

    if (dragMode == DragMode::NewMap)
    {
        g.setColour (mapColour.withAlpha (0.15f));
        g.fillRect (juce::Rectangle<float> (dragStart, dragNow));
        g.setColour (mapColour);
        g.drawRect (juce::Rectangle<float> (dragStart, dragNow), 1.5f);
    }
}

//==============================================================================
void LayoutView::selectAll()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        std::vector<int> all;
        for (auto& it : items)
        {
            if (pixelsButton.getToggleState() && it.hasCells())
                for (int c = 1; c <= it.cellCount(); ++c) all.push_back (cellTarget (it.fid, c));
            else
                all.push_back (it.fid);
        }
        engine.programmer.setSelection (all);
    }
    ui.changed();
}

std::vector<int> LayoutView::selectedFixtures() const
{
    std::vector<int> sel, out;
    { const juce::ScopedLock sl (engine.dataLock); sel = engine.programmer.selection(); }
    for (int t : sel)
        if (std::find (out.begin(), out.end(), targetFid (t)) == out.end())
            out.push_back (targetFid (t));
    return out;
}

LayoutView::Item* LayoutView::itemFor (int fid)
{
    for (auto& it : items) if (it.fid == fid) return &it;
    return nullptr;
}

void LayoutView::arrange (const juce::String& how)
{
    const auto sel = selectedFixtures();
    if (sel.size() < 2) return;

    juce::Point<float> start = itemFor (sel[0]) != nullptr ? itemFor (sel[0])->pos : juce::Point<float>();
    const int n = (int) sel.size();
    for (int i = 0; i < n; ++i)
    {
        auto* it = itemFor (sel[(size_t) i]);
        if (it == nullptr) continue;
        auto& p = it->pos;
        if (how == "line")
            p = start + juce::Point<float> ((float) i * kSpacing, 0.0f);
        else if (how == "grid")
        {
            const int cols = (int) std::ceil (std::sqrt ((float) n));
            p = start + juce::Point<float> ((float) (i % cols) * kSpacing, (float) (i / cols) * kSpacing);
        }
        else // circle, starting at 12 o'clock and going clockwise
        {
            const float radius = juce::jmax (kSpacing, (float) n * kSpacing / juce::MathConstants<float>::twoPi);
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
            p = start + juce::Point<float> (radius * std::sin (a), radius - radius * std::cos (a));
        }
    }
    commitPositions (sel);
}

static juce::Point<float> centreOf (const std::vector<juce::Point<float>>& pts)
{
    float x0 = pts[0].x, x1 = x0, y0 = pts[0].y, y1 = y0;
    for (auto& p : pts) { x0 = juce::jmin (x0, p.x); x1 = juce::jmax (x1, p.x); y0 = juce::jmin (y0, p.y); y1 = juce::jmax (y1, p.y); }
    return { (x0 + x1) * 0.5f, (y0 + y1) * 0.5f };
}

void LayoutView::rotateSelection (float degrees)
{
    const auto sel = selectedFixtures();
    std::vector<Item*> its;
    std::vector<juce::Point<float>> pts;
    for (int f : sel) if (auto* it = itemFor (f)) { its.push_back (it); pts.push_back (it->pos); }
    if (its.empty()) return;
    const auto c = centreOf (pts);
    const float a = juce::degreesToRadians (degrees), cs = std::cos (a), sn = std::sin (a);
    for (auto* it : its)
    {
        const auto d = it->pos - c;
        it->pos = { std::round (c.x + d.x * cs - d.y * sn), std::round (c.y + d.x * sn + d.y * cs) };
        it->rotation = std::fmod (it->rotation + degrees + 360.0f, 360.0f);
    }
    commitPositions (sel);
}

void LayoutView::flipSelection (bool horizontal)
{
    const auto sel = selectedFixtures();
    std::vector<Item*> its;
    std::vector<juce::Point<float>> pts;
    for (int f : sel) if (auto* it = itemFor (f)) { its.push_back (it); pts.push_back (it->pos); }
    if (its.empty()) return;
    const auto c = centreOf (pts);
    for (auto* it : its)
    {
        // a mirror of the world = mirror the cell grid and turn the rotation the other way
        if (horizontal) { it->pos.x = 2.0f * c.x - it->pos.x; it->rotation = std::fmod (360.0f - it->rotation, 360.0f); }
        else            { it->pos.y = 2.0f * c.y - it->pos.y; it->rotation = std::fmod (540.0f - it->rotation, 360.0f); }
        it->mirror = ! it->mirror;
    }
    commitPositions (sel);
}

void LayoutView::alignSelection (const juce::String& how)
{
    const auto sel = selectedFixtures();
    std::vector<Item*> its;
    std::vector<juce::Point<float>> pts;
    for (int f : sel) if (auto* it = itemFor (f)) { its.push_back (it); pts.push_back (it->pos); }
    if (its.size() < 2) return;
    float x0 = pts[0].x, x1 = x0, y0 = pts[0].y, y1 = y0;
    for (auto& p : pts) { x0 = juce::jmin (x0, p.x); x1 = juce::jmax (x1, p.x); y0 = juce::jmin (y0, p.y); y1 = juce::jmax (y1, p.y); }
    for (auto* it : its)
    {
        if (how == "left")        it->pos.x = x0;
        else if (how == "right")  it->pos.x = x1;
        else if (how == "centre") it->pos.x = std::round ((x0 + x1) * 0.5f);
        else if (how == "top")    it->pos.y = y0;
        else if (how == "bottom") it->pos.y = y1;
        else                      it->pos.y = std::round ((y0 + y1) * 0.5f);
    }
    commitPositions (sel);
}

void LayoutView::distributeSelection (bool horizontal)
{
    const auto sel = selectedFixtures();
    std::vector<Item*> its;
    for (int f : sel) if (auto* it = itemFor (f)) its.push_back (it);
    if (its.size() < 3) return;
    auto key = [horizontal] (const Item* i) { return horizontal ? i->pos.x : i->pos.y; };
    std::stable_sort (its.begin(), its.end(), [&] (const Item* a, const Item* b) { return key (a) < key (b); });
    const float lo = key (its.front()), hi = key (its.back());
    for (size_t i = 0; i < its.size(); ++i)
    {
        const float v = std::round (lo + (hi - lo) * (float) i / (float) (its.size() - 1));
        (horizontal ? its[i]->pos.x : its[i]->pos.y) = v;
    }
    commitPositions (sel);
}

void LayoutView::scaleSpacing (float factor)
{
    const auto sel = selectedFixtures();
    std::vector<Item*> its;
    std::vector<juce::Point<float>> pts;
    for (int f : sel) if (auto* it = itemFor (f)) { its.push_back (it); pts.push_back (it->pos); }
    if (its.size() < 2) return;
    const auto c = centreOf (pts);
    for (auto* it : its)
        it->pos = { std::round (c.x + (it->pos.x - c.x) * factor), std::round (c.y + (it->pos.y - c.y) * factor) };
    commitPositions (sel);
}

void LayoutView::autoPlaceAll (bool onlyMissing)
{
    std::vector<int> fids;
    if (onlyMissing)
    {
        // keep fixtures exactly where they are shown: just make those positions real
        for (auto& it : items)
            if (! it.saved) fids.push_back (it.fid);
        if (! fids.empty()) commitPositions (fids);
        return;
    }
    float x = kSpacing * 0.75f, y = kSpacing * 0.75f;
    juce::String lastPrefix;
    for (auto& it : items)
    {
        if (onlyMissing && it.saved) continue;
        const auto prefix = juce::String (it.fid / 100);
        if (lastPrefix.isNotEmpty() && prefix != lastPrefix) { x = kSpacing * 0.75f; y += juce::jmax (kSpacing * 1.3f, it.extentH * FixtureType::cellSpacing + 40.0f); }
        lastPrefix = prefix;
        it.pos = { x, y };
        fids.push_back (it.fid);
        x += juce::jmax (kSpacing, it.extentW * FixtureType::cellSpacing + 24.0f);
        if (x > kSpacing * 16) { x = kSpacing * 0.75f; y += juce::jmax (kSpacing * 1.3f, it.extentH * FixtureType::cellSpacing + 40.0f); }
    }
    commitPositions (fids);
}

void LayoutView::fitToView()
{
    if (items.empty()) return;
    // (Rectangle::getUnion ignores empty rectangles, so track min/max directly)
    float x0 = items[0].pos.x, y0 = items[0].pos.y, x1 = x0, y1 = y0;
    for (auto& it : items)
    {
        x0 = juce::jmin (x0, it.pos.x); y0 = juce::jmin (y0, it.pos.y);
        x1 = juce::jmax (x1, it.pos.x); y1 = juce::jmax (y1, it.pos.y);
    }
    float extra = kFixtureRadius;
    for (auto& it : items)
        extra = juce::jmax (extra, juce::jmax (it.extentW, it.extentH) * FixtureType::cellSpacing * 0.5f);
    auto bounds = juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1).expanded (extra + 24.0f);

    auto c = canvasArea().toFloat().reduced (10.0f);
    zoom = juce::jlimit (0.3f, 2.0f, juce::jmin (c.getWidth() / bounds.getWidth(), c.getHeight() / bounds.getHeight()));
    origin = { 10.0f + (c.getWidth() - bounds.getWidth() * zoom) * 0.5f - bounds.getX() * zoom,
               10.0f + (c.getHeight() - bounds.getHeight() * zoom) * 0.5f - bounds.getY() * zoom };
    repaint();
}

void LayoutView::commitPositions (const std::vector<int>& fids)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& it : items)
            if (std::find (fids.begin(), fids.end(), it.fid) != fids.end())
            {
                engine.patch.setLayoutPosition (it.fid, it.pos.x, it.pos.y);
                engine.patch.setLayoutOrientation (it.fid, it.rotation, it.mirror);
                it.saved = true;
            }
    }
    repaint();
}

//==============================================================================
void LayoutView::rebuildGroupButtons()
{
    juce::String sig;
    std::vector<std::pair<int, juce::String>> groups;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& [n, p] : engine.presets.pool (PresetPool::Group))
        {
            groups.push_back ({ n, p.name.isNotEmpty() ? p.name : "Group " + juce::String (n) });
            sig << n << ":" << groups.back().second << ";";
        }
    }
    if (sig == groupSignature && groupButtons.size() == (int) groups.size()) return;
    groupSignature = sig;
    groupButtons.clear();
    for (auto& [n, name] : groups)
    {
        auto* b = groupButtons.add (new GroupButton (name, n));
        b->setTooltip ("Group " + juce::String (n) + ": tap to select, right-click to update, rename or delete");
        b->setColour (juce::TextButton::buttonColourId, theme::panel2);
        b->onTap = [this] (int num, bool popup, juce::Component* c) { groupTapped (num, popup, c); };
        addAndMakeVisible (b);
    }
    resized();
}

void LayoutView::groupTapped (int number, bool popup, juce::Component* button)
{
    juce::String name;
    { const juce::ScopedLock sl (engine.dataLock); if (auto* p = engine.presets.find (PresetPool::Group, number)) name = p->name; }

    if (! popup && editops::tap (engine, ui, editops::preset (PresetPool::Group, number, name), true, this))
        return;

    if (! popup)
    {
        { const juce::ScopedLock sl (engine.dataLock);
          engine.presets.recall (PresetPool::Group, number, engine.programmer, engine.patch, engine.library,
                                 juce::ModifierKeys::currentModifiers.isShiftDown() || juce::ModifierKeys::currentModifiers.isCommandDown()); }
        ui.changed();
        return;
    }

    juce::PopupMenu m;
    m.addSectionHeader (name.isNotEmpty() ? name : "Group " + juce::String (number));
    m.addItem (1, "Update with the selection");
    m.addItem (2, "Rename...");
    m.addSeparator();
    m.addItem (3, "Delete...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                     [safe = juce::Component::SafePointer<LayoutView> (this), number, name] (int r)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        if (r == 1)
        {
            {
                const juce::ScopedLock sl (self.engine.dataLock);
                auto sel = self.engine.programmer.selection();
                if (auto* p = self.engine.presets.find (PresetPool::Group, number); p != nullptr && ! sel.empty())
                {
                    auto copy = *p;
                    self.engine.presets.remove (PresetPool::Group, number);
                    const int made = self.engine.presets.setGroup (copy.name, sel);
                    if (made != number) self.engine.presets.copy (PresetPool::Group, made, number, true);
                }
            }
            self.engine.showChanged();
            self.ui.say ("Updated " + name);
        }
        if (r == 2)
            ui::askForText (safe, "Rename group", "Group " + juce::String (number), name, [safe, number] (const juce::String& t)
            {
                if (safe == nullptr || t.isEmpty()) return;
                { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.presets.rename (PresetPool::Group, number, t); }
                safe->engine.showChanged();
            });
        if (r == 3)
            editops::confirmDelete (safe, "group \"" + name + "\"", [safe, number]
            {
                if (safe == nullptr) return;
                { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.presets.remove (PresetPool::Group, number); }
                safe->engine.showChanged();
            });
    });
}

void LayoutView::createGroupFromSelection()
{
    std::vector<int> sel;
    { const juce::ScopedLock sl (engine.dataLock); sel = engine.programmer.selection(); }
    if (sel.empty()) { ui.say ("Select fixtures first, then make a group", true); return; }
    ui::askForText (this, "New group", juce::String ((int) sel.size()) + " fixtures (in selection order)", {},
                    [safe = juce::Component::SafePointer<LayoutView> (this), sel] (const juce::String& name)
    {
        if (safe == nullptr) return;
        int n = 0;
        {
            const juce::ScopedLock sl (safe->engine.dataLock);
            n = safe->engine.presets.setGroup (name.isNotEmpty() ? name : "Group", sel);
            safe->engine.programmer.lastGroup = n;
        }
        safe->engine.showChanged();
        safe->ui.say ("Group " + juce::String (n) + " \"" + name + "\" stored");
    });
}

void LayoutView::canvasMenu()
{
    std::vector<int> sel = selectedFixtures();
    int hidden = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& f : engine.patch.fixtures()) hidden += f.hiddenInLayout ? 1 : 0;
    }
    const auto n = juce::String ((int) sel.size());
    juce::PopupMenu m;
    m.addItem (1, "Create group from selection (" + n + ")...", ! sel.empty());
    juce::PopupMenu arr;
    arr.addItem (10, "Line (selection order)");
    arr.addItem (11, "Grid");
    arr.addItem (12, "Circle");
    arr.addSeparator();
    arr.addItem (13, "Align left");
    arr.addItem (14, "Align centre");
    arr.addItem (15, "Align right");
    arr.addItem (16, "Align top");
    arr.addItem (17, "Align middle");
    arr.addItem (18, "Align bottom");
    arr.addSeparator();
    arr.addItem (19, "Space evenly left to right");
    arr.addItem (20, "Space evenly top to bottom");
    arr.addItem (21, "Spread out");
    arr.addItem (22, "Closer together");
    arr.addSeparator();
    arr.addItem (23, "Rotate +90");
    arr.addItem (24, "Flip left-right");
    m.addSubMenu ("Arrange selection", arr, sel.size() > 1);
    m.addSeparator();
    m.addItem (2, "Delete " + n + " fixtures...", ! sel.empty());
    m.addItem (3, "Show hidden fixtures (" + juce::String (hidden) + ")", hidden > 0);
    m.showMenuAsync ({}, [safe = juce::Component::SafePointer<LayoutView> (this), sel] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->createGroupFromSelection();
        if (r == 2) safe->deleteFixturesMenu (sel);
        if (r >= 10 && r <= 24)
        {
            auto& v = *safe;
            switch (r)
            {
                case 10: v.arrange ("line"); break;
                case 11: v.arrange ("grid"); break;
                case 12: v.arrange ("circle"); break;
                case 13: v.alignSelection ("left"); break;
                case 14: v.alignSelection ("centre"); break;
                case 15: v.alignSelection ("right"); break;
                case 16: v.alignSelection ("top"); break;
                case 17: v.alignSelection ("middle"); break;
                case 18: v.alignSelection ("bottom"); break;
                case 19: v.distributeSelection (true); break;
                case 20: v.distributeSelection (false); break;
                case 21: v.scaleSpacing (1.25f); break;
                case 22: v.scaleSpacing (0.8f); break;
                case 23: v.rotateSelection (90.0f); break;
                default: v.flipSelection (true); break;
            }
            v.repaint();
        }
        if (r == 3)
        {
            {
                const juce::ScopedLock sl (safe->engine.dataLock);
                std::vector<int> fids;
                for (auto& f : safe->engine.patch.fixtures()) if (f.hiddenInLayout) fids.push_back (f.fid);
                for (int fid : fids) safe->engine.patch.setHiddenInLayout (fid, false);
            }
            safe->engine.showChanged();
        }
    });
}

void LayoutView::deleteFixturesMenu (std::vector<int> fids)
{
    ui.setEditMode (UiState::EditMode::None);
    if (fids.empty()) return;
    const auto what = fids.size() == 1 ? "fixture " + juce::String (fids.front()) : juce::String ((int) fids.size()) + " fixtures";
    juce::PopupMenu m;
    m.addSectionHeader ("Delete " + what);
    m.addItem (1, "Remove from the Layout (stays patched)");
    m.addItem (2, "Delete from the patch");
    m.addSeparator();
    m.addItem (3, "Cancel");
    m.showMenuAsync ({}, [safe = juce::Component::SafePointer<LayoutView> (this), fids, what] (int r)
    {
        if (safe == nullptr || (r != 1 && r != 2)) return;
        const bool fromPatch = r == 2;
        editops::confirmDelete (safe, what + (fromPatch ? " from the patch" : " from the Layout"), [safe, fids, fromPatch]
        {
            if (safe == nullptr) return;
            {
                const juce::ScopedLock sl (safe->engine.dataLock);
                auto& prog = safe->engine.programmer;
                auto sel = prog.selection();
                sel.erase (std::remove_if (sel.begin(), sel.end(), [&fids] (int t)
                           { return std::find (fids.begin(), fids.end(), targetFid (t)) != fids.end(); }), sel.end());
                prog.setSelection (sel);
                for (int fid : fids)
                {
                    if (fromPatch) safe->engine.patch.remove (fid);
                    else safe->engine.patch.setHiddenInLayout (fid, true);
                }
            }
            safe->engine.showChanged();
            safe->ui.changed();
        });
    });
}

} // namespace pl
