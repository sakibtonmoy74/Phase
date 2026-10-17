#include "PatchView.h"
#include "FixtureBuilderView.h"

namespace pl
{

enum Columns { colFid = 1, colName, colType, colPatch, colChannels, colInvert };

PatchView::PatchView (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    auto& header = table.getHeader();
    header.addColumn ("FID",      colFid,      90, 50, 160);
    header.addColumn ("Name",     colName,     220, 100, 500);
    header.addColumn ("Type",     colType,     260, 100, 500);
    header.addColumn ("Patch",    colPatch,    110, 80, 200);
    header.addColumn ("Channels", colChannels, 90, 60, 150);
    header.addColumn ("Pan / Tilt", colInvert, 110, 70, 200);
    header.setStretchToFitActive (true);
    table.setHeaderHeight (32);
    table.setRowHeight (theme::rowHeight);
    table.setMultipleSelectionEnabled (true);
    table.setColour (juce::ListBox::outlineColourId, theme::line);
    table.setOutlineThickness (1);
    addAndMakeVisible (table);

    builderButton.setColour (juce::TextButton::buttonColourId, theme::familyColour (Family::Focus).withAlpha (0.5f));
    builderButton.onClick = [this] { openFixtureBuilder(); };
    newFolderButton.setTooltip ("Put the selected fixtures in a new folder");
    newFolderButton.onClick = [this]
    {
        if (selectedFids().empty()) { showMessage ("Select fixtures first", true); return; }
        ui::askForText (this, "New folder", "Name for the folder", {}, [safe = juce::Component::SafePointer<PatchView> (this)] (const juce::String& name)
        {
            if (safe != nullptr && name.isNotEmpty()) safe->moveSelectedToFolder (name);
        });
    };
    moveButton.onClick = [this] { showMoveMenu(); };
    renameFolderButton.onClick = [this] { renameSelectedFolder(); };
    groupsButton.setTooltip ("Make (or update) a group for every folder, named like the folder, fixtures in FID order");
    groupsButton.onClick = [this]
    {
        juce::StringArray made;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& folder : engine.patch.folders())
            {
                std::vector<int> fids;
                for (auto& f : engine.patch.fixtures())
                    if (f.folder == folder) fids.push_back (f.fid);
                if (fids.empty()) continue;
                const int n = engine.presets.setGroup (folder, fids);
                made.add (juce::String (n) + " " + folder);
            }
        }
        engine.showChanged();
        if (made.isEmpty()) showMessage ("No folders yet: put fixtures in folders first", true);
        else                showMessage ("Groups: " + made.joinIntoString (", "), false);
    };
    invertPanButton.setTooltip ("Invert pan on the selected fixtures (press again to set back)");
    invertTiltButton.setTooltip ("Invert tilt on the selected fixtures (press again to set back)");
    highlightButton.setClickingTogglesState (true);
    highlightButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Dimmer));
    highlightButton.setTooltip ("Highlight the fixtures selected in this list (full, open white) to find them in the rig");
    highlightButton.onClick = [this] { updatePatchHighlight(); };
    invertPanButton.onClick  = [this] { toggleInvert (true); };
    invertTiltButton.onClick = [this] { toggleInvert (false); };
    repatchButton.setTooltip ("Give the selected fixtures new DMX addresses, one after another from a start address (FID order)");
    repatchButton.onClick = [this] { repatchSelected(); };
    for (auto* b : { &builderButton, &newFolderButton, &moveButton, &renameFolderButton, &groupsButton, &invertPanButton, &invertTiltButton,
                     &highlightButton, &repatchButton })
        addAndMakeVisible (b);

    ui::styleHeader (addHeader, "ADD FIXTURES");
    ui::styleField (typeLabel, "Type");
    ui::styleField (qtyLabel, "Quantity");
    ui::styleField (fidLabel, "First FID");
    ui::styleField (uniLabel, "Universe");
    ui::styleField (addrLabel, "Address");
    ui::styleField (nameLabel, "Name");
    ui::styleField (folderLabel, "Folder");
    for (auto* ed : { &qtyEdit, &fidEdit, &uniEdit, &addrEdit }) ui::styleEditor (*ed, true);
    ui::styleEditor (nameEdit, false);
    ui::styleEditor (folderEdit, false);
    qtyEdit.setText ("1");
    uniEdit.setText ("1");
    nameEdit.setTextToShowWhenEmpty ("Uses type name", theme::textDim);
    folderEdit.setTextToShowWhenEmpty ("None", theme::textDim);
    folderPick.setTooltip ("Pick an existing folder");
    folderPick.onClick = [this]
    {
        juce::PopupMenu m;
        juce::StringArray folders;
        { const juce::ScopedLock sl (engine.dataLock); folders = engine.patch.folders(); }
        m.addItem (1, "None");
        for (int i = 0; i < folders.size(); ++i) m.addItem (i + 2, folders[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderPick),
                         [safe = juce::Component::SafePointer<PatchView> (this), folders] (int r)
        {
            if (safe == nullptr || r == 0) return;
            safe->folderEdit.setText (r == 1 ? juce::String() : folders[r - 2], false);
        });
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &addHeader, &typeLabel, &qtyLabel, &fidLabel, &uniLabel, &addrLabel, &nameLabel, &folderLabel,
             &typeCombo, &qtyEdit, &fidEdit, &uniEdit, &addrEdit, &nameEdit, &folderEdit, &folderPick,
             &nextAddrButton, &addButton })
        sideHolder.addAndMakeVisible (c);

    addButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    addButton.setColour (juce::TextButton::textColourOffId, theme::bg);
    addButton.onClick = [this] { addFixtures(); };
    nextAddrButton.onClick = [this] { suggestNextAddress(); };
    uniEdit.onTextChange = [this] { suggestNextAddress(); };
    typeCombo.onChange = [this] { suggestNextAddress(); };
    ui::styleField (searchLabel, "Search");
    ui::styleEditor (typeSearch, false);
    typeSearch.setTextToShowWhenEmpty ("manufacturer, name, channels...", theme::textDim);
    typeSearch.onTextChange = [this] { refreshTypeCombo(); };
    typeSearch.onReturnKey = [this] { typeCombo.showPopup(); };
    sideHolder.addAndMakeVisible (searchLabel);
    sideHolder.addAndMakeVisible (typeSearch);

    for (auto* t : { &invertPanToggle, &invertTiltToggle })
    {
        t->setTooltip ("For fixtures hung the other way round: the output is inverted, programming stays the same");
        sideHolder.addAndMakeVisible (t);
    }
    invertPanToggle.onClick  = [this] { setInvert (true, invertPanToggle.getToggleState()); };
    invertTiltToggle.onClick = [this] { setInvert (false, invertTiltToggle.getToggleState()); };

    ui::styleHeader (editHeader, "SELECTED FIXTURE");
    ui::styleField (eFidLabel, "FID");
    ui::styleField (eNameLabel, "Name");
    ui::styleField (eUniLabel, "Universe");
    ui::styleField (eAddrLabel, "Address");
    ui::styleEditor (eFid, true);
    ui::styleEditor (eName, false);
    ui::styleEditor (eUni, true);
    ui::styleEditor (eAddr, true);
    for (auto* c : std::initializer_list<juce::Component*> {
             &editHeader, &eFidLabel, &eNameLabel, &eUniLabel, &eAddrLabel,
             &eFid, &eName, &eUni, &eAddr, &applyButton, &deleteButton })
        sideHolder.addAndMakeVisible (c);

    applyButton.onClick = [this] { applyEdit(); };
    deleteButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.8f));
    deleteButton.onClick = [this] { deleteSelected(); };

    message.setFont (theme::font (14.0f));
    message.setJustificationType (juce::Justification::topLeft);
    sideHolder.addAndMakeVisible (message);
    sideViewport.setViewedComponent (&sideHolder, false);
    sideViewport.setScrollBarsShown (true, false);
    sideViewport.setScrollBarThickness (6);
    addAndMakeVisible (sideViewport);

    engine.addChangeListener (this);
    refreshFromEngine();
}

PatchView::~PatchView()
{
    engine.removeChangeListener (this);
    if (builderWindow != nullptr) delete builderWindow.getComponent();
}

//==============================================================================
void PatchView::openFixtureBuilder()
{
    if (builderWindow != nullptr)
    {
        builderWindow->toFront (true);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    auto* content = new FixtureBuilderView (engine);
    content->setSize (1180, 720);
    o.content.setOwned (content);
    o.dialogTitle = "Fixture Builder";
    o.dialogBackgroundColour = theme::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    builderWindow = o.launchAsync();
    if (builderWindow != nullptr)
        builderWindow->setResizeLimits (900, 500, 4000, 3000);
}

//==============================================================================
void PatchView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, sidePanel);
}

void PatchView::repatchSelected()
{
    auto fids = selectedFids();
    if (fids.empty()) { showMessage ("Select the fixtures to re-patch first (Shift / Cmd-click for several)", true); return; }
    std::sort (fids.begin(), fids.end());
    int uni = 1, addr = 1;
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (auto* f = engine.patch.find (fids.front())) { uni = f->universe; addr = f->address; }
    }
    auto* aw = new juce::AlertWindow ("Re-patch " + juce::String ((int) fids.size()) + " fixtures",
                                      "New addresses one after another, in FID order (" + juce::String (fids.front()) + " first). "
                                      "A fixture that doesn't fit goes on to the next universe.",
                                      juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("uni", juce::String (uni), "Start universe");
    aw->addTextEditor ("addr", juce::String (addr), "Start address");
    aw->addTextEditor ("gap", "0", "Free channels between fixtures");
    aw->addButton ("Re-patch", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PatchView> (this), aw, fids] (int r)
    {
        if (safe == nullptr || r != 1) return;
        juce::String err;
        {
            const juce::ScopedLock sl (safe->engine.dataLock);
            err = safe->engine.patch.repatch (fids, aw->getTextEditorContents ("uni").getIntValue(),
                                              aw->getTextEditorContents ("addr").getIntValue(),
                                              aw->getTextEditorContents ("gap").getIntValue(), safe->engine.library);
        }
        safe->engine.showChanged();
        safe->showMessage (err.isNotEmpty() ? err : "Re-patched " + juce::String ((int) fids.size()) + " fixtures", err.isNotEmpty());
    }), true);
}

void PatchView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    sidePanel = r.removeFromRight (340);
    r.removeFromRight (theme::gap);

    auto bar = r.removeFromTop (theme::buttonH);
    {
        struct B { juce::Component* c; int w; int gapAfter; };
        const B list[] { { &builderButton, 150, 12 }, { &repatchButton, 110, 12 }, { &newFolderButton, 112, 4 }, { &moveButton, 122, 4 },
                         { &renameFolderButton, 128, 12 }, { &groupsButton, 150, 12 }, { &invertPanButton, 96, 4 },
                         { &invertTiltButton, 96, 12 }, { &highlightButton, 96, 0 } };
        int total = 0;
        for (auto& b : list) total += b.w + b.gapAfter;
        const float k = juce::jmin (1.0f, (float) bar.getWidth() / (float) total);
        for (auto& b : list)
        {
            b.c->setBounds (bar.removeFromLeft (juce::roundToInt ((float) b.w * k)));
            bar.removeFromLeft (juce::roundToInt ((float) b.gapAfter * k));
        }
    }
    r.removeFromTop (theme::gap);
    table.setBounds (r);

    sideViewport.setBounds (sidePanel.reduced (4));
    sideHolder.setSize (sideViewport.getMaximumVisibleWidth(), 720);
    auto p = sideHolder.getLocalBounds().reduced (10);
    auto row = [&p] (juce::Label& l, juce::Component& c)
    {
        auto line = p.removeFromTop (theme::rowHeight - 2);
        l.setBounds (line.removeFromLeft (84));
        c.setBounds (line);
        p.removeFromTop (5);
    };

    addHeader.setBounds (p.removeFromTop (20));
    p.removeFromTop (4);
    row (searchLabel, typeSearch);
    row (typeLabel, typeCombo);
    row (qtyLabel, qtyEdit);
    row (fidLabel, fidEdit);
    row (uniLabel, uniEdit);
    {
        auto line = p.removeFromTop (theme::rowHeight - 2);
        addrLabel.setBounds (line.removeFromLeft (84));
        nextAddrButton.setBounds (line.removeFromRight (90));
        line.removeFromRight (6);
        addrEdit.setBounds (line);
        p.removeFromTop (5);
    }
    row (nameLabel, nameEdit);
    {
        auto line = p.removeFromTop (theme::rowHeight - 2);
        folderLabel.setBounds (line.removeFromLeft (84));
        folderPick.setBounds (line.removeFromRight (40));
        line.removeFromRight (6);
        folderEdit.setBounds (line);
        p.removeFromTop (5);
    }
    addButton.setBounds (p.removeFromTop (theme::buttonH));

    p.removeFromTop (16);
    editHeader.setBounds (p.removeFromTop (20));
    p.removeFromTop (4);
    row (eFidLabel, eFid);
    row (eNameLabel, eName);
    row (eUniLabel, eUni);
    row (eAddrLabel, eAddr);
    {
        auto line = p.removeFromTop (28);
        line.removeFromLeft (84);
        invertPanToggle.setBounds (line.removeFromLeft (line.getWidth() / 2));
        invertTiltToggle.setBounds (line);
        p.removeFromTop (5);
    }
    {
        auto line = p.removeFromTop (theme::buttonH);
        deleteButton.setBounds (line.removeFromRight (100));
        line.removeFromRight (6);
        applyButton.setBounds (line);
    }
    p.removeFromTop (10);
    message.setBounds (p);
}

//==============================================================================
void PatchView::rebuildRows()
{
    rows.clear();
    juce::StringArray folders;
    for (auto& f : fixtures)
        if (f.folder.isNotEmpty()) folders.addIfNotAlreadyThere (f.folder);

    auto addFolder = [this] (const juce::String& name, const juce::String& label)
    {
        int count = 0;
        for (auto& f : fixtures) if (f.folder == name) ++count;
        if (count == 0) return;
        rows.push_back ({ true, label.isNotEmpty() ? label : name, 0, count });
        rows.back().folder = name;
        if (collapsed.count (name)) return;
        for (auto& f : fixtures)
            if (f.folder == name) rows.push_back ({ false, name, f.fid, 0 });
    };

    if (folders.isEmpty())
    {
        for (auto& f : fixtures) rows.push_back ({ false, {}, f.fid, 0 });
        return;
    }
    for (auto& name : folders) addFolder (name, {});
    addFolder ({}, {});            // fixtures without a folder, at the end
}

int PatchView::getNumRows() { return (int) rows.size(); }

void PatchView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    if (rows[(size_t) row].isFolder)
    {
        g.fillAll (selected ? theme::accent.withAlpha (0.25f) : theme::panel2);
        g.setColour (theme::line);
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
        return;
    }
    if (selected)          g.fillAll (theme::accent.withAlpha (0.25f));
    else if (row % 2 == 1) g.fillAll (theme::panel2.withAlpha (0.35f));
}

void PatchView::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];

    if (r.isFolder)
    {
        if (col != colFid && col != colName) return;
        if (col == colFid)
        {
            const bool open = ! collapsed.count (r.folder);
            juce::Path tri;
            if (open) tri.addTriangle (12.0f, 14.0f, 22.0f, 14.0f, 17.0f, 21.0f);
            else      tri.addTriangle (14.0f, 12.0f, 21.0f, 17.0f, 14.0f, 22.0f);
            g.setColour (theme::accent);
            g.fillPath (tri);
            g.setColour (theme::textDim);
            g.setFont (theme::font (13.0f));
            g.drawText (juce::String (r.count), 28, 0, w - 30, h, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::text);
            g.setFont (theme::font (15.0f, true));
            g.drawText (r.folder.isEmpty() ? juce::String ("No folder") : r.folder, 10, 0, w - 14, h,
                        juce::Justification::centredLeft, true);
        }
        return;
    }

    const PatchedFixture* f = nullptr;
    for (auto& x : fixtures) if (x.fid == r.fid) f = &x;
    if (f == nullptr) return;

    juce::String text;
    juce::Colour colour = theme::text;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto* type = engine.library.find (f->typeId);
        switch (col)
        {
            case colFid:      text = juce::String (f->fid); colour = theme::accent; break;
            case colName:     text = f->name; break;
            case colType:     text = type ? type->displayName() : "Missing type"; colour = theme::textDim; break;
            case colPatch:    text = juce::String (f->universe) + "." + juce::String (f->address).paddedLeft ('0', 3); break;
            case colChannels: text = type ? juce::String (type->footprint()) : "-"; colour = theme::textDim; break;
            case colInvert:
                text = f->invertPan && f->invertTilt ? "both inverted" : f->invertPan ? "pan inverted" : f->invertTilt ? "tilt inverted" : "";
                colour = theme::familyColour (Family::Position);
                break;
            default: break;
        }
    }
    const int indent = (col == colFid && r.folder.isNotEmpty()) ? 28 : 10;
    g.setColour (colour);
    g.setFont (theme::font (15.0f, col == colFid));
    g.drawText (text, indent, 0, w - indent - 4, h, juce::Justification::centredLeft, true);
}

void PatchView::cellClicked (int row, int col, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size() || ! rows[(size_t) row].isFolder) return;
    if (col == colFid)
    {
        auto& name = rows[(size_t) row].folder;
        if (collapsed.count (name)) collapsed.erase (name); else collapsed.insert (name);
        rebuildRows();
        table.updateContent();
        table.repaint();
    }
}

void PatchView::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size() || ! rows[(size_t) row].isFolder) return;
    // double-click a folder: select its fixtures for programming
    std::vector<int> fids;
    for (auto& f : fixtures)
        if (f.folder == rows[(size_t) row].folder) fids.push_back (f.fid);
    { const juce::ScopedLock sl (engine.dataLock); engine.programmer.setSelection (fids); }
    ui.changed();
    showMessage ("Selected " + juce::String ((int) fids.size()) + " fixtures", false);
}

void PatchView::updatePatchHighlight()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (highlightButton.getToggleState())
        {
            auto fids = selectedFids();
            engine.programmer.setHighlightList (fids.empty() ? std::vector<int> { -1 } : fids);   // -1: nothing yet
        }
        else
            engine.programmer.setHighlight (false);
    }
    ui.changed();
}

void PatchView::selectedRowsChanged (int lastRow)
{
    if (highlightButton.getToggleState()) updatePatchHighlight();
    if (lastRow < 0 || lastRow >= (int) rows.size() || rows[(size_t) lastRow].isFolder)
    {
        editingFid = -1;
        for (auto* ed : { &eFid, &eName, &eUni, &eAddr }) ed->clear();
        return;
    }
    for (auto& f : fixtures)
        if (f.fid == rows[(size_t) lastRow].fid)
        {
            editingFid = f.fid;
            eFid.setText (juce::String (f.fid), false);
            eName.setText (f.name, false);
            eUni.setText (juce::String (f.universe), false);
            eAddr.setText (juce::String (f.address), false);
            invertPanToggle.setToggleState (f.invertPan, juce::dontSendNotification);
            invertTiltToggle.setToggleState (f.invertTilt, juce::dontSendNotification);
        }
}

void PatchView::toggleInvert (bool pan)
{
    auto fids = selectedFids();
    if (fids.empty()) { showMessage ("Select fixtures first", true); return; }
    bool allOn = true;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : fids)
            if (auto* f = engine.patch.find (fid)) allOn &= pan ? f->invertPan : f->invertTilt;
    }
    setInvert (pan, ! allOn);
    (pan ? invertPanToggle : invertTiltToggle).setToggleState (! allOn, juce::dontSendNotification);
}

void PatchView::setInvert (bool pan, bool on)
{
    auto fids = selectedFids();
    if (fids.empty()) { showMessage ("Select fixtures first", true); return; }
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : fids)
            if (auto* f = engine.patch.find (fid))
                engine.patch.setInvert (fid, pan ? on : f->invertPan, pan ? f->invertTilt : on);
    }
    showMessage (juce::String (pan ? "Pan" : "Tilt") + (on ? " inverted on " : " normal on ") + juce::String ((int) fids.size()) + " fixture(s)", false);
    engine.showChanged();
}

void PatchView::deleteKeyPressed (int) { deleteSelected(); }

void PatchView::changeListenerCallback (juce::ChangeBroadcaster*) { refreshFromEngine(); }

//==============================================================================
void PatchView::refreshFromEngine()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        fixtures = engine.patch.fixtures();
        if (! fidEdit.hasKeyboardFocus (false))
            fidEdit.setText (juce::String (engine.patch.nextFreeFid()), false);
    }
    rebuildRows();
    refreshTypeCombo();
    suggestNextAddress();
    table.updateContent();
    table.repaint();
}

void PatchView::refreshTypeCombo()
{
    auto previous = typeCombo.getSelectedItemIndex() >= 0 && typeCombo.getSelectedItemIndex() < (int) typeIds.size()
                        ? typeIds[(size_t) typeCombo.getSelectedItemIndex()] : juce::String();
    typeCombo.clear (juce::dontSendNotification);
    typeIds.clear();

    const juce::ScopedLock sl (engine.dataLock);
    const auto words = juce::StringArray::fromTokens (typeSearch.getText().toLowerCase(), " ", "");
    int index = 0, toSelect = 0;
    for (auto& t : engine.library.types())
    {
        const auto hay = (t.displayName() + " " + juce::String (t.footprint()) + "ch").toLowerCase();
        bool match = true;
        for (auto& w : words) match &= w.isEmpty() || hay.contains (w);
        if (! match) continue;
        typeIds.push_back (t.id);
        typeCombo.addItem (t.displayName() + "  (" + juce::String (t.footprint()) + "ch)", ++index);
        if (t.id == previous) toSelect = index - 1;
    }
    if (typeIds.empty()) typeCombo.setTextWhenNothingSelected ("No fixture type matches");
    typeCombo.setSelectedItemIndex (toSelect, juce::dontSendNotification);
}

void PatchView::suggestNextAddress()
{
    const int uni = juce::jmax (1, uniEdit.getText().getIntValue());
    const juce::ScopedLock sl (engine.dataLock);
    addrEdit.setText (juce::String (juce::jmin (512, engine.patch.nextFreeAddress (uni, engine.library))), false);
}

void PatchView::addFixtures()
{
    const int idx = typeCombo.getSelectedItemIndex();
    if (idx < 0) return;

    juce::String err;
    {
        const juce::ScopedLock sl (engine.dataLock);
        err = engine.patch.addRange (typeIds[(size_t) idx],
                                     juce::jlimit (1, 4096, qtyEdit.getText().getIntValue()),
                                     fidEdit.getText().getIntValue(),
                                     juce::jmax (1, uniEdit.getText().getIntValue()),
                                     addrEdit.getText().getIntValue(),
                                     nameEdit.getText().trim(), engine.library, folderEdit.getText().trim());
    }
    if (err.isNotEmpty()) { showMessage (err, true); return; }
    fidEdit.clear();
    showMessage ("Added " + qtyEdit.getText() + " fixture(s)", false);
    engine.showChanged();
}

void PatchView::applyEdit()
{
    if (editingFid < 0) return;
    juce::String err;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto* existing = engine.patch.find (editingFid);
        if (existing == nullptr) return;
        auto f = *existing;
        f.fid = eFid.getText().getIntValue();
        f.name = eName.getText().trim();
        f.universe = eUni.getText().getIntValue();
        f.address = eAddr.getText().getIntValue();
        err = engine.patch.update (editingFid, f, engine.library);
        if (err.isEmpty() && f.fid != editingFid)
        {
            engine.programmer.clearSelection();   // the selection referenced the old FID
            editingFid = f.fid;
        }
    }
    showMessage (err.isNotEmpty() ? err : "Fixture updated", err.isNotEmpty());
    if (err.isEmpty()) engine.showChanged();
}

std::vector<int> PatchView::selectedFids() const
{
    std::vector<int> out;
    auto sel = table.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
    {
        const int r = sel[i];
        if (r < 0 || r >= (int) rows.size()) continue;
        if (rows[(size_t) r].isFolder)
        {
            for (auto& f : fixtures)
                if (f.folder == rows[(size_t) r].folder && std::find (out.begin(), out.end(), f.fid) == out.end())
                    out.push_back (f.fid);
        }
        else if (std::find (out.begin(), out.end(), rows[(size_t) r].fid) == out.end())
            out.push_back (rows[(size_t) r].fid);
    }
    return out;
}

void PatchView::deleteSelected()
{
    auto fids = selectedFids();
    if (fids.empty()) return;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : fids) engine.patch.remove (fid);
        engine.programmer.clearSelection();
    }
    table.deselectAllRows();
    showMessage ("Deleted " + juce::String ((int) fids.size()) + " fixture(s)", false);
    engine.showChanged();
}

void PatchView::moveSelectedToFolder (const juce::String& folder)
{
    auto fids = selectedFids();
    if (fids.empty()) { showMessage ("Select fixtures first", true); return; }
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : fids) engine.patch.setFolder (fid, folder);
    }
    showMessage ("Moved " + juce::String ((int) fids.size()) + " fixture(s) to "
                 + (folder.isEmpty() ? juce::String ("no folder") : folder), false);
    table.deselectAllRows();
    engine.showChanged();
}

void PatchView::showMoveMenu()
{
    juce::StringArray folders;
    { const juce::ScopedLock sl (engine.dataLock); folders = engine.patch.folders(); }
    juce::PopupMenu m;
    for (int i = 0; i < folders.size(); ++i) m.addItem (i + 2, folders[i]);
    if (! folders.isEmpty()) m.addSeparator();
    m.addItem (1, "No folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moveButton),
                     [safe = juce::Component::SafePointer<PatchView> (this), folders] (int r)
    {
        if (safe == nullptr || r == 0) return;
        safe->moveSelectedToFolder (r == 1 ? juce::String() : folders[r - 2]);
    });
}

void PatchView::renameSelectedFolder()
{
    const int r = table.getSelectedRow();
    if (r < 0 || r >= (int) rows.size() || ! rows[(size_t) r].isFolder || rows[(size_t) r].folder.isEmpty())
    {
        showMessage ("Select a folder row to rename", true);
        return;
    }
    const auto from = rows[(size_t) r].folder;
    ui::askForText (this, "Rename folder", {}, from, [safe = juce::Component::SafePointer<PatchView> (this), from] (const juce::String& to)
    {
        if (safe == nullptr || to.isEmpty()) return;
        { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.patch.renameFolder (from, to); }
        if (safe->collapsed.count (from)) { safe->collapsed.erase (from); safe->collapsed.insert (to); }
        safe->engine.showChanged();
    });
}

void PatchView::showMessage (const juce::String& text, bool isError)
{
    message.setText (text, juce::dontSendNotification);
    message.setColour (juce::Label::textColourId, isError ? theme::danger : theme::ok);
}

} // namespace pl
