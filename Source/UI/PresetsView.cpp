#include "PresetsView.h"
#include "EditOps.h"

namespace pl
{

static juce::Colour poolColour (PresetPool p)
{
    if (p == PresetPool::Group)  return theme::accent;
    if (p == PresetPool::Effect) return theme::familyColour (Family::Beam);
    if (p == PresetPool::All)    return theme::text;
    return theme::familyColour (poolFamily (p));
}

//==============================================================================
class PresetsView::Tile : public juce::Component
{
public:
    Tile (PresetsView& o, PresetPool p, int n) : owner (o), pool (p), number (n)
    {
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        juce::String name, detail;
        std::vector<juce::uint32> colours;
        juce::uint32 label = 0;
        bool filled = false;
        {
            const juce::ScopedLock sl (owner.engine.dataLock);
            if (auto* p = owner.engine.presets.find (pool, number))
            {
                filled = true;
                name = p->name;
                colours = p->swatches;
                label = p->label;
                if (pool == PresetPool::Group)
                    detail = juce::String ((int) p->fixtures.size()) + " fixtures";
                else if (pool == PresetPool::Effect)
                {
                    for (auto& e : p->effects)
                        detail << (detail.isEmpty() ? "" : ", ") << e.form;
                }
                else
                {
                    detail = juce::String ((int) p->values.size()) + " fix";
                    if (! p->effects.empty()) detail << " + fx";
                }
            }
        }

        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto col = label != 0 ? juce::Colour (label) : poolColour (pool);
        const bool coloured = filled && ! colours.empty();

        if (coloured)
        {
            // the tile is the colour: one band per fixture, so gradients read at a glance
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 6.0f);
            g.reduceClipRegion (clip);
            const float w = r.getWidth() / (float) colours.size();
            for (size_t i = 0; i < colours.size(); ++i)
            {
                g.setColour (juce::Colour (colours[i]));
                g.fillRect (r.getX() + w * (float) i, r.getY(), w + 0.5f, r.getHeight());
            }
            if (isMouseOver()) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect (r); }
            // dark strip so the name stays readable on any colour
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (r.withTrimmedTop (r.getHeight() * 0.52f));
        }
        else
        {
            auto bg = filled ? theme::panel2.brighter (0.05f) : theme::bg;
            if (filled && label != 0) bg = bg.interpolatedWith (juce::Colour (label), 0.18f);
            if (isMouseOver()) bg = bg.brighter (0.08f);
            g.setColour (bg);
            g.fillRoundedRectangle (r, 6.0f);
            if (filled)
            {
                g.setColour (col);
                g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);
            }
        }

        if (filled && label != 0)
        {
            g.setColour (juce::Colour (label));
            g.fillRoundedRectangle (r.withWidth (5.0f), 2.5f);
        }

        const bool armed = owner.ui.storeArmed;
        g.setColour (armed ? theme::danger.withAlpha (filled ? 0.9f : 0.5f) : (filled ? col.withAlpha (0.8f) : theme::line));
        g.drawRoundedRectangle (r, 6.0f, armed ? 1.5f : (label != 0 ? 1.5f : 1.0f));

        const bool brightTop = coloured && juce::Colour (colours.front()).getPerceivedBrightness() > 0.6f;
        g.setColour (filled ? (brightTop ? juce::Colours::black : theme::text) : theme::textDim.withAlpha (0.5f));
        g.setFont (theme::font (11.0f, true));
        g.drawText (juce::String (number), r.reduced (8, 4), juce::Justification::topLeft);

        if (filled)
        {
            auto textArea = coloured ? r.withTrimmedTop (r.getHeight() * 0.52f) : r.withTrimmedTop (r.getHeight() * 0.3f);
            g.setColour (theme::text);
            g.setFont (theme::font (13.0f, true));
            g.drawFittedText (name, textArea.reduced (8, 1).withTrimmedBottom (coloured ? 0.0f : 14.0f).toNearestInt(),
                              juce::Justification::centredLeft, coloured ? 1 : 2, 0.85f);
            if (! coloured)
            {
                g.setColour (theme::textDim);
                g.setFont (theme::font (11.0f));
                g.drawText (detail, r.reduced (8, 3).removeFromBottom (14.0f), juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (e.mods.isPopupMenu()) owner.tileMenu (pool, number, this);
        else owner.tileClicked (pool, number, e.mods);
    }

private:
    PresetsView& owner;
    const PresetPool pool;
    const int number;
};

//==============================================================================
class PresetsView::PoolRow : public juce::Component
{
public:
    PoolRow (PresetsView& o, PresetPool p) : owner (o), pool (p) {}

    /** Rebuilds tiles: enough for every stored preset plus spare empty ones. */
    void refresh (int width)
    {
        int highest = 0;
        {
            const juce::ScopedLock sl (owner.engine.dataLock);
            for (auto& [n, preset] : owner.engine.presets.pool (pool)) highest = juce::jmax (highest, n);
        }
        const int perLine = juce::jmax (1, (width - labelW) / tileW);
        const int needed = juce::jmax (perLine, ((highest + 4 + perLine - 1) / perLine) * perLine);
        if (needed != tiles.size())
        {
            tiles.clear();
            for (int i = 1; i <= needed; ++i)
                addAndMakeVisible (tiles.add (new Tile (owner, pool, i)));
        }
        lines = (tiles.size() + perLine - 1) / perLine;
        setSize (width, lines * tileH + 8);
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds (labelW + (i % perLine) * tileW, 4 + (i / perLine) * tileH, tileW - 2, tileH - 2);
        for (auto* t : tiles) t->repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withWidth (labelW - 8).reduced (0, 6);
        g.setColour (poolColour (pool));
        g.fillRoundedRectangle (r.removeFromLeft (4).toFloat(), 2.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (poolName (pool), r.withTrimmedLeft (10).withHeight (tileH - 8), juce::Justification::centredLeft);
    }

    static constexpr int labelW = 110, tileW = 104, tileH = 64;

private:
    PresetsView& owner;
    const PresetPool pool;
    juce::OwnedArray<Tile> tiles;
    int lines = 1;
};

//==============================================================================
PresetsView::PresetsView (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    ui::styleHeader (header, "PRESETS");
    hint.setFont (theme::font (12.5f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::centredRight);
    hint.setText ("Tap a preset to apply it to the selection  -  tap an empty tile to store  -  Store + tile to overwrite or merge  -  right-click to rename",
                  juce::dontSendNotification);
    addAndMakeVisible (header);
    addAndMakeVisible (hint);

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (auto p : allPresetPools())
        holder.addAndMakeVisible (rows.add (new PoolRow (*this, p)));

    engine.addChangeListener (this);
    ui.addChangeListener (this);
}

PresetsView::~PresetsView()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void PresetsView::changeListenerCallback (juce::ChangeBroadcaster*) { resized(); }

void PresetsView::paint (juce::Graphics& g) { ui::paintPanel (g, panel); }

void PresetsView::resized()
{
    panel = getLocalBounds().reduced (theme::gap);
    auto r = panel.reduced (12);
    auto top = r.removeFromTop (24);
    header.setBounds (top.removeFromLeft (100));
    hint.setBounds (top);
    r.removeFromTop (6);
    viewport.setBounds (r);

    const int w = viewport.getMaximumVisibleWidth();
    int y = 0;
    for (auto* row : rows)
    {
        row->refresh (w);
        row->setTopLeftPosition (0, y);
        y += row->getHeight();
    }
    holder.setSize (w, juce::jmax (1, y));
}

//==============================================================================
void PresetsView::tileClicked (PresetPool pool, int number, const juce::ModifierKeys& mods)
{
    bool exists = false;
    {
        const juce::ScopedLock sl (engine.dataLock);
        exists = engine.presets.find (pool, number) != nullptr;
    }

    if (ui.editMode != UiState::EditMode::None)
    {
        juce::String name;
        { const juce::ScopedLock sl (engine.dataLock); if (auto* p = engine.presets.find (pool, number)) name = p->name; }
        editops::tap (engine, ui, editops::preset (pool, number, name), exists, this);
        return;
    }

    if (ui.storeArmed && exists)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Store onto " + poolName (pool) + " " + juce::String (number));
        m.addItem (1, "Overwrite");
        m.addItem (2, "Merge");
        m.addSeparator();
        m.addItem (3, "Cancel");
        m.showMenuAsync ({}, [safe = juce::Component::SafePointer<PresetsView> (this), pool, number] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1)      safe->store (pool, number, PresetStoreMode::Overwrite);
            else if (r == 2) safe->store (pool, number, PresetStoreMode::Merge);
            else { safe->ui.storeArmed = false; safe->ui.changed(); }
        });
        return;
    }

    if (ui.storeArmed || ! exists)
    {
        store (pool, number, PresetStoreMode::Overwrite);
        return;
    }

    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.presets.recall (pool, number, engine.programmer, engine.patch, engine.library,
                               mods.isCommandDown() || mods.isShiftDown() || mods.isCtrlDown());
    }
    ui.changed();
}

void PresetsView::store (PresetPool pool, int number, PresetStoreMode mode)
{
    juce::String err;
    {
        const juce::ScopedLock sl (engine.dataLock);
        err = engine.presets.store (pool, number, engine.programmer, engine.patch, engine.library, mode);
    }
    ui.storeArmed = false;
    ui.changed();
    engine.showChanged();
    if (onMessage)
        onMessage (err.isNotEmpty() ? err : "Stored " + poolName (pool) + " " + juce::String (number), err.isNotEmpty());
}

void PresetsView::tileMenu (PresetPool pool, int number, juce::Component* target)
{
    bool exists = false;
    {
        const juce::ScopedLock sl (engine.dataLock);
        exists = engine.presets.find (pool, number) != nullptr;
    }
    juce::PopupMenu m;
    m.addSectionHeader (poolName (pool) + " " + juce::String (number));
    m.addItem (1, exists ? "Overwrite with programmer" : "Store programmer");
    m.addItem (2, "Merge programmer", exists);
    m.addItem (3, "Rename / colour label...", exists);
    m.addSeparator();
    m.addItem (4, "Delete", exists);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [safe = juce::Component::SafePointer<PresetsView> (this), pool, number] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->store (pool, number, PresetStoreMode::Overwrite);
        if (r == 2) safe->store (pool, number, PresetStoreMode::Merge);
        if (r == 3) safe->rename (pool, number);
        if (r == 4)
            editops::confirmDelete (safe, poolName (pool) + " " + juce::String (number), [safe, pool, number]
            {
                if (safe == nullptr) return;
                { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.presets.remove (pool, number); }
                safe->engine.showChanged();
            });
    });
}

void PresetsView::rename (PresetPool pool, int number)
{
    juce::String current;
    juce::uint32 currentLabel = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (auto* p = engine.presets.find (pool, number)) { current = p->name; currentLabel = p->label; }
    }

    static const std::vector<std::pair<juce::String, juce::uint32>> labels {
        { "No label", 0 }, { "Red", 0xffe5484d }, { "Orange", 0xfff2803a }, { "Yellow", 0xfff2d23a },
        { "Green", 0xff3ccf7a }, { "Cyan", 0xff3cc8d8 }, { "Blue", 0xff4f8cff }, { "Purple", 0xff9d7bff },
        { "Pink", 0xffe056c8 }, { "Grey", 0xff8b919a } };

    auto* aw = new juce::AlertWindow ("Rename preset", poolName (pool) + " " + juce::String (number),
                                      juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("name", current);
    juce::StringArray names;
    int selected = 0;
    for (int i = 0; i < (int) labels.size(); ++i)
    {
        names.add (labels[(size_t) i].first);
        if (labels[(size_t) i].second == currentLabel) selected = i;
    }
    aw->addComboBox ("label", names, "Colour label");
    if (auto* cb = aw->getComboBoxComponent ("label")) cb->setSelectedItemIndex (selected);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<PresetsView> (this), aw, pool, number] (int r)
        {
            if (safe == nullptr || r != 1) return;
            const auto name = aw->getTextEditorContents ("name").trim();
            const int li = aw->getComboBoxComponent ("label")->getSelectedItemIndex();
            {
                const juce::ScopedLock sl (safe->engine.dataLock);
                safe->engine.presets.rename (pool, number, name);
                safe->engine.presets.setLabel (pool, number, li >= 0 && li < (int) labels.size() ? labels[(size_t) li].second : 0);
            }
            safe->engine.showChanged();
        }), true);
    if (auto* ed = aw->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

} // namespace pl
