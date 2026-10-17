#include "FixtureBuilderView.h"
#include "GdtfShareView.h"
#include "Engine/FixtureTemplates.h"
#include "Engine/ShowFile.h"
#include "CellLayoutEditor.h"

namespace pl
{

namespace
{
    enum Col { cHandle, cAttr, cCell, cCoarse, c16, cFine, cDefault, cHighlight, cRemove, cCount };

    std::array<juce::Rectangle<int>, cCount> rowColumns (juce::Rectangle<int> r)
    {
        std::array<juce::Rectangle<int>, cCount> c;
        c[cHandle]  = r.removeFromLeft (30);
        c[cRemove]  = r.removeFromRight (40);  r.removeFromRight (10);
        c[cHighlight] = r.removeFromRight (70); r.removeFromRight (8);
        c[cDefault] = r.removeFromRight (110); r.removeFromRight (10);
        c[cFine]    = r.removeFromRight (70);  r.removeFromRight (6);
        c[c16]      = r.removeFromRight (70);  r.removeFromRight (10);
        c[cCoarse]  = r.removeFromRight (70);  r.removeFromRight (10);
        c[cCell]    = r.removeFromRight (56);  r.removeFromRight (8);
        c[cAttr]    = r;
        return c;
    }
}

//==============================================================================
/** Strip showing each DMX channel of the fixture and what it carries. */
class FixtureBuilderView::ChannelMap : public juce::Component
{
public:
    void setType (const FixtureType& t)
    {
        labels = t.channelMap();
        families.clear();
        for (int i = 0; i < labels.size(); ++i) families.push_back (Family::None);
        for (auto& a : t.attributes)
        {
            if (a.coarse >= 1 && a.coarse <= labels.size()) families[(size_t) a.coarse - 1] = familyOf (a.attribute);
            if (a.fine >= 1 && a.fine <= labels.size())     families[(size_t) a.fine - 1] = familyOf (a.attribute);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int n = labels.size();
        if (n == 0) return;
        const float w = juce::jmin (90.0f, (float) getWidth() / (float) n);
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (1.5f);
            const bool used = labels[i].isNotEmpty();
            auto col = theme::familyColour (families[(size_t) i]);
            g.setColour (used ? col.withAlpha (0.22f) : theme::bg);
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (used ? col : theme::line);
            g.drawRoundedRectangle (cell, 4.0f, 1.0f);
            g.setColour (used ? theme::text : theme::textDim);
            g.setFont (theme::font (13.0f, true));
            g.drawText (juce::String (i + 1), cell.removeFromTop (cell.getHeight() * 0.5f), juce::Justification::centredBottom);
            g.setFont (theme::font (juce::jlimit (9.0f, 11.5f, w / 7.0f)));
            g.setColour (used ? theme::text.withAlpha (0.85f) : theme::textDim.withAlpha (0.6f));
            g.drawFittedText (used ? labels[i] : juce::String ("-"), cell.toNearestInt().reduced (2, 0),
                              juce::Justification::centredTop, 2, 0.8f);
        }
    }

private:
    juce::StringArray labels;
    std::vector<Family> families;
};

//==============================================================================
/** Holds the rows and draws the drop marker while dragging. */
class FixtureBuilderView::RowsHolder : public juce::Component
{
public:
    explicit RowsHolder (FixtureBuilderView& o) : owner (o) {}

    void paintOverChildren (juce::Graphics& g) override
    {
        if (owner.dragIndex < 0 || owner.dropIndex < 0) return;
        const int y = owner.dropIndex * rowHeight;
        g.setColour (theme::accent);
        g.fillRoundedRectangle (4.0f, (float) y - 2.0f, (float) getWidth() - 8.0f, 4.0f, 2.0f);
    }

    static constexpr int rowHeight = 48;

private:
    FixtureBuilderView& owner;
};

//==============================================================================
class FixtureBuilderView::AttrRow : public juce::Component
{
public:
    explicit AttrRow (FixtureBuilderView& o) : owner (o)
    {
        Family lastFamily = Family::None;
        auto& attrs = allAttributes();
        for (int i = 1; i < (int) attrs.size(); ++i)          // skip "None"
        {
            if (attrs[(size_t) i].family != lastFamily)
            {
                lastFamily = attrs[(size_t) i].family;
                attribute.addSectionHeading (familyName (lastFamily));
            }
            attribute.addItem (attrs[(size_t) i].label, i + 1);
        }
        attribute.onChange = [this] { push (false); };
        addAndMakeVisible (attribute);

        for (auto* e : { &coarse, &fine })
        {
            ui::styleEditor (*e, true);
            e->setInputRestrictions (3, "0123456789");
            e->setJustification (juce::Justification::centred);
            e->onReturnKey = [this] { push (true); };
            e->onFocusLost = [this] { push (true); };
            addAndMakeVisible (*e);
        }
        ui::styleEditor (cell, true);
        cell.setInputRestrictions (3, "0123456789");
        cell.setJustification (juce::Justification::centred);
        cell.setTooltip ("Cell number for pixel fixtures (empty = whole fixture)");
        cell.onReturnKey = [this] { push (false); };
        cell.onFocusLost = [this] { push (false); };
        addAndMakeVisible (cell);
        coarse.setTooltip ("DMX channel (inside the fixture)");
        fine.setTooltip ("Fine channel for 16-bit");

        sixteen.setButtonText ("16-bit");
        sixteen.setClickingTogglesState (true);
        sixteen.onClick = [this] { push (false); };
        addAndMakeVisible (sixteen);

        defaultValue.setSliderStyle (juce::Slider::LinearBar);
        defaultValue.setRange (0, 255, 1);
        defaultValue.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.35f));
        defaultValue.setTooltip ("Default DMX value");
        defaultValue.onValueChange = [this] { push (false); };
        addAndMakeVisible (defaultValue);

        ui::styleEditor (highlight, true);
        highlight.setInputRestrictions (3, "0123456789");
        highlight.setJustification (juce::Justification::centred);
        highlight.setTooltip ("Highlight value (0-255). Empty = automatic: full, open white, shutter open; pan/tilt/zoom stay");
        highlight.onReturnKey = [this] { push (false); };
        highlight.onFocusLost = [this] { push (false); };
        addAndMakeVisible (highlight);

        remove.setButtonText (juce::CharPointer_UTF8 ("\xe2\x9c\x95"));
        remove.onClick = [this] { owner.removeAttribute (index); };
        addAndMakeVisible (remove);
    }

    void update (int newIndex, const AttributeDef& a, bool readOnly)
    {
        updating = true;
        index = newIndex;
        auto& attrs = allAttributes();
        for (int i = 0; i < (int) attrs.size(); ++i)
            if (a.attribute == attrs[(size_t) i].name)
                attribute.setSelectedId (i + 1, juce::dontSendNotification);
        if (! coarse.hasKeyboardFocus (true)) coarse.setText (juce::String (a.coarse), false);
        if (! cell.hasKeyboardFocus (true))   cell.setText (a.cell > 0 ? juce::String (a.cell) : juce::String(), false);
        if (! fine.hasKeyboardFocus (true))   fine.setText (a.fine > 0 ? juce::String (a.fine) : juce::String(), false);
        if (! highlight.hasKeyboardFocus (true))
            highlight.setText (a.highlight >= 0 ? juce::String (a.highlight) : juce::String(), false);
        const int autoHl = defaultHighlight (a.attribute);
        highlight.setTextToShowWhenEmpty (autoHl >= 0 ? juce::String (autoHl) : juce::String ("-"), theme::textDim);
        sixteen.setToggleState (a.fine > 0, juce::dontSendNotification);
        fine.setEnabled (a.fine > 0 && ! readOnly);
        defaultValue.setValue (a.defaultValue, juce::dontSendNotification);
        stripe = theme::familyColour (familyOf (a.attribute));
        locked = readOnly;
        for (auto* c : std::initializer_list<juce::Component*> { &attribute, &coarse, &sixteen, &defaultValue, &remove, &cell, &highlight })
            c->setEnabled (! readOnly);
        updating = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0, 2), 6.0f);
        g.setColour (stripe);
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 4, 4, (float) getHeight() - 8), 2.0f);

        // drag handle
        auto h = rowColumns (getLocalBounds().reduced (6, 4))[cHandle].toFloat();
        g.setColour (locked ? theme::line : theme::textDim);
        for (int i = -1; i <= 1; ++i)
            g.fillRoundedRectangle (h.getCentreX() - 7.0f, h.getCentreY() + (float) i * 5.0f - 1.0f, 14.0f, 2.0f, 1.0f);
    }

    void resized() override
    {
        auto c = rowColumns (getLocalBounds().reduced (6, 4));
        attribute.setBounds (c[cAttr]);
        cell.setBounds (c[cCell]);
        coarse.setBounds (c[cCoarse]);
        sixteen.setBounds (c[c16]);
        fine.setBounds (c[cFine]);
        defaultValue.setBounds (c[cDefault]);
        highlight.setBounds (c[cHighlight]);
        remove.setBounds (c[cRemove]);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = ! locked && e.x < 40;
        if (dragging) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            owner.dragRow (index, e.getEventRelativeTo (getParentComponent()).y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) owner.finishDrag();
        dragging = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (! locked && e.x < 40 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    void push (bool manualChannel)
    {
        if (updating) return;
        AttributeDef a;
        auto& attrs = allAttributes();
        const int i = attribute.getSelectedId() - 1;
        a.attribute = (i >= 0 && i < (int) attrs.size()) ? attrs[(size_t) i].name : "Dimmer";
        a.coarse = juce::jlimit (1, 512, coarse.getText().getIntValue());
        a.fine = 0;
        if (sixteen.getToggleState())
        {
            a.fine = fine.getText().getIntValue();
            if (a.fine <= 0) a.fine = a.coarse + 1;
        }
        a.defaultValue = (int) defaultValue.getValue();
        a.cell = juce::jmax (0, cell.getText().getIntValue());
        a.highlight = highlight.getText().trim().isEmpty() ? -1 : juce::jlimit (0, 255, highlight.getText().getIntValue());
        owner.setAttribute (index, a, manualChannel);
    }

    FixtureBuilderView& owner;
    int index = 0;
    bool updating = false, locked = false, dragging = false;
    juce::Colour stripe;
    juce::ComboBox attribute;
    juce::TextEditor coarse, fine, cell, highlight;
    juce::TextButton sixteen, remove;
    juce::Slider defaultValue;
};

//==============================================================================
FixtureBuilderView::FixtureBuilderView (DmxEngine& e) : engine (e)
{
    channelMap = std::make_unique<ChannelMap>();
    rowsHolder = std::make_unique<RowsHolder> (*this);

    typeList.setRowHeight (44);
    typeList.setOutlineThickness (0);
    typeList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (typeList);

    for (auto* b : { &newButton, &dupButton, &delButton, &gdtfFileButton, &gdtfShareButton, &libImportButton, &libExportButton })
        addAndMakeVisible (b);
    libImportButton.setTooltip ("Import fixture types from a Phase library (.phaselib), another show (.phase), .gdtf files or a whole folder");
    libImportButton.onClick = [this] { importLibrary(); };
    libExportButton.setTooltip ("Save all custom fixture types to a .phaselib file (to share or back up)");
    libExportButton.onClick = [this] { exportLibrary(); };
    gdtfFileButton.setTooltip ("Add fixture types from a .gdtf file (one per DMX mode)");
    gdtfFileButton.onClick = [this] { importGdtfFile(); };
    gdtfShareButton.setTooltip ("Search and download fixtures from GDTF Share (free account needed)");
    gdtfShareButton.onClick = [this] { openGdtfShare(); };
    gdtfStatus.setFont (theme::font (12.0f));
    gdtfStatus.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (gdtfStatus);
    newButton.setTooltip ("New fixture type: pick what kind of fixture it is for a starting point");
    newButton.onClick = [this] { showNewMenu(); };
    dupButton.onClick = [this]
    {
        if (selectedId.isEmpty()) return;
        { const juce::ScopedLock sl (engine.dataLock); selectedId = engine.library.duplicate (selectedId); }
        engine.showChanged();
    };
    delButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.8f));
    delButton.onClick = [this]
    {
        if (readOnly || selectedId.isEmpty()) return;
        bool inUse = false;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& f : engine.patch.fixtures()) inUse |= (f.typeId == selectedId);
            if (! inUse) { engine.library.remove (selectedId); selectedId.clear(); }
        }
        if (inUse) errorLabel.setText ("This type is patched. Remove those fixtures first.", juce::dontSendNotification);
        engine.showChanged();
    };

    ui::styleHeader (header, "FIXTURE TYPE");
    ui::styleField (mfrLabel, "Manufacturer");
    ui::styleField (nameLabel, "Name");
    ui::styleField (spareLabel, "Spare ch");
    ui::styleEditor (mfrEdit, false);
    ui::styleEditor (nameEdit, false);
    ui::styleEditor (spareEdit, true);
    spareEdit.setTooltip ("Unused channels after the last assigned one");
    mfrEdit.onTextChange  = [this] { auto t = mfrEdit.getText();  modifyType ([t] (FixtureType& ft) { ft.manufacturer = t; }, false); };
    nameEdit.onTextChange = [this] { auto t = nameEdit.getText(); modifyType ([t] (FixtureType& ft) { ft.name = t; }, false); };
    spareEdit.onReturnKey = [this] { int n = spareEdit.getText().getIntValue(); modifyType ([n] (FixtureType& ft) { ft.extraChannels = juce::jlimit (0, 64, n); }, false); };
    spareEdit.onFocusLost = spareEdit.onReturnKey;

    infoLabel.setFont (theme::font (13.5f));
    infoLabel.setColour (juce::Label::textColourId, theme::textDim);
    errorLabel.setFont (theme::font (13.5f, true));
    errorLabel.setColour (juce::Label::textColourId, theme::danger);

    keepOrderToggle.setToggleState (true, juce::dontSendNotification);
    keepOrderToggle.setTooltip ("Renumber channels automatically to follow the row order");
    keepOrderToggle.onClick = [this] { if (keepOrderToggle.getToggleState()) modifyType ([] (FixtureType&) {}, true); };

    addButton.onClick = [this]
    {
        modifyType ([] (FixtureType& ft)
        {
            // first attribute not used yet, defaulting to Dimmer
            juce::String pick = "Dimmer";
            for (auto& a : allAttributes())
            {
                if (juce::String (a.name) == kNoAttribute) continue;
                bool used = false;
                for (auto& x : ft.attributes) used |= (x.attribute == a.name);
                if (! used) { pick = a.name; break; }
            }
            ft.attributes.push_back ({ pick, ft.highestChannel() + 1, 0, suggestedDefault (pick) });
        });
    };
    ui::styleField (cellsLabel, "Cells");
    for (auto* ed : { &cellsXEdit, &cellsYEdit })
    {
        ui::styleEditor (*ed, true);
        ed->setInputRestrictions (3, "0123456789");
        ed->setJustification (juce::Justification::centred);
        ed->onReturnKey = [this]
        {
            const int x = juce::jlimit (1, 256, cellsXEdit.getText().getIntValue()), y = juce::jlimit (1, 256, cellsYEdit.getText().getIntValue());
            modifyType ([x, y] (FixtureType& ft) { ft.cellsX = x; ft.cellsY = y; }, false);
        };
        ed->onFocusLost = ed->onReturnKey;
    }
    cellsXEdit.setTooltip ("Cells across (pixel fixtures)");
    cellsYEdit.setTooltip ("Cells down");
    cellsBy.setText (juce::CharPointer_UTF8 ("\xc3\x97"), juce::dontSendNotification);
    cellsBy.setJustificationType (juce::Justification::centred);
    repeatButton.setTooltip ("Give cell 1 its attributes (set Cell = 1), then copy them to every cell and renumber");
    repeatButton.onClick = [this] { modifyType ([] (FixtureType& ft) { ft.repeatCellOne(); }, false); };
    layoutButton.setTooltip ("Where each pixel sits on the fixture: rings, zig-zag wiring, any shape");
    layoutButton.onClick = [this] { openCellLayout(); };
    pixelSetButton.setTooltip ("Add another set of pixels after the existing ones, e.g. separate white cells next to RGB cells");
    pixelSetButton.onClick = [this] { addPixelSetDialog(); };
    autoButton.setTooltip ("Number channels 1, 2, 3... in row order (fine right after coarse)");
    autoButton.onClick = [this] { modifyType ([] (FixtureType& ft) { ft.autoNumber(); }, false); };

    rowsViewport.setViewedComponent (rowsHolder.get(), false);
    rowsViewport.setScrollBarsShown (true, false);

    for (auto* c : std::initializer_list<juce::Component*> {
             &header, &mfrLabel, &nameLabel, &mfrEdit, &nameEdit, &infoLabel, &errorLabel, &spareLabel, &spareEdit,
             &addButton, &autoButton, &keepOrderToggle, channelMap.get(), &rowsViewport,
             &cellsLabel, &cellsXEdit, &cellsBy, &cellsYEdit, &repeatButton, &layoutButton, &pixelSetButton })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    refreshTypes();
    if (! typesCopy.empty())
        typeList.selectRow (0);
}

FixtureBuilderView::~FixtureBuilderView()
{
    engine.removeChangeListener (this);
    attrRows.clear();
}

//==============================================================================
void FixtureBuilderView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, leftPanel);
    ui::paintPanel (g, rightPanel);

    auto c = rowColumns (columnHeaderArea.withWidth (rowsViewport.getMaximumVisibleWidth()).reduced (6, 0));
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f, true));
    g.drawText ("ATTRIBUTE", c[cAttr].withTrimmedLeft (6), juce::Justification::centredLeft);
    g.drawText ("CELL", c[cCell], juce::Justification::centred);
    g.drawText ("CHANNEL", c[cCoarse], juce::Justification::centred);
    g.drawText ("16-BIT", c[c16], juce::Justification::centred);
    g.drawText ("FINE CH", c[cFine], juce::Justification::centred);
    g.drawText ("DEFAULT", c[cDefault], juce::Justification::centred);
    g.drawText ("HIGHLIGHT", c[cHighlight], juce::Justification::centred);
}

void FixtureBuilderView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    leftPanel = r.removeFromLeft (290);
    r.removeFromLeft (theme::gap);
    rightPanel = r;

    auto l = leftPanel.reduced (10);
    {
        auto libRow = l.removeFromBottom (theme::buttonH - 6);
        libImportButton.setBounds (libRow.removeFromLeft (libRow.getWidth() / 2 - 3));
        libRow.removeFromLeft (6);
        libExportButton.setBounds (libRow);
        l.removeFromBottom (6);
    }
    {
        auto gdtfRow = l.removeFromBottom (theme::buttonH - 6);
        gdtfFileButton.setBounds (gdtfRow.removeFromLeft (gdtfRow.getWidth() / 2 - 3));
        gdtfRow.removeFromLeft (6);
        gdtfShareButton.setBounds (gdtfRow);
        l.removeFromBottom (2);
        gdtfStatus.setBounds (l.removeFromBottom (34));
        l.removeFromBottom (4);
    }
    auto buttons = l.removeFromBottom (theme::buttonH);
    const int bw = (buttons.getWidth() - 12) / 3;
    newButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (6);
    dupButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (6);
    delButton.setBounds (buttons);
    l.removeFromBottom (8);
    typeList.setBounds (l);

    auto p = rightPanel.reduced (14);
    {
        auto line = p.removeFromTop (theme::rowHeight);
        header.setBounds (line.removeFromLeft (120));
        mfrLabel.setBounds (line.removeFromLeft (100));
        mfrEdit.setBounds (line.removeFromLeft (juce::jmin (220, line.getWidth() / 3)));
        line.removeFromLeft (16);
        nameLabel.setBounds (line.removeFromLeft (52));
        nameEdit.setBounds (line);
    }
    p.removeFromTop (10);
    channelMap->setBounds (p.removeFromTop (52));
    p.removeFromTop (8);
    {
        auto line = p.removeFromTop (theme::buttonH - 4);
        addButton.setBounds (line.removeFromRight (150));
        line.removeFromRight (6);
        autoButton.setBounds (line.removeFromRight (120));
        line.removeFromRight (12);
        spareEdit.setBounds (line.removeFromRight (60));
        spareLabel.setBounds (line.removeFromRight (70));
        line.removeFromRight (12);
        keepOrderToggle.setBounds (line.removeFromLeft (230));
        infoLabel.setBounds (line);
    }
    p.removeFromTop (6);
    {
        auto line = p.removeFromTop (30);
        cellsLabel.setBounds (line.removeFromLeft (44));
        cellsXEdit.setBounds (line.removeFromLeft (48));
        cellsBy.setBounds (line.removeFromLeft (20));
        cellsYEdit.setBounds (line.removeFromLeft (48));
        line.removeFromLeft (10);
        repeatButton.setBounds (line.removeFromLeft (190));
        line.removeFromLeft (6);
        layoutButton.setBounds (line.removeFromLeft (120));
        line.removeFromLeft (6);
        pixelSetButton.setBounds (line.removeFromLeft (116));
        line.removeFromLeft (12);
        errorLabel.setBounds (line);
    }
    p.removeFromTop (4);
    columnHeaderArea = p.removeFromTop (20);
    p.removeFromTop (2);
    rowsViewport.setBounds (p);
    layoutRows();
}

//==============================================================================
int FixtureBuilderView::getNumRows() { return (int) typesCopy.size(); }

void FixtureBuilderView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) typesCopy.size()) return;
    auto& t = typesCopy[(size_t) row];
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2);
    g.setColour (selected ? theme::accent.withAlpha (0.25f) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText (t.name, r.reduced (10, 0).withTrimmedBottom (h / 2 - 4), juce::Justification::bottomLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (t.manufacturer + "  -  " + juce::String (t.footprint()) + " ch" + (t.builtIn ? "  -  built-in" : ""),
                r.reduced (10, 0).withTrimmedTop (h / 2 - 2), juce::Justification::topLeft, true);
}

void FixtureBuilderView::selectedRowsChanged (int lastRow)
{
    if (ignoreCallbacks) return;
    if (lastRow >= 0 && lastRow < (int) typesCopy.size())
        selectedId = typesCopy[(size_t) lastRow].id;
    loadSelectedType();
}

void FixtureBuilderView::changeListenerCallback (juce::ChangeBroadcaster*) { refreshTypes(); }

void FixtureBuilderView::refreshTypes()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        typesCopy = engine.library.types();
    }
    const juce::ScopedValueSetter<bool> svs (ignoreCallbacks, true);
    typeList.updateContent();
    for (int i = 0; i < (int) typesCopy.size(); ++i)
        if (typesCopy[(size_t) i].id == selectedId)
            typeList.selectRow (i, false, true);
    typeList.repaint();
    loadSelectedType();
}

void FixtureBuilderView::loadSelectedType()
{
    const FixtureType* found = nullptr;
    for (auto& t : typesCopy)
        if (t.id == selectedId) found = &t;

    current = found != nullptr ? *found : FixtureType();
    readOnly = found == nullptr || current.builtIn;

    if (mfrEdit.getText() != current.manufacturer) mfrEdit.setText (current.manufacturer, false);
    if (nameEdit.getText() != current.name)        nameEdit.setText (current.name, false);
    if (! spareEdit.hasKeyboardFocus (true))       spareEdit.setText (juce::String (current.extraChannels), false);
    if (! cellsXEdit.hasKeyboardFocus (true))      cellsXEdit.setText (juce::String (current.cellsX), false);
    if (! cellsYEdit.hasKeyboardFocus (true))      cellsYEdit.setText (juce::String (current.cellsY), false);
    for (auto* ed : { &mfrEdit, &nameEdit, &spareEdit, &cellsXEdit, &cellsYEdit }) ed->setReadOnly (readOnly);
    for (auto* b : std::initializer_list<juce::Component*> { &addButton, &autoButton, &keepOrderToggle, &repeatButton, &pixelSetButton })
        b->setEnabled (! readOnly);
    delButton.setEnabled (! readOnly);
    dupButton.setEnabled (found != nullptr);

    bool patched = false;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& f : engine.patch.fixtures()) patched |= (f.typeId == current.id);
    }

    if (found == nullptr)
        infoLabel.setText ("Select a fixture type, or create a new one.", juce::dontSendNotification);
    else if (readOnly)
        infoLabel.setText ("Built-in (read-only). Duplicate to edit.", juce::dontSendNotification);
    else
        infoLabel.setText (juce::String (current.footprint()) + " channels"
                           + (current.hasCells() ? ", " + juce::String (current.cellCount()) + " cells" : juce::String())
                           + (current.usesVirtualDimmer() ? ", virtual dimmer" : "")
                           + (patched ? "  -  patched: check addresses after changes" : ""),
                           juce::dontSendNotification);

    errorLabel.setText (found != nullptr ? current.validate() : juce::String(), juce::dontSendNotification);
    channelMap->setType (current);
    rebuildRows();
}

void FixtureBuilderView::rebuildRows()
{
    const int n = (int) current.attributes.size();
    while (attrRows.size() > n) attrRows.removeLast();
    while (attrRows.size() < n) rowsHolder->addAndMakeVisible (attrRows.add (new AttrRow (*this)));
    for (int i = 0; i < n; ++i)
        attrRows[i]->update (i, current.attributes[(size_t) i], readOnly);
    layoutRows();
}

void FixtureBuilderView::layoutRows()
{
    const int h = RowsHolder::rowHeight;
    const int w = rowsViewport.getMaximumVisibleWidth();
    rowsHolder->setSize (w, juce::jmax (1, attrRows.size() * h + 8));
    for (int i = 0; i < attrRows.size(); ++i)
        attrRows[i]->setBounds (0, i * h, w, h);
}

//==============================================================================
void FixtureBuilderView::modifyType (std::function<void (FixtureType&)> fn, bool renumber)
{
    if (readOnly || selectedId.isEmpty()) return;
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (auto* t = engine.library.findMutable (selectedId))
        {
            fn (*t);
            if (renumber && keepOrderToggle.getToggleState())
                t->autoNumber();
            current = *t;
        }
    }
    engine.showChanged();   // async: rows refresh after this callback returns
}

void FixtureBuilderView::setAttribute (int index, const AttributeDef& a, bool manualChannel)
{
    if (manualChannel && keepOrderToggle.getToggleState())
    {
        // typing a channel means the user wants manual numbering
        if (index >= 0 && index < (int) current.attributes.size()
            && (current.attributes[(size_t) index].coarse != a.coarse || current.attributes[(size_t) index].fine != a.fine))
            keepOrderToggle.setToggleState (false, juce::dontSendNotification);
    }
    modifyType ([index, a] (FixtureType& t)
    {
        if (index < 0 || index >= (int) t.attributes.size()) return;
        auto updated = a;
        // a new attribute brings its usual default (Pan / Tilt centred, shutter open...)
        // unless the default had been changed by hand
        const auto& old = t.attributes[(size_t) index];
        if (old.attribute != a.attribute && a.defaultValue == old.defaultValue
            && old.defaultValue == suggestedDefault (old.attribute))
            updated.defaultValue = suggestedDefault (a.attribute);
        t.attributes[(size_t) index] = updated;
    });
}

void FixtureBuilderView::addPixelSetDialog()
{
    if (readOnly) { errorLabel.setText ("Built-in type: duplicate it to change it", juce::dontSendNotification); return; }
    auto* aw = new juce::AlertWindow ("Add a pixel set",
                                      "Another set of pixels after the existing " + juce::String (current.hasCells() ? current.cellCount() : 0)
                                      + " cells, with their own channels (after the last used channel).\n"
                                      "For fixtures with RGB pixels and separate white pixels, rings of different LEDs, etc.",
                                      juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("count", juce::String (current.hasCells() ? current.cellCount() : 8), "How many pixels");
    const juce::StringArray kinds { "White", "RGB", "RGBW", "Amber", "UV", "Warm white + Cold white (CTO)", "Dimmer (single colour)" };
    aw->addComboBox ("kind", kinds, "Each pixel has");
    aw->addComboBox ("where", { "Beside the existing pixels (same spots)", "In a row under them" }, "Layout");
    aw->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<FixtureBuilderView> (this), aw] (int r)
    {
        if (safe == nullptr || r != 1) return;
        const int count = juce::jlimit (1, 512, aw->getTextEditorContents ("count").getIntValue());
        const int kind = aw->getComboBoxComponent ("kind")->getSelectedItemIndex();
        const bool same = aw->getComboBoxComponent ("where")->getSelectedItemIndex() == 0;
        juce::StringArray attrs;
        switch (kind)
        {
            case 0: attrs = { "ColorW" }; break;
            case 1: attrs = { "ColorR", "ColorG", "ColorB" }; break;
            case 2: attrs = { "ColorR", "ColorG", "ColorB", "ColorW" }; break;
            case 3: attrs = { "ColorA" }; break;
            case 4: attrs = { "ColorUV" }; break;
            case 5: attrs = { "ColorW", "CTO" }; break;
            default: attrs = { "Dimmer" }; break;
        }
        safe->modifyType ([count, attrs, same] (FixtureType& t) { t.addPixelSet (count, attrs, same); }, false);
    }), true);
}

void FixtureBuilderView::openCellLayout()
{
    if (! current.hasCells()) { errorLabel.setText ("Set Cells first (more than one cell)", juce::dontSendNotification); return; }
    if (readOnly) { errorLabel.setText ("Built-in type: duplicate it to change its layout", juce::dontSendNotification); return; }
    if (layoutWindow != nullptr) delete layoutWindow.getComponent();
    auto* editor = new CellLayoutEditor (current);
    editor->onChange = [safe = juce::Component::SafePointer<FixtureBuilderView> (this)] (const std::vector<std::pair<float, float>>& pos)
    {
        if (safe != nullptr)
            safe->modifyType ([pos] (FixtureType& t)
            {
                if ((int) pos.size() != t.cellCount() && pos.size() > 1 && t.cellsAreUniform())
                {
                    // the rings asked for a different pixel count: resize, repeating cell 1's channels
                    t.cellsX = (int) pos.size();
                    t.cellsY = 1;
                    t.repeatCellOne();
                }
                if ((int) pos.size() == t.cellCount()) t.setCellLayout (pos);
            }, false);
    };
    editor->setSize (760, 620);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor);
    o.dialogTitle = "Pixel layout - " + current.name;
    o.dialogBackgroundColour = theme::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    layoutWindow = o.launchAsync();
}

void FixtureBuilderView::showNewMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("What kind of fixture?");
    m.addItem (1, "Empty (start from scratch)");
    m.addSeparator();
    const auto& list = templates::all();
    for (int i = 0; i < (int) list.size(); ++i)
        m.addItem (100 + i, list[(size_t) i].name + "   -   " + list[(size_t) i].description);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newButton),
                     [safe = juce::Component::SafePointer<FixtureBuilderView> (this)] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1)
        {
            { const juce::ScopedLock sl (safe->engine.dataLock); safe->selectedId = safe->engine.library.createEmpty(); }
            safe->engine.showChanged();
            return;
        }
        const auto info = templates::all()[(size_t) (r - 100)];
        if (! info.askCells) { safe->createFromTemplate (info.id, 1, 1); return; }
        ui::askForText (safe, info.name, info.grid ? "Cells across x down (for example 8x8)" : "How many pixels / cells?",
                        info.grid ? "8x8" : "8", [safe, info] (const juce::String& text)
        {
            if (safe == nullptr) return;
            const auto parts = juce::StringArray::fromTokens (text.toLowerCase().replace ("*", "x"), "x ", "");
            const int x = juce::jlimit (1, 256, parts[0].getIntValue());
            const int y = info.grid ? juce::jlimit (1, 256, parts.size() > 1 ? parts[1].getIntValue() : x) : 1;
            safe->createFromTemplate (info.id, x, y);
        });
    });
}

void FixtureBuilderView::createFromTemplate (const juce::String& id, int cellsX, int cellsY)
{
    auto t = templates::make (id, cellsX, cellsY);
    if (t.footprint() > 512) { errorLabel.setText ("That is more than 512 channels", juce::dontSendNotification); return; }
    { const juce::ScopedLock sl (engine.dataLock); engine.library.addOrReplace (t); selectedId = t.id; }
    engine.showChanged();
}

void FixtureBuilderView::importLibrary()
{
    chooser = std::make_unique<juce::FileChooser> ("Import fixture library (.phaselib, .phase, .gdtf or a folder)", juce::File(),
                                                   "*.phaselib;*.phase;*.gdtf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<FixtureBuilderView> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResults().isEmpty()) return;
        int added = 0;
        juce::StringArray errors;
        for (auto& f : fc.getResults())
            if (auto e = showfile::importLibrary (safe->engine, f, added); e.isNotEmpty()) errors.add (e);
        safe->engine.showChanged();
        safe->gdtfStatus.setColour (juce::Label::textColourId, added > 0 ? theme::ok : theme::danger);
        safe->gdtfStatus.setText ("Imported " + juce::String (added) + " fixture type(s)"
                                  + (errors.isEmpty() ? juce::String() : "  -  " + errors.joinIntoString ("; ")),
                                  juce::dontSendNotification);
    });
}

void FixtureBuilderView::exportLibrary()
{
    std::vector<FixtureType> types;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& t : engine.library.types()) if (! t.builtIn) types.push_back (t);
    }
    if (types.empty()) { gdtfStatus.setText ("No custom fixture types to export", juce::dontSendNotification); return; }
    chooser = std::make_unique<juce::FileChooser> ("Export fixture library",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Phase fixtures.phaselib"),
                                                   "*.phaselib");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<FixtureBuilderView> (this), types] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        const auto err = showfile::exportLibrary (types, fc.getResult().withFileExtension ("phaselib"));
        safe->gdtfStatus.setText (err.isNotEmpty() ? err : "Exported " + juce::String ((int) types.size()) + " fixture type(s)",
                                  juce::dontSendNotification);
    });
}

void FixtureBuilderView::removeAttribute (int index)
{
    modifyType ([index] (FixtureType& t)
    {
        if (index >= 0 && index < (int) t.attributes.size())
            t.attributes.erase (t.attributes.begin() + index);
    });
}

void FixtureBuilderView::dragRow (int index, int holderY)
{
    dragIndex = index;
    dropIndex = juce::jlimit (0, attrRows.size(), (holderY + RowsHolder::rowHeight / 2) / RowsHolder::rowHeight);
    rowsHolder->repaint();
    if (! autoScroll.isTimerRunning()) autoScroll.startTimerHz (40);
}

void FixtureBuilderView::autoScrollTick()
{
    if (dragIndex < 0 || ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        autoScroll.stopTimer();
        return;
    }
    // the closer to the edge, the faster it scrolls
    const auto m = rowsViewport.getMouseXYRelative();
    const int h = rowsViewport.getHeight(), edge = 40;
    int speed = 0;
    if (m.y < edge)          speed = -juce::jlimit (2, 24, (edge - m.y) / 2);
    else if (m.y > h - edge) speed =  juce::jlimit (2, 24, (m.y - (h - edge)) / 2);
    if (speed == 0) return;
    const int maxY = juce::jmax (0, rowsHolder->getHeight() - rowsViewport.getViewHeight());
    rowsViewport.setViewPosition (0, juce::jlimit (0, maxY, rowsViewport.getViewPositionY() + speed));
    dragRow (dragIndex, rowsHolder->getMouseXYRelative().y);
}

void FixtureBuilderView::importGdtfFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Import GDTF fixture", juce::File(), "*.gdtf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<FixtureBuilderView> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        juce::MemoryBlock data;
        if (! fc.getResult().loadFileAsData (data)) { safe->gdtfStatus.setText ("Couldn't read the file", juce::dontSendNotification); return; }
        importGdtfData (safe->engine, data, &safe->gdtfFileButton, [safe] (const juce::String& msg, bool error)
        {
            if (safe == nullptr) return;
            safe->gdtfStatus.setColour (juce::Label::textColourId, error ? theme::danger : theme::ok);
            safe->gdtfStatus.setText (msg, juce::dontSendNotification);
        });
    });
}

void FixtureBuilderView::openGdtfShare()
{
    if (shareWindow != nullptr) { shareWindow->toFront (true); return; }
    juce::DialogWindow::LaunchOptions o;
    auto* content = new GdtfShareView (engine);
    content->setSize (1100, 680);
    o.content.setOwned (content);
    o.dialogTitle = "GDTF Share";
    o.dialogBackgroundColour = theme::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    shareWindow = o.launchAsync();
}

void FixtureBuilderView::finishDrag()
{
    const int from = dragIndex, to = dropIndex;
    dragIndex = dropIndex = -1;
    rowsHolder->repaint();
    if (from < 0 || to < 0 || to == from || to == from + 1) return;

    modifyType ([from, to] (FixtureType& t)
    {
        auto item = t.attributes[(size_t) from];
        t.attributes.erase (t.attributes.begin() + from);
        const int insertAt = to > from ? to - 1 : to;
        t.attributes.insert (t.attributes.begin() + insertAt, item);
    });
}

} // namespace pl
