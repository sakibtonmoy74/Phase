#include "MidiView.h"
#include "EditOps.h"

namespace pl
{

class MidiView::LearnCell : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    LearnCell (MidiControl& m, MidiTarget t, int i) : midi (m), target (t), index (i)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip (midiTargetName (t, i) + ": click to learn, right-click to clear");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool learning = midi.isLearning (target, index);
        auto* b = midi.bindingFor (target, index);
        auto bg = learning ? (blinkOn ? theme::danger : theme::danger.darker (0.6f))
                           : b != nullptr ? theme::ok.withAlpha (0.18f) : theme::bg;
        if (isMouseOver() && ! learning) bg = bg.brighter (0.1f);
        g.setColour (bg);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (b != nullptr ? theme::ok.withAlpha (0.6f) : theme::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (learning ? theme::text : b != nullptr ? theme::text : theme::textDim.withAlpha (0.6f));
        g.setFont (theme::font (12.0f, b != nullptr));
        g.drawFittedText (learning ? juce::String ("Move a control...") : b != nullptr ? b->describe() : juce::String ("Learn"),
                          getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.8f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (e.mods.isPopupMenu())                 midi.clearBinding (target, index);
        else if (midi.isLearning (target, index)) midi.cancelLearn();
        else                                      midi.startLearn (target, index);
    }

    bool blinkOn = false;

private:
    MidiControl& midi;
    const MidiTarget target;
    const int index;
};

//==============================================================================
MidiView::MidiView (MidiControl& m) : midi (m)
{
    ui::styleHeader (devicesHeader, "MIDI INPUTS");
    ui::styleField (outputLabel, "Feedback output (motor faders, LEDs)");
    ui::styleField (lastLabel, "Last message");
    ui::styleField (monitorLabel, "MIDI monitor (what your controller sends)");
    monitor.setMultiLine (true);
    monitor.setReadOnly (true);
    monitor.setCaretVisible (false);
    monitor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    monitor.setColour (juce::TextEditor::backgroundColourId, theme::bg);
    monitor.setColour (juce::TextEditor::textColourId, theme::text);
    monitor.setColour (juce::TextEditor::outlineColourId, theme::line);
    addAndMakeVisible (monitorLabel);
    addAndMakeVisible (monitor);
    lastMessage.setFont (theme::font (13.0f, true));
    lastMessage.setColour (juce::Label::textColourId, theme::accent);
    hint.setFont (theme::font (12.5f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setText ("Click a cell, then move a fader / press a button / turn an encoder on your controller. "
                  "Right-click a cell to clear it.\n\nExecutor mappings always control the page on screen, "
                  "and motorized faders move to the new page's levels.\n\nSave your own maps with MIDI maps... > Save current map as.\n\nEncoders 1-8 control the attributes shown in the "
                  "programmer (numbered on the knobs), so they follow the selected feature group. Effects knobs 1-8 "
                  "turn the knobs of the Effects panel, left to right.\n\nX-Touch Compact: Load a controller preset.",
                  juce::dontSendNotification);
    refreshButton.onClick = [this] { refreshDevices(); };
    presetButton.setTooltip ("Controller presets, and your own saved maps: save, load, export, import");
    presetButton.onClick = [this]
    {
        juce::PopupMenu menu, builtIn, saved, remove;
        const auto names = MidiControl::presetNames();
        for (int i = 0; i < names.size(); ++i) builtIn.addItem (1 + i, names[i]);
        const auto maps = MidiControl::savedMaps();
        for (int i = 0; i < maps.size(); ++i)
        {
            saved.addItem (100 + i, maps[i].getFileNameWithoutExtension());
            remove.addItem (300 + i, maps[i].getFileNameWithoutExtension());
        }
        menu.addSubMenu ("Controller presets", builtIn);
        menu.addSubMenu ("Load saved map", saved, maps.size() > 0);
        menu.addItem (1001, "Save current map as...");
        menu.addSeparator();
        menu.addItem (1002, "Export map to a file...");
        menu.addItem (1003, "Import map from a file...");
        menu.addSubMenu ("Delete saved map", remove, maps.size() > 0);
        menu.addSeparator();
        menu.addItem (1000, "Clear all mappings");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                         [safe = juce::Component::SafePointer<MidiView> (this), names, maps] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& self = *safe;
            auto say = [&self] (const juce::String& text) { self.lastMessage.setText (text, juce::dontSendNotification); };
            if (r == 1000) { self.midi.clearAllBindings(); say ("All mappings cleared"); return; }
            if (r >= 1 && r <= names.size())
            {
                const auto result = self.midi.loadPreset (names[r - 1]);
                self.refreshDevices();
                self.hint.setText (result + "\n\n" + self.hint.getText(), juce::dontSendNotification);
                return;
            }
            if (r >= 100 && r < 100 + maps.size())
            {
                const auto err = self.midi.loadMap (maps[r - 100]);
                say (err.isNotEmpty() ? err : "Loaded map \"" + maps[r - 100].getFileNameWithoutExtension() + "\"");
                return;
            }
            if (r >= 300 && r < 300 + maps.size())
            {
                const auto f = maps[r - 300];
                editops::confirmDelete (safe, "the saved MIDI map \"" + f.getFileNameWithoutExtension() + "\"",
                                        [f, safe] { f.deleteFile(); if (safe != nullptr) safe->lastMessage.setText ("Map deleted", juce::dontSendNotification); }, false);
                return;
            }
            if (r == 1001)
            {
                ui::askForText (safe, "Save MIDI map", "Name for this map (it appears under Load saved map)", {}, [safe] (const juce::String& name)
                {
                    if (safe == nullptr || name.isEmpty()) return;
                    const auto f = MidiControl::mapsFolder().getChildFile (juce::File::createLegalFileName (name) + ".phasemidi");
                    const auto err = safe->midi.saveMap (f);
                    safe->lastMessage.setText (err.isNotEmpty() ? err : "Saved map \"" + name + "\"", juce::dontSendNotification);
                });
                return;
            }
            if (r == 1002 || r == 1003)
            {
                const bool save = r == 1002;
                self.chooser = std::make_unique<juce::FileChooser> (save ? "Export MIDI map" : "Import MIDI map",
                                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.phasemidi");
                const int chooserFlags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                        : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles;
                self.chooser->launchAsync (chooserFlags, [safe, save] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr || fc.getResult() == juce::File()) return;
                    auto f = fc.getResult();
                    if (save && ! f.hasFileExtension ("phasemidi")) f = f.withFileExtension ("phasemidi");
                    const auto err = save ? safe->midi.saveMap (f) : safe->midi.loadMap (f);
                    if (err.isEmpty() && ! save) safe->midi.saveMap (MidiControl::mapsFolder().getChildFile (f.getFileName()));   // keep a copy
                    safe->lastMessage.setText (err.isNotEmpty() ? err : (save ? "Exported " : "Imported ") + f.getFileName(), juce::dontSendNotification);
                });
            }
        });
    };
    outputCombo.onChange = [this]
    {
        const int i = outputCombo.getSelectedItemIndex();
        midi.setOutput (i > 0 && i - 1 < outputIds.size() ? outputIds[i - 1] : juce::String());
    };
    for (auto* c : std::initializer_list<juce::Component*> { &devicesHeader, &outputLabel, &outputCombo, &refreshButton, &presetButton,
                                                            &lastLabel, &lastMessage, &hint })
        addAndMakeVisible (c);

    auto addLabel = [this] (const juce::String& t, bool header)
    {
        auto* l = labels.add (new juce::Label());
        if (header) ui::styleHeader (*l, t);
        else
        {
            l->setText (t, juce::dontSendNotification);
            l->setFont (theme::font (13.5f));
            l->setColour (juce::Label::textColourId, theme::text);
        }
        holder.addAndMakeVisible (l);
        return l;
    };
    auto addCell = [this] (MidiTarget t, int i) { holder.addAndMakeVisible (cells.add (new LearnCell (midi, t, i))); };

    addLabel ("EXECUTORS (CURRENT PAGE)", true);
    for (auto* t : { "Fader", "Button 1", "Button 2", "Button 3", "Button 4" }) addLabel (t, false);
    for (int s = 1; s <= Playback::executorsPerPage; ++s)
    {
        addLabel ("Exec " + juce::String (s), false);
        for (auto t : { MidiTarget::ExecFader, MidiTarget::ExecGo, MidiTarget::ExecBack, MidiTarget::ExecOff, MidiTarget::ExecFlash })
            addCell (t, s);
    }

    auto addModeRow = [this, &addLabel, &addCell] (MidiTarget t, int i, const juce::String& name)
    {
        addLabel (name, false);
        addCell (t, i);
        auto* mode = encoderModes.add (new juce::ComboBox());
        mode->addItem ("Absolute", 1);
        mode->addItem ("Relative (1 / 127)", 2);
        mode->addItem ("Relative (64 = 0)", 3);
        mode->addItem ("Absolute as endless", 4);
        mode->addItem ("Mackie V-Pot (sign + ticks)", 5);
        mode->setSelectedId (t == MidiTarget::EffectKnob ? 4 : 1, juce::dontSendNotification);
        mode->setTooltip ("How this control sends values: absolute knob, endless encoder (relative), "
                          "or an absolute knob used like an endless one (X-Touch)");
        mode->onChange = [this, t, i, mode] { midi.setEncoderMode (t, i, (MidiBinding::EncoderMode) (mode->getSelectedId() - 1)); };
        holder.addAndMakeVisible (mode);
        modeTargets.push_back ({ t, i });
    };
    addLabel ("ENCODERS (PROGRAMMER)", true);
    for (int e = 1; e <= 8; ++e) addModeRow (MidiTarget::Encoder, e, "Encoder " + juce::String (e));
    addLabel ("EFFECTS PANEL KNOBS", true);
    for (int e = 1; e <= 16; ++e) addModeRow (MidiTarget::EffectKnob, e, "Effects knob " + juce::String (e));

    addLabel ("MASTERS", true);
    for (auto t : { MidiTarget::Grand, MidiTarget::DimSpeed, MidiTarget::MoveSpeed, MidiTarget::Size })
        addModeRow (t, 0, midiTargetName (t, 0));
    addLabel ("KEYS", true);
    for (auto t : { MidiTarget::Tap, MidiTarget::Blackout, MidiTarget::PageUp, MidiTarget::PageDown,
                    MidiTarget::Store, MidiTarget::Clear, MidiTarget::Fan,
                    MidiTarget::Next, MidiTarget::Prev, MidiTarget::StepAll, MidiTarget::Highlight, MidiTarget::Lowlight,
                    MidiTarget::Fine })
    {
        addLabel (midiTargetName (t, 0), false);
        addCell (t, 0);
    }
    for (int layer = 0; layer < 3; ++layer)
    {
        addLabel (midiTargetName (MidiTarget::KnobLayer, layer), false);
        addCell (MidiTarget::KnobLayer, layer);
    }

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    midi.addChangeListener (this);
    refreshDevices();
}

MidiView::~MidiView() { midi.removeChangeListener (this); }

void MidiView::visibilityChanged()
{
    if (isVisible()) { refreshDevices(); startTimerHz (5); }
    else             { stopTimer(); if (midi.isLearning()) midi.cancelLearn(); }
}

void MidiView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (int e = 0; e < encoderModes.size() && e < (int) modeTargets.size(); ++e)
        if (auto* b = midi.bindingFor (modeTargets[(size_t) e].first, modeTargets[(size_t) e].second))
            encoderModes[e]->setSelectedId ((int) b->encoderMode + 1, juce::dontSendNotification);
    for (auto* c : cells) c->repaint();
}

void MidiView::timerCallback()
{
    blink = ! blink;
    for (auto* c : cells) { c->blinkOn = blink; if (midi.isLearning()) c->repaint(); }
    lastMessage.setText (midi.getLastMessage().isNotEmpty() ? midi.getLastMessage() : juce::String ("-"),
                         juce::dontSendNotification);
    const auto rec = midi.getRecentMessages();
    if (rec != shownRecent) { shownRecent = rec; monitor.setText (rec.joinIntoString ("\n"), false); }
}

void MidiView::refreshDevices()
{
    for (auto* t : inputToggles) removeChildComponent (t);
    inputToggles.clear();
    inputIds.clear();
    for (auto& info : midi.inputs())
    {
        auto* t = inputToggles.add (new juce::ToggleButton (info.name));
        t->setToggleState (midi.isInputEnabled (info.identifier), juce::dontSendNotification);
        const auto id = info.identifier;
        t->onClick = [this, t, id] { midi.setInputEnabled (id, t->getToggleState()); };
        inputIds.add (id);
        addAndMakeVisible (t);
    }

    outputCombo.clear (juce::dontSendNotification);
    outputIds.clear();
    outputCombo.addItem ("None", 1);
    int selected = 1;
    for (auto& info : midi.outputs())
    {
        outputIds.add (info.identifier);
        outputCombo.addItem (info.name, outputIds.size() + 1);
        if (info.identifier == midi.getOutputId()) selected = outputIds.size() + 1;
    }
    outputCombo.setSelectedId (selected, juce::dontSendNotification);
    changeListenerCallback (nullptr);
    resized();
}

void MidiView::paint (juce::Graphics& g)
{
    ui::paintPanel (g, leftPanel);
    ui::paintPanel (g, rightPanel);
    if (inputToggles.isEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText ("No MIDI inputs found. Connect a controller and press Refresh.",
                    leftPanel.reduced (14).withTrimmedTop (30).withHeight (40), juce::Justification::topLeft);
    }
}

void MidiView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);
    leftPanel = r.removeFromLeft (330);
    r.removeFromLeft (theme::gap);
    rightPanel = r;

    auto l = leftPanel.reduced (14);
    devicesHeader.setBounds (l.removeFromTop (20));
    l.removeFromTop (6);
    if (inputToggles.isEmpty()) l.removeFromTop (44);
    for (auto* t : inputToggles) t->setBounds (l.removeFromTop (30));
    l.removeFromTop (10);
    outputLabel.setBounds (l.removeFromTop (20));
    outputCombo.setBounds (l.removeFromTop (34));
    l.removeFromTop (8);
    refreshButton.setBounds (l.removeFromTop (34));
    l.removeFromTop (6);
    presetButton.setBounds (l.removeFromTop (34));
    l.removeFromTop (12);
    lastLabel.setBounds (l.removeFromTop (20));
    lastMessage.setBounds (l.removeFromTop (22));
    l.removeFromTop (8);
    monitorLabel.setBounds (l.removeFromTop (20));
    monitor.setBounds (l.removeFromTop (juce::jmin (210, l.getHeight() / 2)));
    l.removeFromTop (8);
    hint.setBounds (l);

    viewport.setBounds (rightPanel.reduced (10));
    const int w = viewport.getMaximumVisibleWidth();

    // left column: executors grid; right column: encoders, masters, keys
    const int leftW = juce::jmin (620, w * 6 / 10);
    const int rowH = 32, labelW = 76;
    int li = 0, ci = 0, mi = 0;
    int y = 0;

    labels[li++]->setBounds (0, y, leftW, 22);                    // EXECUTORS header
    y += 24;
    const int cellW = (leftW - labelW) / 5;
    for (int c = 0; c < 5; ++c)
    {
        labels[li]->setJustificationType (juce::Justification::centred);
        labels[li]->setColour (juce::Label::textColourId, theme::textDim);
        labels[li++]->setBounds (labelW + c * cellW, y, cellW, 20);
    }
    y += 22;
    for (int s = 0; s < Playback::executorsPerPage; ++s)
    {
        labels[li++]->setBounds (0, y, labelW, rowH);
        for (int c = 0; c < 5; ++c)
            cells[ci++]->setBounds (labelW + c * cellW, y, cellW, rowH);
        y += rowH;
    }
    const int leftBottom = y;

    const int x0 = leftW + 24, colW = w - x0;
    const int nameW = 140, cellW2 = juce::jmin (170, colW - nameW - 8);
    y = 0;
    auto section = [&] (int rows, bool withMode)
    {
        labels[li++]->setBounds (x0, y, colW, 22);
        y += 24;
        for (int i = 0; i < rows; ++i)
        {
            labels[li++]->setBounds (x0, y, nameW, rowH);
            cells[ci++]->setBounds (x0 + nameW, y, cellW2, rowH);
            if (withMode)
                encoderModes[mi++]->setBounds (x0 + nameW + cellW2 + 6, y + 2, juce::jmax (60, colW - nameW - cellW2 - 6), rowH - 4);
            y += rowH;
        }
        y += 10;
    };
    section (8, true);
    section (8, true);
    section (4, true);
    section (7, false);

    holder.setSize (w, juce::jmax (leftBottom, y) + 10);
}

} // namespace pl
