#include "ExecutorBar.h"
#include "EditOps.h"

namespace pl
{

//==============================================================================
class ExecutorBar::Strip : public juce::Component
{
public:
    Strip (ExecutorBar& o, int s) : owner (o), slot (s)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fader.setRange (0.0, 1.0, 0.0);
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.onValueChange = [this]
        {
            if (updating) return;
            const juce::ScopedLock sl (owner.engine.dataLock);
            owner.engine.playback.setFader (owner.ui.page, slot, (float) fader.getValue(), DmxEngine::now());
        };
        addAndMakeVisible (fader);

        for (int i = 0; i < Executor::numButtons; ++i)
        {
            auto* btn = buttons.add (new juce::TextButton (buttonFunctionName (defaultFunction (i))));
            btn->onStateChange = [this, i]
            {
                const bool down = buttons[i]->isDown();
                if (down == buttonDown[(size_t) i]) return;
                buttonDown[(size_t) i] = down;
                if (owner.ui.storeArmed)
                {
                    if (down) owner.storeTo (slot);
                    return;
                }
                if (owner.ui.editMode != UiState::EditMode::None)
                {
                    if (down) owner.stripTapped (slot);
                    return;
                }
                {
                    const juce::ScopedLock sl (owner.engine.dataLock);
                    owner.engine.executorButton (owner.ui.page, slot, i, down);
                }
                if (down && functions[(size_t) i] == ButtonFunction::Select) owner.ui.changed();
            };
            addAndMakeVisible (btn);
        }
    }

    static ButtonFunction defaultFunction (int i)
    {
        return i == 0 ? ButtonFunction::Go : i == 1 ? ButtonFunction::Back : i == 2 ? ButtonFunction::Off : ButtonFunction::Flash;
    }

    void refresh()
    {
        juce::String newName, newCue;
        bool on = false, exists = false, chase = false, speedFader = false, held = false;
        float faderValue = 0.0f;
        std::array<ButtonFunction, Executor::numButtons> fns { defaultFunction (0), defaultFunction (1), defaultFunction (2), defaultFunction (3) };
        {
            const juce::ScopedLock sl (owner.engine.dataLock);
            auto* e = owner.engine.playback.find (owner.ui.page, slot);
            auto& st = owner.engine.playback.state (owner.ui.page, slot);
            faderValue = owner.engine.playback.faderPosition (owner.ui.page, slot);
            on = st.isOn();
            held = st.flash || st.swop || st.temp;
            if (e != nullptr)
            {
                exists = true;
                newName = e->name;
                chase = e->chase;
                speedFader = e->faderFunction == FaderFunction::Speed;
                for (int i = 0; i < Executor::numButtons; ++i) fns[(size_t) i] = e->buttons[i];
                if (on && st.cue < (int) e->cues.size())
                    newCue = e->cues[(size_t) st.cue].name + "  " + juce::String (st.cue + 1) + "/" + juce::String ((int) e->cues.size())
                           + (chase && st.paused ? "  ||" : "");
                else
                    newCue = juce::String ((int) e->cues.size()) + (e->cues.size() == 1 ? " cue" : " cues");
            }
        }
        if (fns != functions)
        {
            functions = fns;
            for (int i = 0; i < Executor::numButtons; ++i)
            {
                buttons[i]->setButtonText (buttonFunctionName (fns[(size_t) i]));
                const auto f = fns[(size_t) i];
                buttons[i]->setColour (juce::TextButton::buttonColourId,
                                       f == ButtonFunction::Go || f == ButtonFunction::Toggle ? theme::ok.withAlpha (0.25f)
                                       : f == ButtonFunction::Swop ? theme::danger.withAlpha (0.3f)
                                       : f == ButtonFunction::Flash || f == ButtonFunction::Temp ? theme::accent.withAlpha (0.18f)
                                       : theme::panel2);
            }
        }
        isChase = chase; isSpeed = speedFader; isHeld = held;
        if (! fader.isMouseButtonDown())
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            fader.setValue (faderValue, juce::dontSendNotification);
        }
        fader.setDoubleClickReturnValue (true, isSpeed ? 0.5 : 1.0);
        fader.setColour (juce::Slider::trackColourId, isSpeed ? theme::familyColour (Family::Dimmer)
                                                               : (on ? theme::ok : theme::textDim.withAlpha (0.5f)));

        const bool selected = owner.ui.cuePage == owner.ui.page && owner.ui.cueSlot == slot;
        const int edit = (int) owner.ui.editMode
                       + (owner.ui.editSource.kind == UiState::EditItem::Kind::Executor && owner.ui.editSource.a == owner.ui.page
                          && owner.ui.editSource.b == slot ? 10 : 0);
        if (firstRefresh || newName != name || newCue != cueText || on != isOn || exists != hasExec
            || owner.ui.storeArmed != storeArmed || selected != isSelected || edit != editState)
        {
            firstRefresh = false;
            name = newName; cueText = newCue; isOn = on; hasExec = exists;
            storeArmed = owner.ui.storeArmed; isSelected = selected; editState = edit;
            for (auto* b : buttons)
                b->setEnabled (hasExec || storeArmed || editState != 0);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (isOn ? theme::panel2.brighter (0.06f) : theme::panel2.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 8.0f);

        const bool editArmed = editState % 10 != 0, isSource = editState >= 10;
        juce::Colour edge = storeArmed || (editArmed && editState % 10 == (int) UiState::EditMode::Delete) ? theme::danger
                          : isSource ? juce::Colours::white
                          : editArmed ? theme::familyColour (Family::Position)
                          : (isSelected ? theme::accent : (isOn ? theme::ok : theme::line));
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, storeArmed || isSelected || editArmed || isSource ? 2.0f : 1.0f);

        auto h = headerArea().toFloat();
        g.setColour (isOn ? theme::ok : theme::textDim);
        g.setFont (theme::font (12.0f, true));
        auto top = h.removeFromTop (16.0f).reduced (8.0f, 0.0f);
        g.drawText (juce::String (owner.ui.page) + "." + juce::String (slot), top, juce::Justification::centredLeft);
        juce::String tags;
        if (isChase) tags << "CHASE ";
        if (isSpeed) tags << "SPD";
        if (tags.isNotEmpty())
        {
            g.setColour (theme::familyColour (Family::Dimmer));
            g.setFont (theme::font (10.5f, true));
            g.drawText (tags.trim(), top, juce::Justification::centredRight);
        }
        g.setColour (hasExec ? theme::text : theme::textDim.withAlpha (0.6f));
        g.setFont (theme::font (13.5f, true));
        g.drawText (hasExec ? name : juce::String ("empty"), h.removeFromTop (18.0f).reduced (8.0f, 0.0f),
                    juce::Justification::centredLeft, true);
        g.setColour (isOn ? theme::ok : theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText (cueText, h.removeFromTop (16.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (headerArea().getHeight());
        auto bottom = r.removeFromBottom (30);
        buttons[3]->setBounds (bottom);
        r.removeFromBottom (4);
        auto row = r.removeFromBottom (30);
        const int w = (row.getWidth() - 4) / 2;
        buttons[0]->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (4);
        buttons[1]->setBounds (row);
        r.removeFromBottom (4);
        buttons[2]->setBounds (r.removeFromBottom (22));
        r.removeFromBottom (4);
        fader.setBounds (r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked() || ! headerArea().contains (e.getPosition())) return;
        if (e.mods.isPopupMenu()) owner.showExecutorMenu (slot, this);
        else                      owner.stripTapped (slot);
    }

private:
    juce::Rectangle<int> headerArea() const { return getLocalBounds().reduced (2).withHeight (56); }

    ExecutorBar& owner;
    const int slot;
    juce::Slider fader;
    juce::OwnedArray<juce::TextButton> buttons;
    std::array<bool, Executor::numButtons> buttonDown {};
    std::array<ButtonFunction, Executor::numButtons> functions { ButtonFunction::Go, ButtonFunction::Back, ButtonFunction::Off, ButtonFunction::Flash };
    bool updating = false;
    juce::String name, cueText;
    bool isOn = false, hasExec = false, storeArmed = false, isSelected = false, firstRefresh = true;
    int editState = 0;                       // edit mode (+10 when this strip is the Copy / Move source)
    bool isChase = false, isSpeed = false, isHeld = false;
};

//==============================================================================
ExecutorBar::ExecutorBar (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    ui::styleHeader (pageLabel, "PAGE");
    pageLabel.setJustificationType (juce::Justification::centred);
    pageNumber.setFont (theme::font (30.0f, true));
    pageNumber.setJustificationType (juce::Justification::centred);
    pageNumber.setColour (juce::Label::textColourId, theme::accent);
    pageUp.onClick = [this] { changePage (+1); };
    pageDown.onClick = [this] { changePage (-1); };
    pageUp.setTooltip ("Next page ( ] )");
    pageDown.setTooltip ("Previous page ( [ )");
    for (auto* c : std::initializer_list<juce::Component*> { &pageLabel, &pageNumber, &pageUp, &pageDown })
        addAndMakeVisible (c);

    for (int s = 1; s <= Playback::executorsPerPage; ++s)
        addAndMakeVisible (strips.add (new Strip (*this, s)));

    grandFader.setDefault (1.0);
    grandFader.onChange = [this] (float v) { engine.masters.grand = v; };

    auto speedText = [] (float v) { return juce::String (juce::roundToInt (Masters::speedMultiplier (v) * 100.0f)) + "%"; };
    // dimmer FX speed reads as a tempo: a 60 BPM effect runs at this BPM
    dimSpeedFader.formatter = [] (float v) { return juce::String (juce::roundToInt (Masters::speedMultiplier (v) * Masters::referenceBpm)) + " BPM"; };
    moveSpeedFader.formatter = speedText;
    dimSpeedFader.setDefault (0.5);
    moveSpeedFader.setDefault (0.5);
    dimSpeedFader.onChange  = [this] (float v) { engine.masters.dimmerSpeed = v; };
    moveSpeedFader.onChange = [this] (float v) { engine.masters.movementSpeed = v; };
    sizeFader.setDefault (1.0);
    sizeFader.onChange = [this] (float v) { engine.masters.size = v; };

    blackoutButton.setClickingTogglesState (true);
    blackoutButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
    blackoutButton.setColour (juce::TextButton::textColourOnId, theme::text);
    blackoutButton.setTooltip ("Blackout (B)");
    blackoutButton.onClick = [this] { engine.masters.blackout = blackoutButton.getToggleState(); };

    tapButton.setTooltip ("Tap tempo for the Dimmer FX speed master (T)");
    tapButton.onClick = [this] { tapTempo(); };

    for (auto* c : std::initializer_list<juce::Component*> { &grandFader, &dimSpeedFader, &moveSpeedFader, &sizeFader, &blackoutButton, &tapButton })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    timerCallback();
    startTimerHz (15);
}

ExecutorBar::~ExecutorBar()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void ExecutorBar::changePage (int delta)
{
    ui.page = juce::jmax (1, ui.page + delta);
    ui.changed();
}

void ExecutorBar::toggleBlackout()
{
    engine.masters.blackout = ! engine.masters.blackout.load();
    blackoutButton.setToggleState (engine.masters.blackout.load(), juce::dontSendNotification);
}

void ExecutorBar::tapTempo (double when)
{
    const float bpm = tapper.tap (when);
    tapFlash = 1.0f;
    if (bpm > 0.0f)
    {
        engine.masters.dimmerSpeed = Masters::speedFaderFor (bpm / Masters::referenceBpm);
        if (onMessage) onMessage ("Tempo " + juce::String (bpm, 1) + " BPM", false);
    }
}

void ExecutorBar::changeListenerCallback (juce::ChangeBroadcaster*) { timerCallback(); }

void ExecutorBar::timerCallback()
{
    pageNumber.setText (juce::String (ui.page), juce::dontSendNotification);
    for (auto* s : strips) s->refresh();

    grandFader.setValue (engine.masters.grand.load());
    dimSpeedFader.setValue (engine.masters.dimmerSpeed.load());
    moveSpeedFader.setValue (engine.masters.movementSpeed.load());
    sizeFader.setValue (engine.masters.size.load());
    blackoutButton.setToggleState (engine.masters.blackout.load(), juce::dontSendNotification);
    tapFlash *= 0.7f;
    tapButton.setColour (juce::TextButton::buttonColourId,
                         theme::panel2.interpolatedWith (theme::familyColour (Family::Dimmer), tapFlash));
}

//==============================================================================
void ExecutorBar::paint (juce::Graphics& g)
{
    g.setColour (theme::bg);
    g.fillRect (getLocalBounds());
    ui::paintPanel (g, pagePanel);
    ui::paintPanel (g, masterPanel);
}

void ExecutorBar::resized()
{
    auto r = getLocalBounds().reduced (theme::gap, 6);

    pagePanel = r.removeFromLeft (84);
    {
        auto p = pagePanel.reduced (8);
        pageLabel.setBounds (p.removeFromTop (16));
        pageNumber.setBounds (p.removeFromTop (40));
        p.removeFromTop (4);
        pageUp.setBounds (p.removeFromTop (36));
        p.removeFromTop (6);
        pageDown.setBounds (p.removeFromTop (36));
    }
    r.removeFromLeft (theme::gap);

    masterPanel = r.removeFromRight (300);
    {
        auto m = masterPanel.reduced (8);
        auto keys = m.removeFromBottom (34);
        tapButton.setBounds (keys.removeFromLeft (keys.getWidth() / 2 - 3));
        keys.removeFromLeft (6);
        blackoutButton.setBounds (keys);
        m.removeFromBottom (6);
        const int w = m.getWidth() / 4;
        for (auto* f : { &grandFader, &dimSpeedFader, &moveSpeedFader, &sizeFader })
            f->setBounds (m.removeFromLeft (w));
    }
    r.removeFromRight (theme::gap);

    const int n = strips.size();
    const int w = r.getWidth() / n;
    for (int i = 0; i < n; ++i)
        strips[i]->setBounds (r.getX() + i * w, r.getY(), w, r.getHeight());
}

//==============================================================================
void ExecutorBar::stripTapped (int slot)
{
    if (ui.storeArmed) { storeTo (slot); return; }
    if (ui.editMode != UiState::EditMode::None)
    {
        bool exists = false;
        { const juce::ScopedLock sl (engine.dataLock); exists = engine.playback.find (ui.page, slot) != nullptr; }
        editops::tap (engine, ui, editops::executor (ui.page, slot), exists, this);
        return;
    }
    ui.cuePage = ui.page;
    ui.cueSlot = slot;
    ui.changed();
}

void ExecutorBar::showExecutorMenu (int slot, juce::Component* target)
{
    const int page = ui.page;
    Executor copy;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto* e = engine.playback.find (page, slot);
        if (e == nullptr) { showMessage ("Store something on " + juce::String (page) + "." + juce::String (slot) + " first", true); return; }
        copy = *e;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Executor " + juce::String (page) + "." + juce::String (slot) + "  " + copy.name);
    for (int b = 0; b < Executor::numButtons; ++b)
    {
        juce::PopupMenu sub;
        for (int f = 0; f < buttonFunctionNames().size(); ++f)
            sub.addItem (1000 + b * 100 + f, buttonFunctionNames()[f], true, (int) copy.buttons[b] == f);
        menu.addSubMenu ("Button " + juce::String (b + 1) + ": " + buttonFunctionName (copy.buttons[b]), sub);
    }
    juce::PopupMenu faderMenu;
    faderMenu.addItem (2001, "Master (intensity, auto on / off)", true, copy.faderFunction == FaderFunction::Master);
    faderMenu.addItem (2002, "Speed (chase rate, 50% = normal)", true, copy.faderFunction == FaderFunction::Speed);
    menu.addSubMenu ("Fader: " + juce::String (copy.faderFunction == FaderFunction::Speed ? "Speed" : "Master"), faderMenu);
    menu.addSeparator();
    menu.addItem (3000, "Chase (steps with the Dimmer FX speed)", true, copy.chase);
    juce::PopupMenu rate, fade, mode;
    const float rates[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const char* rateNames[] { "1 step per 4 beats", "1 step per 2 beats", "1 step per beat", "2 steps per beat", "4 steps per beat" };
    for (int i = 0; i < 5; ++i) rate.addItem (3100 + i, rateNames[i], true, std::abs (copy.chaseRate - rates[i]) < 0.001f);
    const float fades[] { 0.0f, 0.25f, 0.5f, 1.0f };
    for (int i = 0; i < 4; ++i) fade.addItem (3200 + i, juce::String (juce::roundToInt (fades[i] * 100.0f)) + "% crossfade", true, std::abs (copy.chaseFade - fades[i]) < 0.001f);
    for (int i = 0; i < chaseModeNames().size(); ++i) mode.addItem (3300 + i, chaseModeNames()[i], true, (int) copy.chaseMode == i);
    menu.addSubMenu ("Chase rate", rate, copy.chase);
    menu.addSubMenu ("Chase fade", fade, copy.chase);
    menu.addSubMenu ("Chase order", mode, copy.chase);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe = juce::Component::SafePointer<ExecutorBar> (this), page, slot, copy] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pb = safe->engine.playback;
        {
            const juce::ScopedLock sl (safe->engine.dataLock);
            if (r >= 1000 && r < 2000)       pb.setButtonFunction (page, slot, (r - 1000) / 100, (ButtonFunction) ((r - 1000) % 100));
            else if (r == 2001 || r == 2002) pb.setFaderFunction (page, slot, r == 2002 ? FaderFunction::Speed : FaderFunction::Master);
            else if (r == 3000)              pb.setChase (page, slot, ! copy.chase, copy.chaseRate, copy.chaseFade, copy.chaseMode);
            else if (r >= 3100 && r < 3105)  pb.setChase (page, slot, true, std::array<float, 5> { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f }[(size_t) (r - 3100)], copy.chaseFade, copy.chaseMode);
            else if (r >= 3200 && r < 3204) pb.setChase (page, slot, true, copy.chaseRate, std::array<float, 4> { 0.0f, 0.25f, 0.5f, 1.0f }[(size_t) (r - 3200)], copy.chaseMode);
            else if (r >= 3300 && r < 3303) pb.setChase (page, slot, true, copy.chaseRate, copy.chaseFade, (ChaseMode) (r - 3300));
        }
        safe->engine.showChanged();
        safe->ui.changed();
    });
}

void ExecutorBar::storeTo (int slot)
{
    const int page = ui.page;
    bool exists = false;
    int nextCue = 1;
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (! engine.programmer.hasValues())
        {
            showMessage ("Programmer is empty", true);
            ui.storeArmed = false;
            ui.changed();
            return;
        }
        if (auto* e = engine.playback.find (page, slot))
        {
            exists = ! e->cues.empty();
            if (exists) nextCue = (int) std::floor (e->cues.back().number) + 1;
        }
    }

    if (! exists)
    {
        doStore (page, slot, StoreMode::Add);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Store onto executor " + juce::String (page) + "." + juce::String (slot));
    menu.addItem (1, "Add as Cue " + juce::String (nextCue));
    menu.addItem (2, "Merge into current cue");
    menu.addItem (3, "Overwrite (new cue list)");
    menu.addSeparator();
    menu.addItem (4, "Cancel");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (strips[slot - 1]),
                        [safe = juce::Component::SafePointer<ExecutorBar> (this), page, slot] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)      safe->doStore (page, slot, StoreMode::Add);
        else if (r == 2) safe->doStore (page, slot, StoreMode::Merge);
        else if (r == 3) safe->doStore (page, slot, StoreMode::Overwrite);
        else { safe->ui.storeArmed = false; safe->ui.changed(); }
    });
}

void ExecutorBar::doStore (int page, int slot, StoreMode mode)
{
    juce::String result;
    {
        const juce::ScopedLock sl (engine.dataLock);
        result = engine.storeFromProgrammer (page, slot, mode);
    }
    ui.storeArmed = false;
    ui.cuePage = page;
    ui.cueSlot = slot;
    ui.changed();
    engine.showChanged();
    showMessage (result);
}

void ExecutorBar::showMessage (const juce::String& text, bool error)
{
    if (onMessage) onMessage (text, error);
}

} // namespace pl
