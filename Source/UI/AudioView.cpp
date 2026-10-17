#include "AudioView.h"
#include "Control/Settings.h"

namespace pl
{

static juce::Colour sourceColour (AudioSource s)
{
    switch (s)
    {
        case AudioSource::Level: return theme::text;
        case AudioSource::Bass:  return theme::familyColour (Family::Dimmer);
        case AudioSource::Mid:   return theme::familyColour (Family::Gobo);
        case AudioSource::High:  return theme::familyColour (Family::Beam);
        case AudioSource::Beat:  return theme::familyColour (Family::Color);
    }
    return theme::text;
}

//==============================================================================
class AudioView::LinkRow : public juce::Component
{
public:
    LinkRow (AudioView& o, int linkId) : id (linkId), owner (o)
    {
        enabled.onClick = [this] { auto on = enabled.getToggleState(); owner.modifyLink (id, [on] (AudioLink& l) { l.enabled = on; }); };
        addAndMakeVisible (enabled);

        for (auto s : allAudioSources()) source.addItem (audioSourceName (s), (int) s + 1);
        source.onChange = [this] { auto s = (AudioSource) (source.getSelectedId() - 1); owner.modifyLink (id, [s] (AudioLink& l) { l.source = s; }); };
        addAndMakeVisible (source);

        auto& attrs = allAttributes();
        for (int i = 1; i < (int) attrs.size(); ++i) attribute.addItem (attrs[(size_t) i].label, i);
        attribute.onChange = [this]
        {
            auto& a = allAttributes();
            const int i = attribute.getSelectedId();
            if (i > 0 && i < (int) a.size())
            {
                juce::String name = a[(size_t) i].name;
                owner.modifyLink (id, [name] (AudioLink& l) { l.attribute = name; });
            }
        };
        addAndMakeVisible (attribute);

        mode.addItem ("Scale", 1);
        mode.addItem ("Set", 2);
        mode.setTooltip ("Scale: multiplies the value (an audio master). Set: the attribute follows the audio.");
        mode.onChange = [this] { auto m = mode.getSelectedId() == 2 ? AudioLinkMode::Set : AudioLinkMode::Scale; owner.modifyLink (id, [m] (AudioLink& l) { l.mode = m; }); };
        addAndMakeVisible (mode);

        range.setSliderStyle (juce::Slider::TwoValueHorizontal);
        range.setRange (0.0, 1.0, 0.0);
        range.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        range.setTooltip ("Range: the value at silence (left) and at full level (right)");
        range.onValueChange = [this]
        {
            const float lo = (float) range.getMinValue(), hi = (float) range.getMaxValue();
            owner.modifyLink (id, [lo, hi] (AudioLink& l) { l.minimum = lo; l.maximum = hi; });
            rangeText.setText (juce::String (juce::roundToInt (lo * 100)) + "-" + juce::String (juce::roundToInt (hi * 100)) + "%",
                               juce::dontSendNotification);
        };
        addAndMakeVisible (range);

        rangeText.setFont (theme::font (12.5f));
        rangeText.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (rangeText);

        target.setFont (theme::font (12.5f));
        target.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (target);

        remove.setButtonText (juce::CharPointer_UTF8 ("\xe2\x9c\x95"));
        remove.onClick = [this] { owner.removeLink (id); };
        addAndMakeVisible (remove);

        // response: attack, release, threshold, curve
        auto setup = [this] (juce::Slider& sl, juce::Label& lb, const char* name, double lo, double hi, double mid,
                             const char* tip, std::function<juce::String (double)> fmt)
        {
            lb.setText (name, juce::dontSendNotification);
            lb.setFont (theme::font (11.5f));
            lb.setColour (juce::Label::textColourId, theme::textDim);
            lb.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (lb);
            sl.setSliderStyle (juce::Slider::LinearBar);
            sl.setRange (lo, hi, 0.0);
            sl.setSkewFactorFromMidPoint (mid);
            sl.setTooltip (tip);
            sl.textFromValueFunction = fmt;
            sl.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.3f));
            sl.onValueChange = [this] { pushResponse(); };
            addAndMakeVisible (sl);
        };
        auto ms = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
        setup (attack, attackLabel, "Attack", 0.0, 1000.0, 60.0, "How fast it rises", ms);
        setup (release, releaseLabel, "Release", 0.0, 3000.0, 300.0, "How fast it falls back (higher = smoother)", ms);
        setup (threshold, thresholdLabel, "Gate", 0.0, 0.9, 0.2, "Below this level the link reads zero (ignores noise)",
               [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; });
        setup (curve, curveLabel, "Curve", 0.25, 4.0, 1.0, "Below 1 lifts quiet parts, above 1 is punchier (only peaks)",
               [] (double v) { return juce::String (v, 2); });
    }

    void pushResponse()
    {
        if (loading) return;
        const float a = (float) attack.getValue(), r = (float) release.getValue(), t = (float) threshold.getValue(), c = (float) curve.getValue();
        owner.modifyLink (id, [a, r, t, c] (AudioLink& l) { l.attackMs = a; l.releaseMs = r; l.threshold = t; l.curve = c; });
    }

    void update (const AudioLink& l, const juce::String& targetText)
    {
        enabled.setToggleState (l.enabled, juce::dontSendNotification);
        source.setSelectedId ((int) l.source + 1, juce::dontSendNotification);
        auto& attrs = allAttributes();
        for (int i = 1; i < (int) attrs.size(); ++i)
            if (l.attribute == attrs[(size_t) i].name) attribute.setSelectedId (i, juce::dontSendNotification);
        mode.setSelectedId (l.mode == AudioLinkMode::Set ? 2 : 1, juce::dontSendNotification);
        if (! range.isMouseButtonDown())
        {
            range.setMinAndMaxValues (l.minimum, l.maximum, juce::dontSendNotification);
            rangeText.setText (juce::String (juce::roundToInt (l.minimum * 100)) + "-" + juce::String (juce::roundToInt (l.maximum * 100)) + "%",
                               juce::dontSendNotification);
        }
        target.setText (targetText, juce::dontSendNotification);
        colour = sourceColour (l.source);
        live = l.enabled ? owner.engine.getAudioLinkLevel (l.id) : 0.0f;
        raw = l.enabled ? owner.engine.audio.get (l.source) : 0.0f;
        {
            const juce::ScopedValueSetter<bool> svs (loading, true);
            for (auto [sl, v] : { std::pair<juce::Slider*, float> { &attack, l.attackMs }, { &release, l.releaseMs },
                                  { &threshold, l.threshold }, { &curve, l.curve } })
                if (! sl->isMouseButtonDown()) sl->setValue (v, juce::dontSendNotification);
        }
        range.setColour (juce::Slider::trackColourId, colour.withAlpha (0.6f));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0, 2);
        g.setColour (theme::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 6.0f);
        // live level along the bottom: raw (faint) and after smoothing (bright)
        auto bar = r.removeFromBottom (4.0f);
        g.setColour (colour.withAlpha (0.25f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * raw), 1.5f);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * live), 1.5f);
    }

    void resized() override
    {
        auto all = getLocalBounds().reduced (8, 6);
        auto second = all.removeFromBottom (24);
        all.removeFromBottom (4);
        {
            second.removeFromLeft (34);
            const int w = second.getWidth() / 4;
            for (auto [sl, lb] : { std::pair<juce::Slider*, juce::Label*> { &attack, &attackLabel }, { &release, &releaseLabel },
                                   { &threshold, &thresholdLabel }, { &curve, &curveLabel } })
            {
                auto cell = second.removeFromLeft (w).reduced (3, 0);
                lb->setBounds (cell.removeFromLeft (52));
                cell.removeFromLeft (4);
                sl->setBounds (cell);
            }
        }
        auto r = all;
        enabled.setBounds (r.removeFromLeft (28));
        remove.setBounds (r.removeFromRight (36));
        r.removeFromRight (8);
        target.setBounds (r.removeFromRight (120));
        r.removeFromRight (8);
        source.setBounds (r.removeFromLeft (90));
        r.removeFromLeft (6);
        attribute.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (6);
        mode.setBounds (r.removeFromLeft (82));
        r.removeFromLeft (10);
        rangeText.setBounds (r.removeFromRight (64));
        range.setBounds (r);
    }

    const int id;

private:
    AudioView& owner;
    juce::ToggleButton enabled;
    juce::ComboBox source, attribute, mode;
    juce::Slider range;
    juce::Label rangeText, target;
    juce::TextButton remove;
    juce::Colour colour { theme::text };
    float live = 0.0f, raw = 0.0f;
    bool loading = false;
    juce::Slider attack, release, threshold, curve;
    juce::Label attackLabel, releaseLabel, thresholdLabel, curveLabel;
};

//==============================================================================
AudioView::AudioView (DmxEngine& e, UiState& u, AudioInput& a) : engine (e), ui (u), audio (a)
{
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (audio.deviceManager, 1, 2, 0, 0,
                                                                            false, false, false, false);
    addAndMakeVisible (deviceSelector.get());

    ui::styleHeader (inputHeader, "AUDIO INPUT");
    status.setFont (theme::font (12.5f));
    status.setColour (juce::Label::textColourId, theme::textDim);
    ui::styleField (gainLabel, "Gain");

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setRange (0.1, 10.0, 0.0);
    gainSlider.setSkewFactorFromMidPoint (1.0);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 24);
    gainSlider.setNumDecimalPlacesToDisplay (2);
    gainSlider.setValue (audio.gain.load(), juce::dontSendNotification);
    gainSlider.onValueChange = [this] { audio.gain = (float) gainSlider.getValue(); };
    autoGainToggle.setToggleState (audio.autoGain.load(), juce::dontSendNotification);
    autoGainToggle.setTooltip ("Keeps quiet and loud inputs using the full range");
    autoGainToggle.onClick = [this] { audio.autoGain = autoGainToggle.getToggleState(); };
    followBeatToggle.setTooltip ("Every detected beat taps the tempo of the Dimmer FX speed master");

    ui::styleHeader (linksHeader, "AUDIO LINKS");
    linksHint.setFont (theme::font (12.5f));
    linksHint.setColour (juce::Label::textColourId, theme::textDim);
    linksHint.setText ("Select fixtures and press + Link selection, or link a stored group. "
                       "Scale = audio master (e.g. bass pumps intensity). Set = the attribute follows the audio.",
                       juce::dontSendNotification);
    addSelectionButton.onClick = [this] { addLink (0); };
    addGroupButton.onClick = [this]
    {
        juce::PopupMenu m;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& [n, p] : engine.presets.pool (PresetPool::Group))
                m.addItem (n, juce::String (n) + "  " + p.name);
        }
        if (m.getNumItems() == 0) m.addItem (-1, "No groups stored yet (Presets page)", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addGroupButton),
                         [safe = juce::Component::SafePointer<AudioView> (this)] (int r)
        {
            if (safe != nullptr && r > 0) safe->addLink (r);
        });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &inputHeader, &status, &gainLabel, &gainSlider, &autoGainToggle,
                                                            &followBeatToggle, &linksHeader, &linksHint,
                                                            &addSelectionButton, &addGroupButton })
        addAndMakeVisible (c);

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    engine.addChangeListener (this);
    rebuildRows();
    startTimerHz (30);
}

AudioView::~AudioView()
{
    engine.removeChangeListener (this);
    auto* o = new juce::DynamicObject();
    o->setProperty ("gain", audio.gain.load());
    o->setProperty ("autoGain", audio.autoGain.load());
    settings::set ("audioAnalysis", juce::var (o));
}

void AudioView::changeListenerCallback (juce::ChangeBroadcaster*) { rebuildRows(); }

void AudioView::timerCallback()
{
    const int beats = audio.beatCount.load();
    if (beats != lastBeatCount) { lastBeatCount = beats; beatFlash = 1.0f; }
    beatFlash *= 0.85f;

    if (! isShowing()) return;
    status.setText (audio.deviceStatus(), juce::dontSendNotification);
    repaint (meterArea);

    std::vector<AudioLink> links;
    std::map<int, juce::String> targets;
    {
        const juce::ScopedLock sl (engine.dataLock);
        links = engine.audioLinks;
        for (auto& l : links)
        {
            if (l.groupNumber > 0)
            {
                auto* g = engine.presets.find (PresetPool::Group, l.groupNumber);
                targets[l.id] = "Group " + juce::String (l.groupNumber) + (g ? " " + g->name : juce::String (" (missing)"));
            }
            else targets[l.id] = juce::String ((int) l.fixtures.size()) + " fixtures";
        }
    }
    for (auto* row : rows)
        for (auto& l : links)
            if (l.id == row->id) row->update (l, targets[l.id]);
}

void AudioView::rebuildRows()
{
    std::vector<int> ids;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (auto& l : engine.audioLinks) ids.push_back (l.id);
    }
    if (ids != rowIds)
    {
        rows.clear();
        rowIds = ids;
        for (int id : ids) holder.addAndMakeVisible (rows.add (new LinkRow (*this, id)));
        resized();
    }
    timerCallback();
}

void AudioView::addLink (int groupNumber)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        AudioLink l;
        int maxId = 0;
        for (auto& x : engine.audioLinks) maxId = juce::jmax (maxId, x.id);
        l.id = maxId + 1;
        l.threshold = 0.05f;
        l.groupNumber = groupNumber;
        if (groupNumber == 0)
        {
            l.fixtures = engine.programmer.selection();
            if (l.fixtures.empty()) return;
        }
        engine.audioLinks.push_back (l);
    }
    engine.showChanged();
}

void AudioView::modifyLink (int id, std::function<void (AudioLink&)> fn)
{
    const juce::ScopedLock sl (engine.dataLock);
    for (auto& l : engine.audioLinks)
        if (l.id == id) fn (l);
}

void AudioView::removeLink (int id)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.audioLinks.erase (std::remove_if (engine.audioLinks.begin(), engine.audioLinks.end(),
                                                 [id] (const AudioLink& l) { return l.id == id; }),
                                 engine.audioLinks.end());
    }
    engine.showChanged();
}

//==============================================================================
void AudioView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, leftPanel);
    ui::paintPanel (g, rightPanel);

    // band meters
    const auto sources = allAudioSources();
    const int n = (int) sources.size();
    const float w = (float) meterArea.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto col = juce::Rectangle<float> ((float) meterArea.getX() + (float) i * w, (float) meterArea.getY(),
                                           w, (float) meterArea.getHeight()).reduced (5.0f, 0.0f);
        auto label = col.removeFromBottom (18.0f);
        const float v = sources[(size_t) i] == AudioSource::Beat ? juce::jmax (engine.audio.beat.load(), beatFlash)
                                                                 : engine.audio.get (sources[(size_t) i]);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (col, 4.0f);
        g.setColour (sourceColour (sources[(size_t) i]));
        g.fillRoundedRectangle (col.withTop (col.getBottom() - col.getHeight() * juce::jlimit (0.0f, 1.0f, v)), 4.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f, true));
        g.drawText (audioSourceName (sources[(size_t) i]), label, juce::Justification::centred);
    }
}

void AudioView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    leftPanel = r.removeFromLeft (juce::jmin (480, r.getWidth() * 2 / 5));
    r.removeFromLeft (theme::gap);
    rightPanel = r;

    auto l = leftPanel.reduced (12);
    inputHeader.setBounds (l.removeFromTop (20));
    status.setBounds (l.removeFromTop (20));
    {
        auto bottom = l.removeFromBottom (30);
        followBeatToggle.setBounds (bottom);
        l.removeFromBottom (4);
        auto g = l.removeFromBottom (30);
        gainLabel.setBounds (g.removeFromLeft (44));
        autoGainToggle.setBounds (g.removeFromRight (110));
        gainSlider.setBounds (g);
        l.removeFromBottom (8);
        meterArea = l.removeFromBottom (juce::jmin (130, l.getHeight() / 2));
        l.removeFromBottom (8);
    }
    deviceSelector->setBounds (l);

    auto p = rightPanel.reduced (12);
    {
        auto top = p.removeFromTop (34);
        linksHeader.setBounds (top.removeFromLeft (120));
        addGroupButton.setBounds (top.removeFromRight (130));
        top.removeFromRight (6);
        addSelectionButton.setBounds (top.removeFromRight (150));
    }
    linksHint.setBounds (p.removeFromTop (34));
    p.removeFromTop (4);
    viewport.setBounds (p);
    const int w = viewport.getMaximumVisibleWidth(), h = 82;
    holder.setSize (w, juce::jmax (1, rows.size() * h));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * h, w, h);
}

} // namespace pl
