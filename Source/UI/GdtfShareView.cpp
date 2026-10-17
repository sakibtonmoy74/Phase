#include "GdtfShareView.h"
#include "Engine/Gdtf.h"
#include "Control/Settings.h"

namespace pl
{

void importGdtfData (DmxEngine& engine, const juce::MemoryBlock& data, juce::Component* target,
                     std::function<void (const juce::String&, bool)> done)
{
    auto d = std::make_shared<gdtf::Description>();
    if (auto err = gdtf::read (data, *d); err.isNotEmpty()) { done (err, true); return; }

    juce::PopupMenu menu;
    menu.addSectionHeader (d->manufacturer + " " + d->name + ": add which mode?");
    if (d->modes.size() > 1) menu.addItem (1, "All modes (" + juce::String ((int) d->modes.size()) + " fixture types)");
    for (int i = 0; i < (int) d->modes.size(); ++i)
    {
        auto& m = d->modes[(size_t) i];
        menu.addItem (100 + i, m.name + "  -  " + juce::String (m.footprint) + " ch"
                                   + (m.cells > 1 ? ", " + juce::String (m.cells) + " cells" : juce::String()));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [&engine, d, done] (int r)
    {
        if (r == 0) return;
        std::vector<juce::String> modes;
        if (r == 1) for (auto& m : d->modes) modes.push_back (m.name);
        else        modes.push_back (d->modes[(size_t) (r - 100)].name);

        juce::StringArray added, failed;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& m : modes)
            {
                FixtureType t;
                if (auto err = gdtf::toFixtureType (*d, m, t); err.isNotEmpty()) { failed.add (m + ": " + err); continue; }
                engine.library.addOrReplace (t);
                added.add (t.name);
            }
        }
        engine.showChanged();
        if (! added.isEmpty())
            done ("Added " + added.joinIntoString (", ") + (failed.isEmpty() ? juce::String() : "  (skipped " + failed.joinIntoString ("; ") + ")"), false);
        else
            done ("Couldn't import: " + failed.joinIntoString ("; "), true);
    });
}

//==============================================================================
enum Columns { colManufacturer = 1, colFixture, colRevision, colModes, colRating, colCreator };

GdtfShareView::GdtfShareView (DmxEngine& e) : engine (e), client (std::make_shared<GdtfShare>())
{
    ui::styleHeader (title, "GDTF SHARE");
    ui::styleField (userLabel, "User");
    ui::styleField (passLabel, "Password");
    ui::styleField (searchLabel, "Search");
    ui::styleEditor (userEdit, false);
    ui::styleEditor (passEdit, false);
    ui::styleEditor (searchEdit, false);
    passEdit.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    passEdit.onReturnKey = [this] { doLogin(); };
    userEdit.setText (settings::get ("gdtfShareUser").toString(), false);
    searchEdit.setTextToShowWhenEmpty ("manufacturer or fixture name", theme::textDim);
    searchEdit.onTextChange = [this] { applyFilter(); };

    loginButton.onClick = [this] { doLogin(); };
    refreshButton.setTooltip ("Download the current fixture list from GDTF Share");
    refreshButton.onClick = [this] { refreshList(); };
    importButton.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.6f));
    importButton.onClick = [this] { importSelected(); };
    signupButton.setTooltip ("GDTF Share accounts are free: opens gdtf-share.com");
    signupButton.onClick = [] { juce::URL ("https://gdtf-share.com").launchInDefaultBrowser(); };

    status.setFont (theme::font (13.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);

    auto& h = table.getHeader();
    h.addColumn ("Manufacturer", colManufacturer, 170);
    h.addColumn ("Fixture", colFixture, 260);
    h.addColumn ("Revision", colRevision, 150);
    h.addColumn ("Modes", colModes, 300);
    h.addColumn ("Rating", colRating, 60);
    h.addColumn ("Creator", colCreator, 120);
    table.setRowHeight (28);
    table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, theme::panel);

    for (auto* c : std::initializer_list<juce::Component*> { &title, &userLabel, &passLabel, &userEdit, &passEdit, &loginButton,
                                                             &signupButton, &status, &searchLabel, &searchEdit, &refreshButton,
                                                             &importButton, &table })
        addAndMakeVisible (c);

    // the list is cached, so searching works before logging in (downloading needs a login)
    if (cacheFile().existsAsFile())
        if (GdtfShare::parseList (cacheFile().loadFileAsString(), all).isEmpty())
            setStatus (juce::String ((int) all.size()) + " fixtures in the saved list (from "
                       + cacheFile().getLastModificationTime().toString (true, false) + "). Log in to download.");
    if (all.empty())
        setStatus ("Log in with your GDTF Share account to browse and download fixtures.");
    applyFilter();
}

GdtfShareView::~GdtfShareView() = default;

juce::File GdtfShareView::cacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Phase").getChildFile ("gdtf-share-list.json");
}

void GdtfShareView::setStatus (const juce::String& t, bool error)
{
    status.setText (t, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, error ? theme::danger : theme::textDim);
}

void GdtfShareView::doLogin()
{
    if (busy) return;
    const auto user = userEdit.getText().trim(), pass = passEdit.getText();
    if (user.isEmpty() || pass.isEmpty()) { setStatus ("Enter your GDTF Share user name and password", true); return; }
    settings::set ("gdtfShareUser", user);          // the password is never stored
    busy = true;
    setStatus ("Logging in...");
    juce::Thread::launch ([safe = juce::WeakReference<GdtfShareView> (this), c = client, user, pass]
    {
        const auto err = c->login (user, pass);
        juce::MessageManager::callAsync ([safe, err]
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (err.isNotEmpty()) { safe->setStatus (err, true); return; }
            safe->passEdit.clear();
            safe->setStatus ("Logged in.");
            if (safe->all.empty()) safe->refreshList();
        });
    });
}

void GdtfShareView::refreshList()
{
    if (busy) return;
    if (! client->isLoggedIn()) { setStatus ("Log in first", true); return; }
    busy = true;
    setStatus ("Downloading the fixture list...");
    juce::Thread::launch ([safe = juce::WeakReference<GdtfShareView> (this), c = client]
    {
        auto list = std::make_shared<std::vector<GdtfShare::Entry>>();
        juce::String raw;
        const auto err = c->fetchList (*list, &raw);
        if (err.isEmpty())
        {
            cacheFile().getParentDirectory().createDirectory();
            cacheFile().replaceWithText (raw);
        }
        juce::MessageManager::callAsync ([safe, err, list]
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (err.isNotEmpty()) { safe->setStatus (err, true); return; }
            safe->all = std::move (*list);
            safe->applyFilter();
            safe->setStatus (juce::String ((int) safe->all.size()) + " fixtures on GDTF Share.");
        });
    });
}

void GdtfShareView::applyFilter()
{
    const auto words = juce::StringArray::fromTokens (searchEdit.getText().toLowerCase(), " ", "");
    shown.clear();
    for (auto& e : all)
    {
        const auto hay = (e.manufacturer + " " + e.fixture + " " + e.revision).toLowerCase();
        bool ok = true;
        for (auto& w : words) ok &= w.isEmpty() || hay.contains (w);
        if (ok) shown.push_back (e);
        if (shown.size() >= 2000) break;          // keep the table quick; search narrows it
    }
    table.updateContent();
    table.repaint();
}

int GdtfShareView::getNumRows() { return (int) shown.size(); }

void GdtfShareView::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    if (selected) g.fillAll (theme::accent.withAlpha (0.25f));
}

void GdtfShareView::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto& e = shown[(size_t) row];
    juce::String t;
    switch (column)
    {
        case colManufacturer: t = e.manufacturer; break;
        case colFixture:      t = e.fixture; break;
        case colRevision:     t = e.revision; break;
        case colModes:        t = e.modes.joinIntoString (", "); break;
        case colRating:       t = e.rating > 0.0f ? juce::String (e.rating, 1) : juce::String ("-"); break;
        case colCreator:      t = e.creator + (e.uploader == "Manuf." ? " (manufacturer)" : ""); break;
        default: break;
    }
    g.setColour (column == colFixture ? theme::text : theme::textDim.brighter (0.3f));
    g.setFont (theme::font (13.5f, column == colFixture));
    g.drawText (t, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void GdtfShareView::cellDoubleClicked (int, int, const juce::MouseEvent&) { importSelected(); }

void GdtfShareView::sortOrderChanged (int columnId, bool forwards)
{
    auto key = [columnId] (const GdtfShare::Entry& e) -> juce::String
    {
        switch (columnId)
        {
            case colFixture:  return e.fixture.toLowerCase();
            case colRevision: return e.revision.toLowerCase();
            case colRating:   return juce::String (e.rating, 2).paddedLeft ('0', 6);
            case colCreator:  return e.creator.toLowerCase();
            default:          return e.manufacturer.toLowerCase() + " " + e.fixture.toLowerCase();
        }
    };
    std::stable_sort (all.begin(), all.end(), [&] (const GdtfShare::Entry& a, const GdtfShare::Entry& b)
                      { return forwards ? key (a) < key (b) : key (b) < key (a); });
    applyFilter();
}

void GdtfShareView::importSelected()
{
    const int row = table.getSelectedRow();
    if (row < 0 || row >= (int) shown.size()) { setStatus ("Pick a fixture in the list", true); return; }
    if (! client->isLoggedIn()) { setStatus ("Log in to download fixtures", true); return; }
    if (busy) return;
    const auto entry = shown[(size_t) row];
    busy = true;
    setStatus ("Downloading " + entry.manufacturer + " " + entry.fixture + "...");
    juce::Thread::launch ([safe = juce::WeakReference<GdtfShareView> (this), c = client, entry]
    {
        auto data = std::make_shared<juce::MemoryBlock>();
        const auto err = c->download (entry.rid, *data);
        juce::MessageManager::callAsync ([safe, err, data]
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (err.isNotEmpty()) { safe->setStatus (err, true); return; }
            importGdtfData (safe->engine, *data, &safe->importButton,
                            [safe] (const juce::String& msg, bool error) { if (safe != nullptr) safe->setStatus (msg, error); });
        });
    });
}

void GdtfShareView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void GdtfShareView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap * 2);
    auto top = r.removeFromTop (theme::rowHeight);
    title.setBounds (top.removeFromLeft (130));
    userLabel.setBounds (top.removeFromLeft (44));
    userEdit.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (12);
    passLabel.setBounds (top.removeFromLeft (76));
    passEdit.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (8);
    loginButton.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (8);
    signupButton.setBounds (top.removeFromLeft (140));
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    auto row = r.removeFromTop (theme::rowHeight);
    searchLabel.setBounds (row.removeFromLeft (60));
    importButton.setBounds (row.removeFromRight (150));
    row.removeFromRight (8);
    refreshButton.setBounds (row.removeFromRight (120));
    row.removeFromRight (12);
    searchEdit.setBounds (row);
    r.removeFromTop (8);
    table.setBounds (r);
}

} // namespace pl
