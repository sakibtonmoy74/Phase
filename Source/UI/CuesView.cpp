#include "CuesView.h"
#include "CueFoldersEditor.h"
#include "EditOps.h"

namespace pl
{

class CuesView::CueRow : public juce::Component
{
public:
    CueRow (CuesView& o, int i) : owner (o), index (i)
    {
        number.setFont (theme::font (15.0f, true));
        number.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (number);

        ui::styleEditor (nameEdit, false);
        nameEdit.onReturnKey = [this] { push(); };
        nameEdit.onFocusLost = [this] { push(); };
        addAndMakeVisible (nameEdit);

        for (auto* ed : { &fadeEdit, &fadeOutEdit })
        {
            ui::styleEditor (*ed, false);
            ed->setInputRestrictions (6, "0123456789.");
            ed->setJustification (juce::Justification::centred);
            ed->onReturnKey = [this] { push(); };
            ed->onFocusLost = [this] { push(); };
            addAndMakeVisible (*ed);
        }
        fadeEdit.setTooltip ("Fade in (s): rising intensity and all other attributes");
        fadeOutEdit.setTooltip ("Fade out (s): falling intensity, and Off / fader down");

        for (auto* l : { &fadeLabel, &fadeOutLabel })
        {
            l->setFont (theme::font (12.5f));
            l->setColour (juce::Label::textColourId, theme::textDim);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
        }
        fadeLabel.setText ("In", juce::dontSendNotification);
        fadeOutLabel.setText ("Out", juce::dontSendNotification);

        number.setInterceptsMouseClicks (false, false);       // clicks reach the row (store armed)
        info.setInterceptsMouseClicks (false, false);
        info.setFont (theme::font (12.5f));
        info.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (info);

        goButton.onClick = [this]
        {
            const juce::ScopedLock sl (owner.engine.dataLock);
            owner.engine.playback.gotoCue (owner.shownPage, owner.shownSlot, index, DmxEngine::now());
        };
        deleteButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.7f));
        deleteButton.onClick = [this]
        {
            editops::confirmDelete (&owner, nameEdit.getText().isNotEmpty() ? nameEdit.getText() : "cue " + juce::String (index + 1),
                                    [safe = juce::Component::SafePointer<CuesView> (&owner), i = index]
            {
                if (safe == nullptr) return;
                { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.playback.deleteCue (safe->shownPage, safe->shownSlot, i); }
                safe->engine.showChanged();
            });
        };
        addAndMakeVisible (goButton);
        addAndMakeVisible (deleteButton);
        storeButton.setTooltip ("Store the programmer into this cue (merge / overwrite), or insert a new cue after it. Right-click a cue to stop a tracked effect.");
        storeButton.onClick = [this] { owner.storeMenu (index, &storeButton); };
        addAndMakeVisible (storeButton);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (owner.ui.editMode != UiState::EditMode::None)
        {
            editops::tap (owner.engine, owner.ui, editops::cue (owner.shownPage, owner.shownSlot, index, nameEdit.getText()), true, &owner);
            return;
        }
        // Store armed (Store pressed in the programmer): tapping a cue stores into it
        if (owner.ui.storeArmed) { owner.storeMenu (index, this); return; }
        // right-click: stop a tracked effect from this cue on
        if (e.mods.isPopupMenu()) owner.effectsMenu (index, this);
    }

    void update (const Cue& c, bool current, const std::vector<Playback::CueEffect>& running)
    {
        number.setText (juce::String (c.number, juce::exactlyEqual (c.number, std::floor (c.number)) ? 0 : 1), juce::dontSendNotification);
        number.setColour (juce::Label::textColourId, current ? theme::ok : theme::accent);
        if (! nameEdit.hasKeyboardFocus (true)) nameEdit.setText (c.name, false);
        if (! fadeEdit.hasKeyboardFocus (true))    fadeEdit.setText (juce::String (c.fadeTime, 1), false);
        if (! fadeOutEdit.hasKeyboardFocus (true)) fadeOutEdit.setText (juce::String (c.fadeOut, 1), false);
        // what's running in this cue: its own effects and the ones tracking in
        juce::StringArray fx;
        for (auto& ce : running)
            fx.add (ce.effect.kind == EffectKind::Attribute ? ce.effect.attribute + " " + ce.effect.form : ce.effect.form);
        info.setText (fx.isEmpty() ? juce::String() : "FX: " + fx.joinIntoString (", "), juce::dontSendNotification);
        storeButton.setColour (juce::TextButton::buttonColourId, owner.ui.storeArmed ? theme::danger.withAlpha (0.8f) : theme::panel2);
        isCurrent = current;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0, 2);
        g.setColour (isCurrent ? theme::ok.withAlpha (0.12f) : theme::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 6.0f);
        if (isCurrent)
        {
            g.setColour (theme::ok);
            g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        number.setBounds (r.removeFromLeft (56));
        deleteButton.setBounds (r.removeFromRight (72));
        r.removeFromRight (6);
        goButton.setBounds (r.removeFromRight (60));
        r.removeFromRight (6);
        storeButton.setBounds (r.removeFromRight (64));
        r.removeFromRight (12);
        fadeOutEdit.setBounds (r.removeFromRight (56));
        fadeOutLabel.setBounds (r.removeFromRight (34));
        r.removeFromRight (6);
        fadeEdit.setBounds (r.removeFromRight (56));
        fadeLabel.setBounds (r.removeFromRight (26));
        r.removeFromRight (12);
        auto nameArea = r.removeFromLeft (juce::jmin (260, r.getWidth() / 2));
        nameEdit.setBounds (nameArea);
        r.removeFromLeft (12);
        info.setBounds (r);
    }

private:
    void push()
    {
        {
            const juce::ScopedLock sl (owner.engine.dataLock);
            owner.engine.playback.setCueProperties (owner.shownPage, owner.shownSlot, index,
                                                    nameEdit.getText().trim(), fadeEdit.getText().getFloatValue(),
                                                    fadeOutEdit.getText().getFloatValue());
        }
        owner.engine.showChanged();
    }

    CuesView& owner;
    const int index;
    bool isCurrent = false;
    juce::Label number, fadeLabel, fadeOutLabel, info;
    juce::TextEditor nameEdit, fadeEdit, fadeOutEdit;
    juce::TextButton goButton { "Go" }, deleteButton { "Delete" },
                     storeButton { "Store" };
};

void CuesView::openFolders (int cueIndex)
{
    if (foldersWindow != nullptr) delete foldersWindow.getComponent();
    auto* content = new CueFoldersEditor (engine, shownPage, shownSlot, cueIndex);
    content->setSize (1100, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = "Folders - executor " + juce::String (shownPage) + "." + juce::String (shownSlot) + " cue " + juce::String (cueIndex + 1);
    o.dialogBackgroundColour = theme::bg;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    foldersWindow = o.launchAsync();
}

void CuesView::storeMenu (int cueIndex, juce::Component* target)
{
    juce::String cueName;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto* e = engine.playback.find (shownPage, shownSlot);
        if (e == nullptr || cueIndex >= (int) e->cues.size()) return;
        cueName = e->cues[(size_t) cueIndex].name;
    }
    juce::PopupMenu menu;
    menu.addSectionHeader ("Store programmer - " + cueName);
    menu.addItem (1, "Merge into " + cueName);
    menu.addItem (2, "Overwrite " + cueName);
    menu.addItem (3, "Insert new cue after " + cueName);
    menu.addSeparator();
    menu.addItem (4, "Cancel");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe = juce::Component::SafePointer<CuesView> (this), cueIndex] (int r)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        self.ui.storeArmed = false;
        if (r >= 1 && r <= 3)
        {
            const auto mode = r == 1 ? StoreMode::Merge : r == 2 ? StoreMode::Overwrite : StoreMode::Add;
            juce::String result;
            {
                const juce::ScopedLock sl (self.engine.dataLock);
                result = self.engine.storeFromProgrammer (self.shownPage, self.shownSlot, mode, DmxEngine::now(), cueIndex);
            }
            self.engine.showChanged();
            if (self.onMessage) self.onMessage (result, result.startsWith ("Programmer is empty"));
        }
        self.ui.changed();
        self.rebuild();
    });
}

void CuesView::effectsMenu (int cueIndex, juce::Component* target)
{
    std::vector<Playback::CueEffect> running;
    std::vector<EffectParams> stops;
    juce::String cueName;
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto* e = engine.playback.find (shownPage, shownSlot);
        if (e == nullptr || cueIndex >= (int) e->cues.size()) return;
        running = engine.playback.effectsAt (*e, cueIndex);
        for (auto& fx : e->cues[(size_t) cueIndex].effects) if (fx.stop) stops.push_back (fx);
        cueName = e->cues[(size_t) cueIndex].name;
    }
    auto label = [] (const EffectParams& e)
    {
        return effectKindName (e.kind) + " " + (e.kind == EffectKind::Attribute ? e.attribute + " " : juce::String())
             + e.form + "  (" + juce::String ((int) e.fixtures.size()) + " fixtures)";
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Effects in " + cueName);
    if (running.empty() && stops.empty()) menu.addItem (-1, "No effects running", false);
    menu.addItem (1000, "Folders (groups + preset references)...");
    menu.addSeparator();
    std::vector<std::function<void()>> actions;
    for (auto& ce : running)
    {
        const bool own = ce.fromCue == cueIndex;
        juce::String text = label (ce.effect) + (ce.fromFolder ? "  - folder" : "")
                          + (own ? juce::String() : "  - tracked from cue " + juce::String (ce.fromCue + 1));
        if (ce.fromFolder) { menu.addItem (-1, text, false); continue; }
        auto e = ce.effect;
        actions.push_back ([this, cueIndex, e] { engine.playback.stopEffect (shownPage, shownSlot, cueIndex, e); });
        menu.addItem ((int) actions.size(), (own ? "Remove: " : "Stop here: ") + text);
    }
    if (! stops.empty())
    {
        menu.addSeparator();
        for (auto& e : stops)
        {
            const int id = e.id;
            actions.push_back ([this, cueIndex, id] { engine.playback.removeCueEffect (shownPage, shownSlot, cueIndex, id); });
            menu.addItem ((int) actions.size(), "Let it run again: " + label (e) + " (stopped here)");
        }
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [safe = juce::Component::SafePointer<CuesView> (this), actions, cueIndex] (int r)
    {
        if (safe != nullptr && r == 1000) { safe->openFolders (cueIndex); return; }
        if (safe == nullptr || r < 1 || r > (int) actions.size()) return;
        { const juce::ScopedLock sl (safe->engine.dataLock); actions[(size_t) r - 1](); }
        safe->engine.showChanged();
        safe->rebuild();
    });
}

//==============================================================================
CuesView::CuesView (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    ui::styleHeader (header, "CUE LIST");
    ui::styleField (nameLabel, "Executor");
    ui::styleEditor (nameEdit, false);
    nameEdit.onReturnKey = [this]
    {
        { const juce::ScopedLock sl (engine.dataLock); engine.playback.renameExecutor (shownPage, shownSlot, nameEdit.getText().trim()); }
        engine.showChanged();
    };
    nameEdit.onFocusLost = nameEdit.onReturnKey;

    deleteExecButton.setColour (juce::TextButton::buttonColourId, theme::danger.withAlpha (0.8f));
    deleteExecButton.onClick = [this]
    {
        editops::confirmDelete (this, "executor " + juce::String (shownPage) + "." + juce::String (shownSlot) + " and its cue list",
                                [safe = juce::Component::SafePointer<CuesView> (this), page = shownPage, slot = shownSlot]
        {
            if (safe == nullptr) return;
            { const juce::ScopedLock sl (safe->engine.dataLock); safe->engine.playback.deleteExecutor (page, slot); }
            safe->engine.showChanged();
        });
    };

    trackingToggle.setTooltip ("Tracking: values carry into later cues until changed");
    trackingToggle.onClick = [this]
    {
        { const juce::ScopedLock sl (engine.dataLock); engine.playback.setTracking (shownPage, shownSlot, trackingToggle.getToggleState()); }
        engine.showChanged();
    };

    // chase and executor options
    chaseToggle.setTooltip ("Chase: step through the cues by itself, timed by the Dimmer FX speed master (tap tempo)");
    chaseToggle.onClick = [this] { pushChase(); };
    for (auto* n : { "1 / 4 beats", "1 / 2 beats", "1 / beat", "2 / beat", "4 / beat" }) rateCombo.addItem (n, rateCombo.getNumItems() + 1);
    for (auto* n : { "Snap", "25% fade", "50% fade", "100% fade" }) fadeCombo.addItem (n, fadeCombo.getNumItems() + 1);
    for (int i = 0; i < chaseModeNames().size(); ++i) orderCombo.addItem (chaseModeNames()[i], i + 1);
    rateCombo.setTooltip ("Chase steps per beat of the Dimmer FX speed");
    fadeCombo.setTooltip ("Crossfade between chase steps, as part of a step");
    orderCombo.setTooltip ("Chase order");
    for (auto* c : { &rateCombo, &fadeCombo, &orderCombo }) c->onChange = [this] { pushChase(); };
    faderCombo.addItem ("Master", 1);
    faderCombo.addItem ("Speed", 2);
    faderCombo.setTooltip ("Fader: intensity master (auto on / off), or chase speed (50% = normal)");
    faderCombo.onChange = [this]
    {
        { const juce::ScopedLock sl (engine.dataLock);
          engine.playback.setFaderFunction (shownPage, shownSlot, faderCombo.getSelectedItemIndex() == 1 ? FaderFunction::Speed : FaderFunction::Master); }
        engine.showChanged();
    };
    for (int b = 0; b < Executor::numButtons; ++b)
    {
        auto* cb = buttonCombos.add (new juce::ComboBox());
        for (int f = 0; f < buttonFunctionNames().size(); ++f) cb->addItem (buttonFunctionNames()[f], f + 1);
        cb->setTooltip ("Button " + juce::String (b + 1) + " on the executor strip (and its MIDI button)");
        cb->onChange = [this, b, cb]
        {
            { const juce::ScopedLock sl (engine.dataLock);
              engine.playback.setButtonFunction (shownPage, shownSlot, b, (ButtonFunction) cb->getSelectedItemIndex()); }
            engine.showChanged();
        };
    }
    for (auto* l : { &faderLabel, &buttonsLabel })
    {
        l->setFont (theme::font (12.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    for (auto* c : optionComponents()) addAndMakeVisible (c);

    hint.setFont (theme::font (15.0f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::centred);

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &nameLabel, &nameEdit, &deleteExecButton, &viewport, &hint, &trackingToggle })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    rebuild();
    startTimerHz (5);
}

CuesView::~CuesView()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

std::vector<juce::Component*> CuesView::optionComponents()
{
    std::vector<juce::Component*> v { &chaseToggle, &rateCombo, &fadeCombo, &orderCombo, &faderLabel, &faderCombo, &buttonsLabel };
    for (auto* b : buttonCombos) v.push_back (b);
    return v;
}

void CuesView::pushChase()
{
    static const float rates[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    static const float fades[] { 0.0f, 0.25f, 0.5f, 1.0f };
    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.playback.setChase (shownPage, shownSlot, chaseToggle.getToggleState(),
                                  rates[juce::jlimit (0, 4, rateCombo.getSelectedItemIndex())],
                                  fades[juce::jlimit (0, 3, fadeCombo.getSelectedItemIndex())],
                                  (ChaseMode) juce::jmax (0, orderCombo.getSelectedItemIndex()));
    }
    engine.showChanged();
}

void CuesView::changeListenerCallback (juce::ChangeBroadcaster*) { rebuild(); }
void CuesView::timerCallback() { if (isShowing()) rebuild(); }

void CuesView::rebuild()
{
    shownPage = ui.cuePage;
    shownSlot = ui.cueSlot;

    std::vector<Cue> cues;
    juce::String name;
    int current = -1;
    bool exists = false, tracking = true;
    Executor options;
    std::vector<std::vector<Playback::CueEffect>> running;
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (auto* e = engine.playback.find (shownPage, shownSlot))
        {
            exists = true;
            options = *e;
            cues = e->cues;
            name = e->name;
            tracking = e->tracking;
            current = engine.playback.state (shownPage, shownSlot).cue;
            for (int i = 0; i < (int) e->cues.size(); ++i) running.push_back (engine.playback.effectsAt (*e, i));
        }
    }

    header.setText (exists ? "CUE LIST  -  EXECUTOR " + juce::String (shownPage) + "." + juce::String (shownSlot)
                           : juce::String ("CUE LIST"), juce::dontSendNotification);
    if (! nameEdit.hasKeyboardFocus (true)) nameEdit.setText (name, false);
    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &nameEdit, &deleteExecButton, &viewport, &trackingToggle })
        c->setVisible (exists);
    trackingToggle.setToggleState (tracking, juce::dontSendNotification);
    for (auto* c : optionComponents()) c->setVisible (exists);
    chaseToggle.setToggleState (options.chase, juce::dontSendNotification);
    auto nearest = [] (float v, std::initializer_list<float> list)
    {
        int best = 0, i = 0;
        for (float x : list) { if (std::abs (x - v) < std::abs (*(list.begin() + best) - v)) best = i; ++i; }
        return best;
    };
    rateCombo.setSelectedItemIndex (nearest (options.chaseRate, { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f }), juce::dontSendNotification);
    fadeCombo.setSelectedItemIndex (nearest (options.chaseFade, { 0.0f, 0.25f, 0.5f, 1.0f }), juce::dontSendNotification);
    orderCombo.setSelectedItemIndex ((int) options.chaseMode, juce::dontSendNotification);
    for (auto* c : { &rateCombo, &fadeCombo, &orderCombo }) c->setEnabled (options.chase);
    faderCombo.setSelectedItemIndex (options.faderFunction == FaderFunction::Speed ? 1 : 0, juce::dontSendNotification);
    for (int b = 0; b < buttonCombos.size(); ++b)
        buttonCombos[b]->setSelectedItemIndex ((int) options.buttons[b], juce::dontSendNotification);
    hint.setVisible (! exists);
    hint.setText ("Tap an executor's name to show its cue list here.\nTo record: set values in the programmer, press Store, then tap an executor (or a cue here).",
                  juce::dontSendNotification);

    if ((int) cues.size() != shownCount)
    {
        rows.clear();
        for (int i = 0; i < (int) cues.size(); ++i)
            holder.addAndMakeVisible (rows.add (new CueRow (*this, i)));
        shownCount = (int) cues.size();
        layoutRows();
    }
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->update (cues[(size_t) i], i == current, running[(size_t) i]);
}

void CuesView::layoutRows()
{
    const int h = 52, w = viewport.getMaximumVisibleWidth();
    holder.setSize (w, juce::jmax (1, rows.size() * h));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * h, w, h);
}

void CuesView::paint (juce::Graphics& g) { ui::paintPanel (g, panel); }

void CuesView::resized()
{
    panel = getLocalBounds().reduced (theme::gap);
    auto r = panel.reduced (14);
    {
        auto top = r.removeFromTop (theme::buttonH);
        header.setBounds (top.removeFromLeft (300));
        deleteExecButton.setBounds (top.removeFromRight (150));
        top.removeFromRight (12);
        trackingToggle.setBounds (top.removeFromRight (100));
        top.removeFromRight (12);
        nameEdit.setBounds (top.removeFromRight (240));
        nameLabel.setBounds (top.removeFromRight (80));
    }
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (30);
        chaseToggle.setBounds (row.removeFromLeft (76));
        rateCombo.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (4);
        fadeCombo.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (4);
        orderCombo.setBounds (row.removeFromLeft (90));
        faderLabel.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (4);
        faderCombo.setBounds (row.removeFromLeft (90));
        buttonsLabel.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (4);
        const int w = juce::jmin (100, (row.getWidth() - 12) / 4);
        for (auto* cb : buttonCombos) { cb->setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); }
    }
    r.removeFromTop (10);
    viewport.setBounds (r);
    hint.setBounds (r);
    layoutRows();
}

} // namespace pl
