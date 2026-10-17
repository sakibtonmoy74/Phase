#include "EffectsPanel.h"
#include "ColorPicker.h"

namespace pl
{

static const std::vector<EffectKind>& panelKinds()
{
    static const std::vector<EffectKind> k { EffectKind::Dimmer, EffectKind::Movement, EffectKind::Color, EffectKind::Attribute, EffectKind::Fly };
    return k;
}

static juce::Colour colourForKind (EffectKind k)
{
    switch (k)
    {
        case EffectKind::Dimmer:    return theme::familyColour (Family::Dimmer);
        case EffectKind::Movement:  return theme::familyColour (Family::Position);
        case EffectKind::Color:     return theme::familyColour (Family::Color);
        case EffectKind::Attribute: return theme::familyColour (Family::Focus);
        case EffectKind::Fly:       return theme::familyColour (Family::Beam);
    }
    return theme::accent;
}

EffectsPanel::EffectsPanel (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    for (auto k : panelKinds())
    {
        auto& d = drafts[(int) k];
        d.kind = k;
        d.form = effects::formsFor (k)[0];
        if (k == EffectKind::Movement) d.bpm = 30.0f;
        if (k == EffectKind::Fly) { d.size = 0.5f; d.tiltSize = 0.3f; d.width = 0.6f; }
        if (k == EffectKind::Attribute) d.attribute = "Zoom";
        d.updateFamily();
    }

    ui::styleHeader (header, "EFFECTS");
    info.setFont (theme::font (12.5f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    info.setJustificationType (juce::Justification::centredLeft);

    for (auto k : panelKinds())
    {
        auto* t = kindTabs.add (new juce::TextButton (k == EffectKind::Movement ? "Move" : k == EffectKind::Attribute ? "Attrib." : effectKindName (k)));
        if (k == EffectKind::Fly) t->setTooltip ("Fly: movement and dimmer together (fly out, in, up, fan)");
        t->setClickingTogglesState (true);
        t->setRadioGroupId (4001);
        t->setColour (juce::TextButton::buttonOnColourId, colourForKind (k));
        t->onClick = [this, k] { setKind (k); };
        addAndMakeVisible (t);
    }

    attributeCombo.setTooltip ("The attribute this effect drives");
    attributeCombo.onChange = [this]
    {
        if (loading) return;
        const int i = attributeCombo.getSelectedItemIndex();
        if (i >= 0 && i < attributeNames.size())
        {
            draft().attribute = attributeNames[i];
            draft().updateFamily();
            loadFromProgrammer();
            refreshControls();
        }
    };
    addChildComponent (attributeCombo);

    colourAButton.onClick = [this] { pickColour (true); };
    colourBButton.onClick = [this] { pickColour (false); };
    addChildComponent (colourAButton);
    addChildComponent (colourBButton);

    auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    auto deg = [] (double v) { return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); };
    auto count = [] (double v) { return v < 2.0 ? juce::String ("Off") : juce::String (juce::roundToInt (v)); };

    speed.setRange (1.0, 300.0, 1.0, 60.0);
    speed.formatter = [] (double v) { return juce::String (juce::roundToInt (v)) + " BPM"; };
    for (auto* k : { &low, &high, &size, &tiltSize }) k->formatter = pct;
    tiltSize.setRange (0.0, 1.0, 0.0, 0.2);
    low.setRange (0.0, 1.0, 0.0, 0.0);
    high.setRange (0.0, 1.0, 0.0, 1.0);
    size.setRange (0.0, 1.0, 0.0, 0.2);
    width.setRange (0.05, 1.0, 0.0, 0.5);
    width.formatter = pct;
    phaseStart.setRange (0.0, 720.0, 1.0, 0.0);
    phaseEnd.setRange (0.0, 720.0, 1.0, 360.0);
    phaseStart.formatter = deg;
    phaseEnd.formatter = deg;
    for (auto* k : { &groups, &blocks, &wings }) { k->setRange (0.0, 16.0, 1.0, 0.0); k->formatter = count; }

    for (auto* k : { &speed, &low, &high, &size, &tiltSize, &width, &phaseStart, &phaseEnd, &groups, &blocks, &wings })
    {
        k->onChange = [this] (double) { paramChanged(); };
        addAndMakeVisible (k);
    }
    reverseButton.setClickingTogglesState (true);
    reverseButton.setTooltip ("Run the shape the other way round (fly: swap left and right)");
    reverseButton.onClick = [this] { paramChanged(); };
    addChildComponent (reverseButton);

    mirrorButton.setClickingTogglesState (true);
    mirrorButton.setTooltip ("Symmetrical from the centre, using selection order");
    mirrorButton.onClick = [this] { paramChanged(); };

    applyButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    applyButton.setColour (juce::TextButton::textColourOffId, theme::bg);
    applyButton.onClick = [this] { apply(); };
    removeButton.onClick = [this] { removeEffect(); };

    for (auto* c : std::initializer_list<juce::Component*> { &header, &info, &mirrorButton, &applyButton, &removeButton })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    setKind (EffectKind::Dimmer);
}

EffectsPanel::~EffectsPanel()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void EffectsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (kind == EffectKind::Attribute) refreshAttributeCombo();
    loadFromProgrammer();
    refreshControls();
}

juce::Colour EffectsPanel::kindColour() const { return colourForKind (kind); }

const EffectParams* EffectsPanel::running() const
{
    return engine.programmer.effect (kind, kind == EffectKind::Attribute ? drafts.at ((int) kind).attribute : juce::String());
}

//==============================================================================
void EffectsPanel::setKind (EffectKind k)
{
    kind = k;
    for (int i = 0; i < kindTabs.size(); ++i)
        kindTabs[i]->setToggleState (panelKinds()[(size_t) i] == k, juce::dontSendNotification);

    formButtons.clear();
    for (auto& name : effects::formsFor (k))
    {
        auto* b = formButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4002);
        b->setColour (juce::TextButton::buttonOnColourId, kindColour());
        b->onClick = [this, name] { draft().form = name; paramChanged(); };
        addAndMakeVisible (b);
    }

    for (auto* kn : { &speed, &low, &high, &size, &tiltSize, &width, &phaseStart, &phaseEnd, &groups, &blocks, &wings })
        kn->setKnobColour (kindColour());

    const bool waveform = k == EffectKind::Dimmer || k == EffectKind::Attribute || k == EffectKind::Fly;
    const bool moves = k == EffectKind::Movement || k == EffectKind::Fly;
    low.setVisible (waveform);
    high.setVisible (waveform);
    width.setVisible (k != EffectKind::Movement);
    width.setLabel (k == EffectKind::Fly ? "Lit time" : "Width");
    size.setVisible (moves);
    size.setLabel (moves ? "Pan size" : "Size");
    tiltSize.setVisible (moves);
    reverseButton.setVisible (moves);
    attributeCombo.setVisible (k == EffectKind::Attribute);
    colourAButton.setVisible (k == EffectKind::Color);
    colourBButton.setVisible (k == EffectKind::Color);
    if (k == EffectKind::Attribute) refreshAttributeCombo();

    loadFromProgrammer();
    refreshControls();
    resized();
}

void EffectsPanel::refreshAttributeCombo()
{
    // attributes of the selected fixtures (or all attributes when nothing is selected)
    juce::StringArray names;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : engine.programmer.selection())
            if (auto* pf = engine.patch.find (fid))
                if (auto* type = engine.library.find (pf->typeId))
                    for (auto& s : type->attributeSlots())
                        names.addIfNotAlreadyThere (baseAttribute (s.attribute));
    }
    if (names.isEmpty())
        for (auto& a : allAttributes())
            if (juce::String (a.name) != kNoAttribute) names.add (a.name);
    if (! names.contains (draft().attribute)) names.insert (0, draft().attribute);

    if (names != attributeNames)
    {
        const juce::ScopedValueSetter<bool> svs (loading, true);
        attributeNames = names;
        attributeCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            attributeCombo.addItem (familyName (familyOf (names[i])) + "  /  " + attributeLabel (names[i]), i + 1);
    }
    const juce::ScopedValueSetter<bool> svs (loading, true);
    attributeCombo.setSelectedItemIndex (attributeNames.indexOf (draft().attribute), juce::dontSendNotification);
}

void EffectsPanel::loadFromProgrammer()
{
    const juce::ScopedLock sl (engine.dataLock);
    if (auto* e = running())
        draft() = *e;
}

void EffectsPanel::refreshControls()
{
    const juce::ScopedValueSetter<bool> svs (loading, true);
    auto& d = draft();
    for (auto* b : formButtons)
        b->setToggleState (b->getButtonText() == d.form, juce::dontSendNotification);

    speed.setValue (d.bpm);
    low.setValue (d.low);
    high.setValue (d.high);
    size.setValue (d.size);
    tiltSize.setValue (d.effectiveTiltSize());
    reverseButton.setToggleState (d.reverse, juce::dontSendNotification);
    width.setValue (d.width);
    phaseStart.setValue (d.phaseStart);
    phaseEnd.setValue (d.phaseEnd);
    groups.setValue (d.groups);
    blocks.setValue (d.blocks);
    wings.setValue (d.wings);
    mirrorButton.setToggleState (d.mirror, juce::dontSendNotification);

    auto styleSwatch = [] (juce::TextButton& b, juce::uint32 argb)
    {
        const juce::Colour c (argb);
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, c.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
    };
    styleSwatch (colourAButton, d.colourA);
    styleSwatch (colourBButton, d.colourB);
    colourBButton.setEnabled (d.form != "Rainbow" && d.form != "Pulse A" && d.form != "Random Hue");

    bool isRunning = false;
    size_t selected = 0, onFixtures = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        selected = engine.programmer.selection().size();
        if (auto* e = running()) { isRunning = true; onFixtures = e->fixtures.size(); }
    }
    for (auto* k : { &speed, &low, &high, &size, &tiltSize, &width, &phaseStart, &phaseEnd, &groups, &blocks, &wings })
        k->setActive (isRunning);

    info.setText (isRunning ? "Running on " + juce::String ((int) onFixtures) + " - edits are live"
                            : juce::String ((int) selected) + " selected - press Apply to run",
                  juce::dontSendNotification);
    info.setColour (juce::Label::textColourId, isRunning ? theme::ok : theme::textDim);
    removeButton.setEnabled (isRunning);
    applyButton.setButtonText (isRunning ? "Re-apply to selection" : "Apply to selection");
}

void EffectsPanel::paramChanged()
{
    if (loading) return;
    auto& d = draft();
    d.bpm        = (float) speed.getValue();
    d.low        = (float) low.getValue();
    d.high       = (float) high.getValue();
    d.size       = (float) size.getValue();
    if (kind == EffectKind::Movement || kind == EffectKind::Fly)
    {
        d.tiltSize = (float) tiltSize.getValue();
        d.reverse  = reverseButton.getToggleState();
    }
    d.width      = (float) width.getValue();
    d.phaseStart = (float) phaseStart.getValue();
    d.phaseEnd   = (float) phaseEnd.getValue();
    d.groups     = (int) groups.getValue();
    d.blocks     = (int) blocks.getValue();
    d.wings      = (int) wings.getValue();
    d.mirror     = mirrorButton.getToggleState();
    colourBButton.setEnabled (d.form != "Rainbow" && d.form != "Pulse A" && d.form != "Random Hue");

    const juce::ScopedLock sl (engine.dataLock);
    if (auto* r = running())
    {
        auto updated = d;
        updated.id = r->id;
        updated.fixtures = r->fixtures;
        engine.programmer.setEffect (updated);
    }
}

void EffectsPanel::apply()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto sel = engine.programmer.selection();
        if (sel.empty()) return;
        auto& d = draft();
        d.kind = kind;
        d.updateFamily();
        d.fixtures = sel;
        if (auto* r = running()) d.id = r->id;
        if (d.id == 0) d.id = EffectParams::nextId();
        engine.programmer.setEffect (d);
    }
    ui.changed();
}

void EffectsPanel::removeEffect()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.programmer.removeEffect (kind, kind == EffectKind::Attribute ? draft().attribute : juce::String());
    }
    draft().id = 0;
    ui.changed();
}

void EffectsPanel::pickColour (bool a)
{
    auto panel = std::make_unique<ColorPickerPanel>();
    panel->setColour (juce::Colour (a ? draft().colourA : draft().colourB));
    panel->onPick = [safe = juce::Component::SafePointer<EffectsPanel> (this), a] (juce::Colour c)
    {
        if (safe == nullptr) return;
        auto& d = safe->draft();
        (a ? d.colourA : d.colourB) = c.getARGB();
        safe->paramChanged();
        safe->refreshControls();
    };
    juce::CallOutBox::launchAsynchronously (std::move (panel), (a ? colourAButton : colourBButton).getScreenBounds(), nullptr);
}

std::vector<Knob*> EffectsPanel::visibleKnobs()
{
    std::vector<Knob*> visible;
    for (auto* k : { &speed, &low, &high, &size, &tiltSize, &width, &phaseStart, &phaseEnd, &groups, &blocks, &wings })
        if (k->isVisible()) visible.push_back (k);
    return visible;
}

void EffectsPanel::nudgeKnob (int number, float delta)
{
    auto v = visibleKnobs();
    if (number >= 1 && number <= (int) v.size())
        v[(size_t) number - 1]->nudge (delta);
}

float EffectsPanel::knobValue (int number)
{
    auto v = visibleKnobs();
    if (number < 1 || number > (int) v.size()) return -1.0f;
    return v[(size_t) number - 1]->getNormalised();
}

//==============================================================================
void EffectsPanel::paint (juce::Graphics& g)
{
    ui::paintPanel (g, panelArea);
}

void EffectsPanel::resized()
{
    panelArea = getLocalBounds().reduced (theme::gap).withTrimmedLeft (0);
    auto r = panelArea.reduced (12);

    header.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    {
        auto tabs = r.removeFromTop (34);
        const int w = (tabs.getWidth() - 4 * (kindTabs.size() - 1)) / kindTabs.size();
        for (auto* t : kindTabs) { t->setBounds (tabs.removeFromLeft (w)); tabs.removeFromLeft (4); }
    }
    r.removeFromTop (8);

    if (kind == EffectKind::Attribute)
    {
        attributeCombo.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
    }
    if (kind == EffectKind::Color)
    {
        auto row = r.removeFromTop (30);
        colourAButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        colourBButton.setBounds (row);
        r.removeFromTop (6);
    }

    {
        const int cols = formButtons.size() > 6 ? 4 : 3, h = formButtons.size() > 6 ? 25 : 28;
        const int w = (r.getWidth() - (cols - 1) * 4) / cols;
        const int rows = (formButtons.size() + cols - 1) / cols;
        auto area = r.removeFromTop (rows * (h + 4));
        for (int i = 0; i < formButtons.size(); ++i)
            formButtons[i]->setBounds (area.getX() + (i % cols) * (w + 4), area.getY() + (i / cols) * (h + 4), w, h);
    }
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (theme::buttonH - 4);
    removeButton.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (6);
    applyButton.setBounds (bottom);
    r.removeFromBottom (6);
    auto mirrorRow = r.removeFromBottom (32);
    mirrorButton.setBounds (mirrorRow.removeFromLeft (90));
    mirrorRow.removeFromLeft (6);
    if (reverseButton.isVisible())
    {
        reverseButton.setBounds (mirrorRow.removeFromLeft (90));
        mirrorRow.removeFromLeft (6);
    }
    mirrorRow.removeFromLeft (4);
    info.setBounds (mirrorRow);
    r.removeFromBottom (6);

    const auto visible = visibleKnobs();

    const int cols = visible.size() > 10 ? 6 : 5;
    const int rows = ((int) visible.size() + cols - 1) / cols;
    const int kw = r.getWidth() / cols;
    const int kh = juce::jmin (112, r.getHeight() / juce::jmax (1, rows));
    for (int i = 0; i < (int) visible.size(); ++i)
        visible[(size_t) i]->setBounds (r.getX() + (i % cols) * kw, r.getY() + (i / cols) * kh, kw - 4, kh - 4);
}

} // namespace pl
