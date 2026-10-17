#include "NetworkView.h"
#include "EditOps.h"

namespace pl
{

namespace
{
    juce::String paText (int pa)
    {
        return juce::String ((pa >> 8) & 0x7f) + ":" + juce::String ((pa >> 4) & 0xf) + ":" + juce::String (pa & 0xf);
    }
}

//==============================================================================
/** One Art-Net output: On | Interface | Mode | Node IP | Start universe | Universes | Art-Net start | -> | Delete */
class NetworkView::OutputRow : public juce::Component
{
public:
    OutputRow (NetworkView& o, int i) : owner (o), index (i)
    {
        onToggle.setTooltip ("Send this output");
        onToggle.onClick = [this] { push(); };

        iface.setTooltip ("Network interface to send from");
        iface.onChange = [this] { push(); };

        mode.addItem ("Broadcast", 1);
        mode.addItem ("Node IP", 2);
        mode.setTooltip ("Broadcast on the interface's network, or send only to one node (unicast)");
        mode.onChange = [this] { push(); };

        for (auto* ed : { &nodeIp, &start, &count, &artStart })
        {
            ui::styleEditor (*ed, ed != &nodeIp);
            ed->onReturnKey = [this] { push(); };
            ed->onFocusLost = [this] { push(); };
        }
        nodeIp.setInputRestrictions (15, "0123456789.");
        nodeIp.setTextToShowWhenEmpty ("e.g. 2.0.0.10", theme::textDim);
        start.setInputRestrictions (5, "0123456789");
        count.setInputRestrictions (5, "0123456789");
        artStart.setInputRestrictions (5, "0123456789");
        start.setTooltip ("First Phase universe this node gets");
        count.setTooltip ("How many universes it outputs (empty or 0 = every universe from the start)");
        count.setTextToShowWhenEmpty ("all", theme::textDim);
        artStart.setTooltip ("The node's Art-Net universe for its first output (Port-Address). Empty = same numbering as Phase (universe 1 = 0)");

        info.setFont (theme::font (12.0f));
        info.setColour (juce::Label::textColourId, theme::textDim);

        deleteButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.6f));
        deleteButton.setTooltip ("Remove this output");
        deleteButton.onClick = [this]
        {
            editops::confirmDelete (&owner, "this Art-Net output", [safe = juce::Component::SafePointer<NetworkView> (&owner), i = index]
            {
                if (safe == nullptr) return;
                auto s = safe->engine.getNetSettings();
                if (i >= 0 && i < (int) s.outputs.size()) s.outputs.erase (s.outputs.begin() + i);
                safe->pushSettings (s);
                safe->rebuildRows();
            }, false);
        };

        for (auto* c : std::initializer_list<juce::Component*> { &onToggle, &iface, &mode, &nodeIp, &start, &count, &artStart, &info, &deleteButton })
            addAndMakeVisible (c);
    }

    void show (const ArtNetOutput& o, const juce::StringArray& interfaces)
    {
        onToggle.setToggleState (o.enabled, juce::dontSendNotification);
        iface.clear (juce::dontSendNotification);
        iface.addItem ("Automatic", 1);
        for (int i = 0; i < interfaces.size(); ++i) iface.addItem (interfaces[i], i + 2);
        const int at = interfaces.indexOf (o.localIp);
        if (at >= 0) iface.setSelectedId (at + 2, juce::dontSendNotification);
        else if (o.localIp.isEmpty()) iface.setSelectedId (1, juce::dontSendNotification);
        else { iface.addItem (o.localIp + " (not here)", 999); iface.setSelectedId (999, juce::dontSendNotification); }
        mode.setSelectedId (o.broadcast ? 1 : 2, juce::dontSendNotification);
        nodeIp.setText (o.nodeIp, false);
        nodeIp.setEnabled (! o.broadcast);
        start.setText (juce::String (o.startUniverse), false);
        count.setText (o.universes > 0 ? juce::String (o.universes) : juce::String(), false);
        artStart.setText (o.artNetStart >= 0 ? juce::String (o.artNetStart) : juce::String(), false);
        artStart.setTextToShowWhenEmpty ("auto " + juce::String (o.startUniverse - 1), theme::textDim);
        localIp = o.localIp;

        const int first = o.portAddressFor (o.startUniverse);
        juce::String t = "Art-Net " + paText (first);
        if (o.universes != 1)
            t << (o.universes > 0 ? " - " + paText (o.portAddressFor (o.startUniverse + o.universes - 1)) : juce::String (" on"));
        info.setText (t, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        for (int i = 0; i < 9; ++i)
        {
            auto cell = r.removeFromLeft (widths[i]);
            r.removeFromLeft (6);
            juce::Component* c[] { &onToggle, &iface, &mode, &nodeIp, &start, &count, &artStart, &info, &deleteButton };
            c[i]->setBounds (cell);
        }
    }

    static constexpr int widths[9] { 34, 170, 110, 140, 70, 74, 84, 150, 70 };

private:
    void push()
    {
        auto s = owner.engine.getNetSettings();
        if (index < 0 || index >= (int) s.outputs.size()) return;
        auto& o = s.outputs[(size_t) index];
        o.enabled = onToggle.getToggleState();
        const int id = iface.getSelectedId();
        o.localIp = id == 1 ? juce::String() : id == 999 ? localIp : iface.getText();
        o.broadcast = mode.getSelectedId() != 2;
        o.nodeIp = nodeIp.getText().trim();
        o.startUniverse = juce::jmax (1, start.getText().getIntValue());
        o.universes = juce::jmax (0, count.getText().getIntValue());
        o.artNetStart = artStart.getText().trim().isEmpty() ? -1 : juce::jlimit (0, artnet::maxPortAddress, artStart.getText().getIntValue());
        owner.pushSettings (s);
        show (o, owner.interfaces);
    }

    NetworkView& owner;
    const int index;
    juce::String localIp;
    juce::ToggleButton onToggle;
    juce::ComboBox iface, mode;
    juce::TextEditor nodeIp, start, count, artStart;
    juce::Label info;
    juce::TextButton deleteButton { "Delete" };
};

//==============================================================================
/** Monitor: the 512 values of the selected stream. */
class NetworkView::Grid : public juce::Component
{
public:
    explicit Grid (NetworkView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const ArtNetMonitor::Stream* st = nullptr;
        if (owner.selectedStream >= 0 && owner.selectedStream < (int) owner.streams.size()) st = &owner.streams[(size_t) owner.selectedStream];
        const int cols = 32, rowsN = 16;
        const float cw = (float) getWidth() / (float) cols, ch = (float) getHeight() / (float) rowsN;
        for (int i = 0; i < 512; ++i)
        {
            const int v = st != nullptr && i < st->length ? st->data[(size_t) i] : 0;
            auto cell = juce::Rectangle<float> ((float) (i % cols) * cw, (float) (i / cols) * ch, cw, ch).reduced (0.5f);
            g.setColour (theme::bg.interpolatedWith (theme::accent, (float) v / 255.0f * 0.9f));
            g.fillRect (cell);
            if (ch > 18.0f && cw > 24.0f)
            {
                g.setColour (v > 150 ? theme::bg : theme::textDim);
                g.setFont (theme::font (9.0f));
                g.drawText (juce::String (v), cell.reduced (2.0f, 1.0f), juce::Justification::bottomRight, false);
            }
        }
    }

private:
    NetworkView& owner;
};

/** Monitor: the streams heard (universe, source, rate). Click one to see its values. */
class NetworkView::StreamList : public juce::Component
{
public:
    explicit StreamList (NetworkView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setFont (theme::font (12.0f, true));
        g.setColour (theme::textDim);
        auto head = r.removeFromTop (20);
        head.removeFromLeft (12);
        g.drawText ("Art-Net", head.removeFromLeft (118), juce::Justification::centredLeft);
        g.drawText ("Source", head.removeFromLeft (110), juce::Justification::centredLeft);
        g.drawText ("Rate", head, juce::Justification::centredLeft);
        g.setFont (theme::font (13.0f));
        for (int i = 0; i < (int) owner.streams.size() && r.getHeight() >= rowH; ++i)
        {
            auto& st = owner.streams[(size_t) i];
            auto row = r.removeFromTop (rowH);
            if (i == owner.selectedStream)
            {
                g.setColour (theme::accent.withAlpha (0.25f));
                g.fillRoundedRectangle (row.toFloat(), 4.0f);
            }
            const bool live = st.fps > 0.0f;
            g.setColour (live ? theme::ok : theme::textDim);
            g.fillEllipse (row.removeFromLeft (12).withSizeKeepingCentre (7, 7).toFloat());
            g.setColour (theme::text);
            g.drawText (paText (st.portAddress) + "  (" + juce::String (st.portAddress) + ")", row.removeFromLeft (118), juce::Justification::centredLeft);
            g.drawText (st.source, row.removeFromLeft (110), juce::Justification::centredLeft);
            g.drawText (live ? juce::String (st.fps, 1) + " /s" : juce::String ("quiet"), row, juce::Justification::centredLeft);
        }
        if (owner.streams.empty())
        {
            g.setColour (theme::textDim);
            g.drawFittedText (owner.monitor.isListening() ? "Listening... nothing heard yet." : "Press Listen to see the Art-Net on the network.",
                              r.reduced (4), juce::Justification::topLeft, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = (e.y - 20) / rowH;
        if (i >= 0 && i < (int) owner.streams.size())
        {
            owner.selectedStream = i;
            owner.selectedKey = { owner.streams[(size_t) i].source, owner.streams[(size_t) i].portAddress };
            repaint();
            if (owner.grid != nullptr) owner.grid->repaint();
        }
    }

private:
    static constexpr int rowH = 22;
    NetworkView& owner;
};

//==============================================================================
NetworkView::~NetworkView() { monitor.stop(); }

NetworkView::NetworkView (DmxEngine& e) : engine (e)
{
    ui::styleHeader (header, "ART-NET OUTPUTS");
    enableToggle.setTooltip ("Art-Net output on / off");
    enableToggle.onClick = [this] { auto s = engine.getNetSettings(); s.enabled = enableToggle.getToggleState(); pushSettings (s); };
    ui::styleField (rateLabel, "Rate");
    rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rateSlider.setRange (1, 60, 1);
    rateSlider.setTextValueSuffix (" Hz");
    rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 26);
    rateSlider.onValueChange = [this] { auto s = engine.getNetSettings(); s.rateHz = (int) rateSlider.getValue(); pushSettings (s); };
    addButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    addButton.setColour (juce::TextButton::textColourOffId, theme::bg);
    addButton.setTooltip ("Add an output: an interface and a node (or broadcast) with its universes. The same interface can be added many times");
    addButton.onClick = [this]
    {
        auto s = engine.getNetSettings();
        ArtNetOutput o;
        if (! s.outputs.empty())
        {
            const auto& last = s.outputs.back();
            o.localIp = last.localIp;
            o.broadcast = false;
            o.startUniverse = last.universes > 0 ? last.startUniverse + last.universes : last.startUniverse + 1;
        }
        else if (! interfaces.isEmpty()) o.localIp = interfaces[0];
        addOutput (o);
    };
    refreshButton.onClick = [this] { interfaces = localIPv4Addresses(); rebuildRows(); };
    columns.setFont (theme::font (12.0f, true));
    columns.setColour (juce::Label::textColourId, theme::textDim);
    status.setFont (theme::font (13.0f));
    rowsViewport.setViewedComponent (&rowsHolder, false);
    rowsViewport.setScrollBarsShown (true, true);

    ui::styleHeader (nodesHeader, "NODES");
    nodesHint.setFont (theme::font (12.5f));
    nodesHint.setColour (juce::Label::textColourId, theme::textDim);
    nodesHint.setJustificationType (juce::Justification::topLeft);
    nodesHint.setText ("Discover the Art-Net nodes on the network, then click one to add it as an output.", juce::dontSendNotification);
    discoverButton.onClick = [this]
    {
        discovery.poll (engine.getNetSettings().primaryInterface());
        polling = true;
        nodesHint.setText ("Polling...", juce::dontSendNotification);
    };

    ui::styleHeader (monitorHeader, "ART-NET MONITOR");
    monitorInfo.setFont (theme::font (12.5f));
    monitorInfo.setColour (juce::Label::textColourId, theme::textDim);
    listenToggle.setTooltip ("Listen on port 6454 and show every Art-Net universe on the network, Phase's own output included");
    listenToggle.onClick = [this]
    {
        if (listenToggle.getToggleState())
        {
            if (! monitor.start ({}))
            {
                listenToggle.setToggleState (false, juce::dontSendNotification);
                monitorInfo.setText ("Couldn't open port 6454 (another Art-Net app has it?)", juce::dontSendNotification);
            }
        }
        else monitor.stop();
    };
    clearButton.onClick = [this] { monitor.clear(); streams.clear(); selectedStream = 0; repaint(); };
    streamList = std::make_unique<StreamList> (*this);
    grid = std::make_unique<Grid> (*this);

    for (auto* c : std::initializer_list<juce::Component*> {
             &header, &enableToggle, &rateLabel, &rateSlider, &addButton, &refreshButton, &columns, &rowsViewport, &status,
             &nodesHeader, &nodesHint, &discoverButton, &monitorHeader, &monitorInfo, &listenToggle, &clearButton,
             streamList.get(), grid.get() })
        addAndMakeVisible (c);

    interfaces = localIPv4Addresses();
    loadFromEngine();
}

void NetworkView::visibilityChanged()
{
    if (isVisible()) { loadFromEngine(); startTimerHz (8); }
    else stopTimer();
}

void NetworkView::loadFromEngine()
{
    auto s = engine.getNetSettings();
    enableToggle.setToggleState (s.enabled, juce::dontSendNotification);
    rateSlider.setValue (s.rateHz, juce::dontSendNotification);
    if (! (s.outputs == shown.outputs) || rows.size() != (int) s.outputs.size()) rebuildRows();
}

void NetworkView::pushSettings (const NetSettings& s)
{
    engine.setNetSettings (s);
    shown = s;
}

void NetworkView::addOutput (ArtNetOutput o)
{
    auto s = engine.getNetSettings();
    s.outputs.push_back (o);
    pushSettings (s);
    rebuildRows();
}

void NetworkView::rebuildRows()
{
    shown = engine.getNetSettings();
    rows.clear();
    for (int i = 0; i < (int) shown.outputs.size(); ++i)
    {
        auto* r = rows.add (new OutputRow (*this, i));
        rowsHolder.addAndMakeVisible (r);
        r->show (shown.outputs[(size_t) i], interfaces);
    }
    layoutRows();
}

void NetworkView::layoutRows()
{
    int w = 0;
    for (int x : OutputRow::widths) w += x + 6;
    w = juce::jmax (w, rowsViewport.getMaximumVisibleWidth());
    rowsHolder.setSize (w, juce::jmax (1, rows.size() * 38));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 38, w, 38);
}

void NetworkView::refreshNodes()
{
    nodeButtons.clear();
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        auto& n = nodes[(size_t) i];
        juce::String outs;
        for (int pa : n.outputs) outs << (outs.isEmpty() ? "" : ", ") << paText (pa);
        auto* b = nodeButtons.add (new juce::TextButton (n.ip + "   " + n.shortName + (outs.isNotEmpty() ? "   out " + outs : juce::String())));
        b->setTooltip (n.longName + " - click to add as an output");
        b->onClick = [this, i, b] { nodeMenu (i, b); };
        addAndMakeVisible (b);
    }
    resized();
}

void NetworkView::nodeMenu (int index, juce::Component* target)
{
    if (index < 0 || index >= (int) nodes.size()) return;
    const auto n = nodes[(size_t) index];
    juce::PopupMenu m;
    m.addSectionHeader (n.ip + "  " + n.shortName);
    m.addItem (1, "Add as an output (unicast to " + n.ip + ")");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe = juce::Component::SafePointer<NetworkView> (this), n] (int r)
    {
        if (safe == nullptr || r != 1) return;
        ArtNetOutput o;
        o.broadcast = false;
        o.nodeIp = n.ip;
        // the interface on the node's network
        for (auto& ip : safe->interfaces)
            if (ip.upToLastOccurrenceOf (".", false, false).upToLastOccurrenceOf (".", false, false)
                == n.ip.upToLastOccurrenceOf (".", false, false).upToLastOccurrenceOf (".", false, false)) { o.localIp = ip; break; }
        o.universes = juce::jmax (1, (int) n.outputs.size());
        if (! n.outputs.empty()) { o.artNetStart = n.outputs.front(); o.startUniverse = n.outputs.front() + 1; }
        safe->addOutput (o);
    });
}

void NetworkView::timerCallback()
{
    loadFromEngine();
    const int pps = engine.getPacketsPerSecond();
    const auto unis = engine.outputUniverses();
    const auto s = engine.getNetSettings();
    juce::StringArray notSent;
    for (int u : unis) if (s.destinationsFor (u).empty()) notSent.add (juce::String (u));
    status.setText (engine.getNetStatus() + "\n" + juce::String (pps) + " packets/s  -  " + juce::String ((int) unis.size()) + " universe(s) patched"
                    + (notSent.isEmpty() ? juce::String() : "  -  not sent (no output covers them): " + notSent.joinIntoString (", ")),
                    juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, ! s.enabled ? theme::danger : ! notSent.isEmpty() ? juce::Colours::orange
                                                : pps > 0 ? theme::ok : theme::textDim);

    if (polling && ! discovery.isPolling())
    {
        polling = false;
        nodes = discovery.nodes();
        nodesHint.setText (nodes.empty() ? "No nodes replied. Check the interface (first output's) and cabling."
                                         : juce::String ((int) nodes.size()) + " node(s). Click one to add it as an output.",
                           juce::dontSendNotification);
        refreshNodes();
    }

    if (monitor.isListening() || ! streams.empty())
    {
        streams = monitor.streams();
        selectedStream = 0;
        for (int i = 0; i < (int) streams.size(); ++i)
            if (streams[(size_t) i].source == selectedKey.first && streams[(size_t) i].portAddress == selectedKey.second) selectedStream = i;
        monitorInfo.setText (monitor.isListening() ? juce::String ((int) streams.size()) + " universe(s) heard"
                                                     + (monitor.otherPackets() > 0 ? ", " + juce::String ((juce::int64) monitor.otherPackets()) + " other Art-Net packets" : juce::String())
                                                   : juce::String ("Stopped"), juce::dontSendNotification);
        streamList->repaint();
        grid->repaint();
    }
}

void NetworkView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, outputsPanel);
    ui::paintPanel (g, nodesPanel);
    ui::paintPanel (g, monitorPanel);

    // column titles above the rows
    auto r = rowsViewport.getBounds().translated (0, -20).withHeight (18);
    g.setFont (theme::font (11.5f, true));
    g.setColour (theme::textDim);
    const char* names[] { "On", "Interface", "Send to", "Node IP", "Start uni.", "Universes", "Art-Net start", "Goes to", "" };
    for (int i = 0; i < 9; ++i)
    {
        g.drawText (names[i], r.removeFromLeft (OutputRow::widths[i]), juce::Justification::centredLeft);
        r.removeFromLeft (6);
    }
}

void NetworkView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    outputsPanel = r.removeFromTop (juce::jmax (220, r.getHeight() * 45 / 100));
    r.removeFromTop (theme::gap);
    nodesPanel = r.removeFromLeft (juce::jmin (380, r.getWidth() / 3));
    r.removeFromLeft (theme::gap);
    monitorPanel = r;

    auto p = outputsPanel.reduced (14);
    {
        auto line = p.removeFromTop (theme::buttonH);
        header.setBounds (line.removeFromLeft (170));
        enableToggle.setBounds (line.removeFromLeft (110));
        line.removeFromLeft (10);
        rateLabel.setBounds (line.removeFromLeft (44));
        rateSlider.setBounds (line.removeFromLeft (200));
        addButton.setBounds (line.removeFromRight (130));
        line.removeFromRight (8);
        refreshButton.setBounds (line.removeFromRight (150));
    }
    status.setBounds (p.removeFromBottom (40));
    p.removeFromTop (24);                                  // column titles
    rowsViewport.setBounds (p);
    layoutRows();

    auto n = nodesPanel.reduced (14);
    {
        auto line = n.removeFromTop (theme::buttonH);
        discoverButton.setBounds (line.removeFromRight (140));
        nodesHeader.setBounds (line);
    }
    n.removeFromTop (6);
    nodesHint.setBounds (n.removeFromTop (36));
    for (auto* b : nodeButtons)
    {
        b->setVisible (n.getHeight() >= 30);
        b->setBounds (n.removeFromTop (30));
        n.removeFromTop (4);
    }

    auto m = monitorPanel.reduced (14);
    {
        auto line = m.removeFromTop (theme::buttonH);
        monitorHeader.setBounds (line.removeFromLeft (170));
        listenToggle.setBounds (line.removeFromLeft (90));
        clearButton.setBounds (line.removeFromLeft (70));
        line.removeFromLeft (10);
        monitorInfo.setBounds (line);
    }
    m.removeFromTop (8);
    streamList->setBounds (m.removeFromLeft (juce::jmin (330, m.getWidth() / 2)));
    m.removeFromLeft (10);
    grid->setBounds (m);
}

} // namespace pl
