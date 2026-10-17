#include "DmxMonitorView.h"

namespace pl
{

DmxMonitorView::DmxMonitorView (DmxEngine& e) : engine (e)
{
    ui::styleHeader (header, "DMX OUTPUT");
    info.setFont (theme::font (13.0f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    universeCombo.onChange = [this]
    {
        const int i = universeCombo.getSelectedItemIndex();
        int u = 0;
        if (i >= 0 && i < (int) universes.size()) u = universes[(size_t) i];
        else u = universeCombo.getText().retainCharacters ("0123456789").getIntValue();   // typed
        if (u >= 1 && u <= 32768 && u != shownUniverse)
        {
            shownUniverse = u;
            selected.clear();
            chaseToggle.setToggleState (false, juce::dontSendNotification);
            pushChase();
            refreshFixtureMap();
            repaint();
        }
    };
    addAndMakeVisible (header);
    addAndMakeVisible (info);
    addAndMakeVisible (universeCombo);
    universeCombo.setEditableText (true);
    universeCombo.setTooltip ("Pick a patched universe or type any universe number to test");

    ui::styleHeader (testHeader, "DMX TESTER");
    levelLabel.setFont (theme::font (13.0f));
    levelLabel.setColour (juce::Label::textColourId, theme::textDim);
    levelLabel.setText ("Level for the selected channels", juce::dontSendNotification);
    testInfo.setFont (theme::font (12.5f));
    testInfo.setColour (juce::Label::textColourId, theme::textDim);
    testInfo.setJustificationType (juce::Justification::topLeft);
    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setRange (0, 255, 1);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 26);
    levelSlider.onValueChange = [this] { setLevel ((int) levelSlider.getValue()); };
    chaseSpeed.setSliderStyle (juce::Slider::LinearHorizontal);
    chaseSpeed.setRange (0.25, 20.0, 0.25);
    chaseSpeed.setValue (2.0, juce::dontSendNotification);
    chaseSpeed.setTextValueSuffix (" /s");
    chaseSpeed.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 26);
    chaseSpeed.onValueChange = [this] { pushChase(); };
    chaseToggle.onClick = [this] { pushChase(); };
    fullButton.onClick = [this] { levelSlider.setValue (255); setLevel (255); };
    halfButton.onClick = [this] { levelSlider.setValue (128); setLevel (128); };
    zeroButton.onClick = [this] { levelSlider.setValue (0); setLevel (0); };
    releaseButton.onClick = [this] { release (false); };
    releaseAllButton.onClick = [this] { release (true); };
    releaseAllButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.6f));
    fixtureButton.setTooltip ("Extend the selection to the whole fixture(s) under it");
    fixtureButton.onClick = [this] { selectFixtureChannels(); };
    allButton.onClick = [this] { selected.clear(); for (int c = 1; c <= 512; ++c) selected.insert (c); repaint(); };
    for (auto* c : std::initializer_list<juce::Component*> { &testHeader, &levelLabel, &levelSlider, &fullButton, &halfButton, &zeroButton,
                                                            &releaseButton, &releaseAllButton, &fixtureButton, &allButton,
                                                            &chaseToggle, &chaseSpeed, &testInfo })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    refreshUniverses();
}

DmxMonitorView::~DmxMonitorView() { engine.removeChangeListener (this); }

void DmxMonitorView::visibilityChanged()
{
    if (isVisible()) { refreshUniverses(); startTimerHz (20); }
    else stopTimer();
}

void DmxMonitorView::changeListenerCallback (juce::ChangeBroadcaster*) { refreshUniverses(); }

void DmxMonitorView::refreshUniverses()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        universes = engine.patch.usedUniverses();
    }
    if (universes.empty())
        universes.push_back (1);

    universeCombo.clear (juce::dontSendNotification);
    int toSelect = 0;
    for (int i = 0; i < (int) universes.size(); ++i)
    {
        universeCombo.addItem ("Universe " + juce::String (universes[(size_t) i]), i + 1);
        if (universes[(size_t) i] == shownUniverse) toSelect = i;
    }
    universeCombo.setSelectedItemIndex (toSelect, juce::dontSendNotification);
    shownUniverse = universes[(size_t) toSelect];
    refreshFixtureMap();
}

void DmxMonitorView::refreshFixtureMap()
{
    slotFid.fill (0);
    slotOwner.fill (0);
    const juce::ScopedLock sl (engine.dataLock);
    for (auto& f : engine.patch.fixtures())
    {
        if (f.universe != shownUniverse) continue;
        auto* t = engine.library.find (f.typeId);
        if (t == nullptr) continue;
        const int start = f.address - 1;
        if (start >= 0 && start < 512) slotFid[(size_t) start] = f.fid;
        for (int i = 0; i < t->footprint() && start + i < 512; ++i)
            slotOwner[(size_t) (start + i)] = f.fid;
    }
}

void DmxMonitorView::timerCallback()
{
    if (! engine.getUniverse (shownUniverse, values))
        values.fill (0);

    int testedChannels = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto it = engine.dmxTest.values.find (shownUniverse);
        testValues = it != engine.dmxTest.values.end() ? it->second : std::map<int, int> {};
        for (auto& [u, chans] : engine.dmxTest.values) testedChannels += (int) chans.size();
        if (engine.dmxTest.chase) testedChannels += (int) engine.dmxTest.chaseChannels.size();
    }
    testInfo.setText (juce::String ((int) selected.size()) + " selected  -  click, drag, Shift-click a range, Cmd-click to add.\n"
                      + (testedChannels > 0 ? juce::String (testedChannels) + " channel(s) under test: they override the show until released."
                                            : juce::String ("Nothing under test.")),
                      juce::dontSendNotification);
    testInfo.setColour (juce::Label::textColourId, testedChannels > 0 ? theme::danger : theme::textDim);

    const auto dests = engine.getNetSettings().destinationsFor (shownUniverse);
    juce::String where;
    for (auto& d : dests)
    {
        const int pa = d.portAddress;
        where << (where.isEmpty() ? "" : "   |   ") << "Art-Net " << ((pa >> 8) & 0x7f) << ":" << ((pa >> 4) & 0xf) << ":" << (pa & 0xf)
              << " (" << pa << ") -> " << (d.ip.isEmpty() ? juce::String ("broadcast") : d.ip);
    }
    info.setText (dests.empty() ? juce::String ("Not sent: no Art-Net output covers this universe (Network page)") : where,
                  juce::dontSendNotification);
    repaint (gridArea);
}

void DmxMonitorView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap + 6);
    auto top = r.removeFromTop (theme::buttonH);
    header.setBounds (top.removeFromLeft (110));
    universeCombo.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (16);
    info.setBounds (top);
    r.removeFromTop (10);
    testPanel = r.removeFromRight (260);
    r.removeFromRight (10);
    gridArea = r;

    auto t = testPanel.reduced (10);
    testHeader.setBounds (t.removeFromTop (22));
    t.removeFromTop (6);
    levelLabel.setBounds (t.removeFromTop (20));
    levelSlider.setBounds (t.removeFromTop (30));
    t.removeFromTop (6);
    {
        auto row = t.removeFromTop (32);
        const int w = (row.getWidth() - 8) / 3;
        fullButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        halfButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        zeroButton.setBounds (row);
    }
    t.removeFromTop (6);
    {
        auto row = t.removeFromTop (32);
        const int w = (row.getWidth() - 4) / 2;
        releaseButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        releaseAllButton.setBounds (row);
    }
    t.removeFromTop (6);
    {
        auto row = t.removeFromTop (32);
        const int w = (row.getWidth() - 4) / 2;
        fixtureButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        allButton.setBounds (row);
    }
    t.removeFromTop (12);
    chaseToggle.setBounds (t.removeFromTop (28));
    chaseSpeed.setBounds (t.removeFromTop (30));
    t.removeFromTop (10);
    testInfo.setBounds (t.removeFromTop (90));
}

int DmxMonitorView::channelAt (juce::Point<float> p) const
{
    if (! gridArea.toFloat().contains (p)) return 0;
    const int cols = 32, rowsN = 16;
    const int col = juce::jlimit (0, cols - 1, (int) ((p.x - (float) gridArea.getX()) / ((float) gridArea.getWidth() / (float) cols)));
    const int row = juce::jlimit (0, rowsN - 1, (int) ((p.y - (float) gridArea.getY()) / ((float) gridArea.getHeight() / (float) rowsN)));
    return row * cols + col + 1;
}

void DmxMonitorView::mouseDown (const juce::MouseEvent& e)
{
    const int c = channelAt (e.position);
    if (c == 0) return;
    if (e.mods.isShiftDown() && anchor > 0)
        for (int i = juce::jmin (anchor, c); i <= juce::jmax (anchor, c); ++i) selected.insert (i);
    else if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (! selected.erase (c)) selected.insert (c);
        anchor = c;
    }
    else
    {
        selected = { c };
        anchor = c;
    }
    dragBase = selected;
    // show the channel's current tester value (or output) on the slider
    auto it = testValues.find (c);
    levelSlider.setValue (it != testValues.end() ? it->second : values[(size_t) c - 1], juce::dontSendNotification);
    repaint();
}

void DmxMonitorView::mouseDrag (const juce::MouseEvent& e)
{
    const int c = channelAt (e.position);
    if (c == 0 || anchor == 0) return;
    selected = dragBase;
    for (int i = juce::jmin (anchor, c); i <= juce::jmax (anchor, c); ++i) selected.insert (i);
    repaint();
}

void DmxMonitorView::setLevel (int value)
{
    if (selected.empty()) return;
    if (chaseToggle.getToggleState()) { pushChase(); return; }   // the slider sets the chase level
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto& chans = engine.dmxTest.values[shownUniverse];
        for (int c : selected) chans[c] = juce::jlimit (0, 255, value);
    }
    repaint();
}

void DmxMonitorView::release (bool all)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (all)
        {
            engine.dmxTest.values.clear();
            engine.dmxTest.chase = false;
        }
        else
        {
            auto it = engine.dmxTest.values.find (shownUniverse);
            if (it != engine.dmxTest.values.end())
            {
                for (int c : selected) it->second.erase (c);
                if (it->second.empty()) engine.dmxTest.values.erase (it);
            }
        }
    }
    if (all) chaseToggle.setToggleState (false, juce::dontSendNotification);
    repaint();
}

void DmxMonitorView::pushChase()
{
    const juce::ScopedLock sl (engine.dataLock);
    auto& t = engine.dmxTest;
    t.chase = chaseToggle.getToggleState() && ! selected.empty();
    t.chaseUniverse = shownUniverse;
    t.chaseChannels.assign (selected.begin(), selected.end());
    t.chaseStepsPerSecond = (float) chaseSpeed.getValue();
    t.chaseLevel = juce::jmax (1, (int) levelSlider.getValue());
    if (t.chase)                                   // the chase owns its channels
        if (auto it = t.values.find (shownUniverse); it != t.values.end())
            for (int c : selected) it->second.erase (c);
}

void DmxMonitorView::selectFixtureChannels()
{
    std::set<int> fids;
    for (int c : selected) if (slotOwner[(size_t) c - 1] != 0) fids.insert (slotOwner[(size_t) c - 1]);
    for (int i = 0; i < 512; ++i) if (fids.count (slotOwner[(size_t) i]) > 0) selected.insert (i + 1);
    repaint();
}

void DmxMonitorView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, getLocalBounds().reduced (theme::gap));
    g.setColour (theme::panel2.withAlpha (0.5f));
    g.fillRoundedRectangle (testPanel.toFloat(), 8.0f);

    const int cols = 32, rowsN = 16;
    const float cw = (float) gridArea.getWidth() / (float) cols;
    const float ch = (float) gridArea.getHeight() / (float) rowsN;
    const bool roomy = ch > 30.0f && cw > 30.0f;

    for (int i = 0; i < 512; ++i)
    {
        const float x = (float) gridArea.getX() + (float) (i % cols) * cw;
        const float y = (float) gridArea.getY() + (float) (i / cols) * ch;
        auto cell = juce::Rectangle<float> (x, y, cw, ch).reduced (1.0f);

        const int v = values[(size_t) i];
        const bool patched = slotOwner[(size_t) i] != 0;
        auto base = patched ? theme::panel2 : theme::bg;
        g.setColour (base.interpolatedWith (theme::accent, (float) v / 255.0f * 0.85f));
        g.fillRoundedRectangle (cell, 3.0f);

        if (slotFid[(size_t) i] != 0)
        {
            g.setColour (theme::accent);
            g.fillRect (cell.removeFromLeft (2.0f));
        }

        if (testValues.count (i + 1) > 0)          // under test
        {
            g.setColour (theme::danger);
            g.fillRect (cell.withTrimmedLeft (cell.getWidth() - 4.0f));
        }
        if (selected.count (i + 1) > 0)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (cell, 3.0f, 1.6f);
        }

        g.setColour (v > 150 ? theme::bg : theme::textDim);
        g.setFont (theme::font (roomy ? 10.0f : 8.5f));
        g.drawText (juce::String (i + 1), cell.reduced (3, 1), juce::Justification::topLeft, false);

        g.setColour (v > 150 ? theme::bg : (v > 0 ? theme::text : theme::textDim.withAlpha (0.6f)));
        g.setFont (theme::font (roomy ? 13.0f : 10.0f, true));
        g.drawText (juce::String (v), cell.reduced (3, 1), juce::Justification::bottomRight, false);
    }
}

} // namespace pl
