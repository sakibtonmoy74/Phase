#include "DjLinkView.h"
#include "Control/Settings.h"

namespace pl
{

namespace
{
    juce::String clock (double ms)
    {
        if (ms < 0) return "--:--";
        const int s = (int) (ms / 1000.0);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    void saveSettings (const ProDjLink::Settings& s)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("enabled", s.enabled);
        o->setProperty ("interface", s.interfaceIp);
        o->setProperty ("number", s.deviceNumber);
        o->setProperty ("follow", s.followMasterTempo);
        settings::set ("prodjlink", juce::var (o));
    }
}

void DjLinkView::restoreSettings (ProDjLink& link)
{
    auto v = settings::get ("prodjlink");
    if (! v.isObject()) return;
    ProDjLink::Settings s;
    s.enabled = (bool) v.getProperty ("enabled", false);
    s.interfaceIp = v["interface"].toString();
    if (s.interfaceIp.isNotEmpty() && ! localIPv4Addresses().contains (s.interfaceIp)) s.interfaceIp = {};
    s.deviceNumber = juce::jlimit (0, 7, (int) v.getProperty ("number", 0));
    s.followMasterTempo = (bool) v.getProperty ("follow", true);
    link.setSettings (s);
}

DjLinkView::DjLinkView (ProDjLink& l, DmxEngine& e) : link (l), engine (e)
{
    ui::styleHeader (header, "PRO DJ LINK");
    ui::styleField (ifaceLabel, "Interface");
    ui::styleField (numberLabel, "Phase as player");
    status.setFont (theme::font (13.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);

    enableToggle.setTooltip ("Join the Pro DJ Link network as a virtual CDJ and show the decks");
    followToggle.setTooltip ("The Dimmer FX speed (tap tempo) follows the master deck's BPM, pitch included");
    numberCombo.addItem ("Auto", 1);
    for (int n = 1; n <= 7; ++n) numberCombo.addItem (juce::String (n), n + 1);
    numberCombo.setTooltip ("Player number Phase uses. Auto takes a free number 1-4 (needed to read track data when "
                            "there are fewer than 4 players), else 5-7");
    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setRange (2.0, 20.0, 0.5);
    zoomSlider.setValue (6.0, juce::dontSendNotification);
    zoomSlider.setTextValueSuffix (" s");
    zoomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 24);
    zoomSlider.setTooltip ("Seconds shown each side of the playhead");

    auto s = link.getSettings();
    enableToggle.setToggleState (s.enabled, juce::dontSendNotification);
    followToggle.setToggleState (s.followMasterTempo, juce::dontSendNotification);
    numberCombo.setSelectedId (s.deviceNumber + 1, juce::dontSendNotification);
    ifaceCombo.addItem ("Automatic", 1);
    const auto ips = localIPv4Addresses();
    for (int i = 0; i < ips.size(); ++i) ifaceCombo.addItem (ips[i], i + 2);
    const int at = ips.indexOf (s.interfaceIp);
    ifaceCombo.setSelectedId (at >= 0 ? at + 2 : 1, juce::dontSendNotification);

    enableToggle.onClick = [this] { push(); };
    followToggle.onClick = [this] { push(); };
    ifaceCombo.onChange = [this] { push(); };
    numberCombo.onChange = [this] { push(); };

    for (auto* c : std::initializer_list<juce::Component*> { &header, &enableToggle, &ifaceLabel, &ifaceCombo, &numberLabel,
                                                            &numberCombo, &followToggle, &zoomSlider, &status })
        addAndMakeVisible (c);
    startTimerHz (30);
}

DjLinkView::~DjLinkView() = default;

void DjLinkView::push()
{
    ProDjLink::Settings s;
    s.enabled = enableToggle.getToggleState();
    s.interfaceIp = ifaceCombo.getSelectedId() > 1 ? ifaceCombo.getText() : juce::String();
    s.deviceNumber = juce::jmax (0, numberCombo.getSelectedId() - 1);
    s.followMasterTempo = followToggle.getToggleState();
    link.setSettings (s);
    saveSettings (s);
}

void DjLinkView::timerCallback()
{
    // the lights follow the master deck (also while this page is hidden)
    const auto s = link.getSettings();
    if (s.enabled && s.followMasterTempo)
    {
        const float bpm = link.masterBpm();
        if (bpm > 20.0f && std::abs (bpm - lastFollowed) > 0.05f)
        {
            lastFollowed = bpm;
            engine.masters.dimmerSpeed = Masters::speedFaderFor (bpm / Masters::referenceBpm);
        }
    }
    if (! isShowing()) return;
    if (++ticks % 10 == 0) status.setText (link.statusText(), juce::dontSendNotification);
    repaint (decksArea);
}

void DjLinkView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    barArea = r.removeFromTop (theme::buttonH);
    auto bar = barArea;
    header.setBounds (bar.removeFromLeft (120));
    enableToggle.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    ifaceLabel.setBounds (bar.removeFromLeft (70));
    ifaceCombo.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (8);
    numberLabel.setBounds (bar.removeFromLeft (110));
    numberCombo.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (12);
    followToggle.setBounds (bar.removeFromLeft (240));
    zoomSlider.setBounds (bar.removeFromRight (180));
    bar.removeFromRight (8);
    status.setBounds (bar);
    r.removeFromTop (theme::gap);
    decksArea = r;
}

void DjLinkView::paint (juce::Graphics& g)
{
    const double now = ProDjLink::nowSeconds();
    auto decks = link.decks();
    ui::paintPanel (g, decksArea);
    auto area = decksArea.reduced (10);

    if (decks.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f));
        const auto s = link.getSettings();
        g.drawFittedText (s.enabled ? "Waiting for players...\n\nPut the CDJs / XDJs and this computer on the same network (a switch, or the DJM's link ports), "
                                      "pick that network's interface above. Track data needs rekordbox-analysed tracks."
                                    : "Press Connect to join the Pro DJ Link network.",
                          area.reduced (20), juce::Justification::centred, 6);
        return;
    }

    const int n = (int) decks.size();
    const int gap = 8;
    const int h = juce::jmin (230, (area.getHeight() - gap * (n - 1)) / n);
    for (auto& d : decks)
    {
        paintDeck (g, area.removeFromTop (h), d, now);
        area.removeFromTop (gap);
    }
}

void DjLinkView::paintDeck (juce::Graphics& g, juce::Rectangle<int> r, const ProDjLink::Deck& d, double now)
{
    const auto& st = d.status;
    g.setColour (theme::panel2.withAlpha (0.6f));
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    if (st.master)
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.5f);
    }

    auto info = r.removeFromLeft (250).reduced (10, 8);
    // deck number + badges
    {
        auto top = info.removeFromTop (34);
        auto num = top.removeFromLeft (34).toFloat();
        g.setColour (st.master ? theme::accent : theme::panel);
        g.fillEllipse (num);
        g.setColour (st.master ? theme::bg : theme::text);
        g.setFont (theme::font (18.0f, true));
        g.drawText (juce::String (d.number), num, juce::Justification::centred);
        top.removeFromLeft (8);
        auto badge = [&] (const juce::String& t, bool on, juce::Colour c)
        {
            auto b = top.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (11.0f, true), t)) + 12).reduced (0, 7);
            top.removeFromLeft (4);
            g.setColour (on ? c : theme::panel);
            g.fillRoundedRectangle (b.toFloat(), 4.0f);
            g.setColour (on ? theme::bg : theme::textDim);
            g.setFont (theme::font (11.0f, true));
            g.drawText (t, b, juce::Justification::centred);
        };
        badge (st.playing ? "PLAY" : "PAUSE", st.playing, theme::ok);
        badge ("MASTER", st.master, theme::accent);
        badge ("ON AIR", st.onAir, theme::danger);
        if (st.sync) badge ("SYNC", true, theme::familyColour (Family::Position));
    }
    info.removeFromTop (6);
    const auto* track = d.track.get();
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText (track != nullptr && track->info.title.isNotEmpty() ? track->info.title
                : st.trackPlayer > 0 ? "Track " + juce::String ((juce::int64) st.rekordboxId) : juce::String ("No track"),
                info.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    juce::String sub = track != nullptr ? track->info.artist : juce::String();
    if (track != nullptr && track->info.key.isNotEmpty()) sub << (sub.isEmpty() ? "" : "  -  ") << track->info.key;
    g.drawText (sub, info.removeFromTop (18), juce::Justification::centredLeft, true);
    g.drawText (d.name + "  -  " + (st.trackPlayer > 0 ? "from " + juce::String (st.trackPlayer) + " " + juce::String (st.slot == 3 ? "USB" : st.slot == 2 ? "SD" : st.slot == 4 ? "rekordbox" : "") : juce::String()),
                info.removeFromTop (18), juce::Justification::centredLeft, true);
    info.removeFromTop (6);

    // tempo, pitch, time
    {
        auto row = info.removeFromTop (34);
        const float bpm = d.bpm();
        g.setColour (theme::text);
        g.setFont (theme::font (26.0f, true));
        g.drawText (bpm > 0 ? juce::String (bpm, 1) : juce::String ("--.-"), row.removeFromLeft (100), juce::Justification::centredLeft);
        g.setFont (theme::font (12.0f));
        g.setColour (theme::textDim);
        g.drawText ("BPM", row.removeFromLeft (34), juce::Justification::bottomLeft);
        g.setColour (std::abs (st.pitch) < 0.01f ? theme::textDim : theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText ((st.pitch >= 0 ? "+" : "") + juce::String (st.pitch, 2) + "%", row, juce::Justification::centredRight);
    }
    const double ph = d.playheadMs (now);
    const int len = d.trackLengthMs();
    {
        auto row = info.removeFromTop (22);
        g.setColour (theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (clock (ph), row.removeFromLeft (70), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.drawText (len > 0 && ph >= 0 ? "-" + clock (len - ph) : juce::String(), row.removeFromLeft (70), juce::Justification::centredLeft);
        // beat in bar
        for (int b = 1; b <= 4; ++b)
        {
            auto dot = row.removeFromLeft (22).withSizeKeepingCentre (14, 14).toFloat();
            const bool on = st.beatInBar == b;
            g.setColour (on ? (b == 1 ? theme::danger : theme::accent) : theme::panel);
            g.fillRoundedRectangle (dot, 3.0f);
        }
    }

    // waveforms
    auto wave = r.reduced (8, 8);
    auto overview = wave.removeFromTop (juce::jmax (30, wave.getHeight() / 4));
    wave.removeFromTop (6);
    g.setColour (theme::bg);
    g.fillRoundedRectangle (overview.toFloat(), 4.0f);
    g.fillRoundedRectangle (wave.toFloat(), 4.0f);

    if (track == nullptr || (track->preview.empty() && track->detail.empty()))
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText (d.trackState.isNotEmpty() ? d.trackState : st.trackPlayer > 0 ? juce::String ("Waiting for track data...") : juce::String(),
                    wave, juce::Justification::centred);
        return;
    }

    // overview (the whole track), played part dimmed, playhead
    if (! track->preview.empty())
    {
        const auto& pv = track->preview;
        const int cols = (int) pv.height.size();
        const float cw = (float) overview.getWidth() / (float) cols;
        const float playedTo = len > 0 && ph >= 0 ? (float) (ph / len) : -1.0f;
        for (int i = 0; i < cols; ++i)
        {
            const float hgt = (float) pv.height[(size_t) i] / 31.0f * (float) overview.getHeight();
            const bool played = playedTo >= 0 && (float) i / (float) cols < playedTo;
            g.setColour (juce::Colour (pv.colour[(size_t) i]).withAlpha (played ? 0.45f : 1.0f));
            g.fillRect ((float) overview.getX() + (float) i * cw, (float) overview.getBottom() - hgt, juce::jmax (1.0f, cw), hgt);
        }
        if (playedTo >= 0)
        {
            g.setColour (juce::Colours::white);
            g.fillRect ((float) overview.getX() + playedTo * (float) overview.getWidth() - 1.0f, (float) overview.getY(), 2.0f, (float) overview.getHeight());
        }
    }

    // detail around the playhead with the beat grid
    if (! track->detail.empty() && track->detail.perSecond > 0 && ph >= 0)
    {
        const auto& dt = track->detail;
        const double span = zoomSlider.getValue() * 1000.0;                  // ms each side
        const float cx = (float) wave.getCentreX(), mid = (float) wave.getCentreY();
        const float half = (float) wave.getHeight() * 0.5f - 2.0f;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (wave);
        for (int x = wave.getX(); x < wave.getRight(); ++x)
        {
            const double t = ph + ((double) x - cx) / ((double) wave.getWidth() * 0.5) * span;
            if (t < 0) continue;
            const auto i = (size_t) (t * dt.perSecond / 1000.0);
            if (i >= dt.height.size()) break;
            const float hgt = (float) dt.height[i] / 31.0f * half;
            g.setColour (juce::Colour (dt.colour[i]).withAlpha (x < (int) cx ? 0.75f : 1.0f));
            g.fillRect ((float) x, mid - hgt, 1.0f, hgt * 2.0f);
        }
        if (! track->grid.empty())
        {
            const auto& gt = track->grid.timeMs;
            auto it = std::lower_bound (gt.begin(), gt.end(), (int) (ph - span));
            for (; it != gt.end() && *it <= ph + span; ++it)
            {
                const float x = cx + (float) ((*it - ph) / span) * (float) wave.getWidth() * 0.5f;
                const bool downbeat = track->grid.beatInBar[(size_t) (it - gt.begin())] == 1;
                g.setColour (downbeat ? theme::danger.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.35f));
                g.fillRect (x - (downbeat ? 1.0f : 0.5f), (float) wave.getY(), downbeat ? 2.0f : 1.0f, downbeat ? 10.0f : 6.0f);
                g.fillRect (x - (downbeat ? 1.0f : 0.5f), (float) wave.getBottom() - (downbeat ? 10.0f : 6.0f), downbeat ? 2.0f : 1.0f, downbeat ? 10.0f : 6.0f);
            }
        }
        g.setColour (juce::Colours::white);
        g.fillRect (cx - 1.0f, (float) wave.getY(), 2.0f, (float) wave.getHeight());
    }
}

} // namespace pl
