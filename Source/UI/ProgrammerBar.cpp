#include "ProgrammerBar.h"
#include "Control/Settings.h"
#include "ColorPicker.h"
#include "Engine/CommandLine.h"

namespace pl
{

static juce::String shortFamilyName (Family f)
{
    if (f == Family::Dimmer)   return "Dim";
    if (f == Family::Position) return "Pos";
    if (f == Family::Control)  return "Ctrl";
    return familyName (f);
}

/** Command line with history (up / down). */
class ProgrammerBar::CommandEditor : public juce::TextEditor
{
public:
    explicit CommandEditor (ProgrammerBar& o) : owner (o) {}
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
        {
            auto& h = owner.history;
            if (h.isEmpty()) return true;
            owner.historyPos = juce::jlimit (0, h.size(), owner.historyPos + (k == juce::KeyPress::upKey ? -1 : 1));
            setText (owner.historyPos < h.size() ? h[owner.historyPos] : juce::String(), false);
            moveCaretToEnd();
            return true;
        }
        if (k == juce::KeyPress::escapeKey)
        {
            if (isEmpty()) { giveAwayKeyboardFocus(); owner.clearStep(); }
            else clear();
            return true;
        }
        return juce::TextEditor::keyPressed (k);
    }
private:
    ProgrammerBar& owner;
};

ProgrammerBar::ProgrammerBar (DmxEngine& e, UiState& u) : engine (e), ui (u)
{
    commandLine = std::make_unique<CommandEditor> (*this);
    ui::styleEditor (*commandLine, false);
    commandLine->setTextToShowWhenEmpty ("Command:  1 thru 10 at 50   -   at 0 thru 100   -   group 2   -   pan at 25 thru 75   -   preset 4.1   -   highlight",
                                         theme::textDim);
    commandLine->onReturnKey = [this]
    {
        const auto c = commandLine->getText().trim();
        if (c.isEmpty()) return;
        runCommand (c);
        history.removeString (c);
        history.add (c);
        historyPos = history.size();
        commandLine->clear();
    };
    addAndMakeVisible (*commandLine);
    commandLabel.setText ("CMD", juce::dontSendNotification);
    commandLabel.setFont (theme::font (12.0f, true));
    commandLabel.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (commandLabel);

    fullButton.setTooltip ("Selected fixtures to full");
    dimOffButton.setTooltip ("Selected fixtures' dimmer to 0");
    highlightButton.setTooltip ("Highlight: the selection at full, open white, above everything (press again to stop)");
    highlightButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Dimmer));
    highlightButton.setClickingTogglesState (true);
    fullButton.onClick = [this] { full (true); };
    dimOffButton.onClick = [this] { full (false); };
    highlightButton.onClick = [this] { toggleHighlight(); };
    for (auto* b : { &fullButton, &dimOffButton, &highlightButton }) addAndMakeVisible (b);

    prevButton.setTooltip ("Prev: step back through the selection one fixture at a time");
    nextButton.setTooltip ("Next: step through the selection one fixture at a time (with Highlight / Lowlight to find and fix positions)");
    allButton.setTooltip ("All: the whole selection again after Next / Prev");
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    allButton.onClick  = [this] { stepAll(); };
    lowlightButton.setTooltip ("Lowlight: the selection (or, with Next / Prev, the rest of it) in the lowlight colour. Pick the colour with the swatch");
    lowlightButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Color));
    lowlightButton.setClickingTogglesState (true);
    lowlightButton.onClick = [this] { toggleLowlight(); };
    lowlightColourButton.setTooltip ("Lowlight colour and level (brightness)");
    lowlightColourButton.setButtonText ("LL colour");
    lowlightColourButton.onClick = [this] { openLowlightPicker(); };
    for (auto* b : { &prevButton, &nextButton, &allButton, &lowlightButton, &lowlightColourButton }) addAndMakeVisible (b);

    copyButton.setTooltip ("Copy: tap an executor, preset / group or cue, then where to copy it");
    moveButton.setTooltip ("Move: tap an executor (fader), preset / group or cue, then where to move it (swaps when taken)");
    deleteButton.setTooltip ("Delete: tap an executor, preset / group, cue, or fixtures in the Layout (asks first)");
    copyButton.onClick   = [this] { toggleEditMode (UiState::EditMode::Copy); };
    moveButton.onClick   = [this] { toggleEditMode (UiState::EditMode::Move); };
    deleteButton.onClick = [this] { toggleEditMode (UiState::EditMode::Delete); };
    for (auto* b : { &copyButton, &moveButton, &deleteButton }) addAndMakeVisible (b);
    fineButton.setTooltip ("Fine: encoders and knobs move 10x finer (also: push an encoder on the controller)");
    fineButton.setClickingTogglesState (true);
    fineButton.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Position));
    fineButton.onClick = [this] { toggleFine(); };
    addAndMakeVisible (fineButton);
    loadLowlight();

    ui::styleHeader (header, "PROGRAMMER");
    info.setFont (theme::font (12.5f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (header);
    addAndMakeVisible (info);

    familyOrder = programmerFamilies();
    for (auto f : familyOrder)
    {
        auto* b = familyButtons.add (new juce::TextButton (shortFamilyName (f)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (3001);
        b->setColour (juce::TextButton::buttonOnColourId, theme::familyColour (f));
        b->onClick = [this, f] { ui.family = f; ui.changed(); };
        addAndMakeVisible (b);
    }

    pickerButton.setTooltip ("Colour picker");
    pickerButton.setColour (juce::TextButton::buttonColourId, theme::familyColour (Family::Color).withAlpha (0.35f));
    pickerButton.onClick = [this] { openPicker(); };
    addChildComponent (pickerButton);
    for (auto& [name, colour] : colourSwatches())
    {
        auto* b = swatchButtons.add (new juce::TextButton());
        b->setColour (juce::TextButton::buttonColourId, colour);
        b->setTooltip (name);
        b->onClick = [this, c = colour] { applyColour (c); };
        addChildComponent (b);
    }

    knobViewport.setViewedComponent (&knobHolder, false);
    knobViewport.setScrollBarsShown (false, true);
    knobViewport.setScrollBarThickness (6);
    addAndMakeVisible (knobViewport);

    emptyHint.setFont (theme::font (14.0f));
    emptyHint.setColour (juce::Label::textColourId, theme::textDim);
    emptyHint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyHint);

    fanButton.setTooltip ("Fan: spread changes across the selection in selection order. Click to cycle Spread / Ramp > / Ramp < / V / Off (F)");
    fanButton.onClick = [this] { cycleFan(); };
    storeButton.setTooltip ("Store, then tap an executor or a preset (S)");
    storeButton.onClick = [this] { toggleStore(); };
    clearButton.setTooltip ("Clear selection, then programmer (Esc)");
    clearButton.onClick = [this] { clearStep(); };
    for (auto* b : { &fanButton, &storeButton, &clearButton }) addAndMakeVisible (b);
    updateFanButton();

    engine.addChangeListener (this);
    ui.addChangeListener (this);
    rebuildKnobs();
    startTimerHz (10);
}

ProgrammerBar::~ProgrammerBar()
{
    engine.removeChangeListener (this);
    ui.removeChangeListener (this);
}

void ProgrammerBar::toggleStore()
{
    ui.storeArmed = ! ui.storeArmed;
    ui.changed();
}

void ProgrammerBar::toggleEditMode (UiState::EditMode m)
{
    if (ui.editMode == m) { ui.setEditMode (UiState::EditMode::None); return; }
    ui.setEditMode (m);
    ui.say (m == UiState::EditMode::Delete ? "Delete: tap what to delete (executor, preset, group, cue, or fixtures in the Layout)"
          : m == UiState::EditMode::Copy   ? "Copy: tap what to copy"
                                           : "Move: tap what to move");
}

void ProgrammerBar::clearStep()
{
    if (ui.storeArmed) { ui.storeArmed = false; ui.changed(); return; }
    if (ui.editMode != UiState::EditMode::None) { ui.setEditMode (UiState::EditMode::None); return; }
    { const juce::ScopedLock sl (engine.dataLock); engine.programmer.clearStep(); }
    ui.changed();
}

void ProgrammerBar::full (bool on)
{
    runCommand (on ? "at full" : "at 0");
}

void ProgrammerBar::toggleHighlight()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.programmer.setHighlight (! engine.programmer.isHighlight());
    }
    ui.changed();
}

void ProgrammerBar::toggleFine()
{
    ui.fine = ! ui.fine;
    for (auto* k : knobs) k->setFine (ui.fine);
    ui.say (ui.fine ? "Fine on: encoders move 10x finer" : "Fine off");
    ui.changed();
}

void ProgrammerBar::toggleLowlight()
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        engine.programmer.setLowlight (! engine.programmer.isLowlight());
    }
    ui.changed();
}

void ProgrammerBar::step (int direction)
{
    bool ok = false;
    juce::String where;
    {
        const juce::ScopedLock sl (engine.dataLock);
        ok = engine.programmer.step (direction);
        if (ok)
        {
            const int t = engine.programmer.selection().front();
            where = targetName (t) + "  (" + juce::String (engine.programmer.stepPosition() + 1) + " of "
                  + juce::String ((int) engine.programmer.stepTargets().size()) + ")";
        }
    }
    if (onMessage) onMessage (ok ? where : juce::String ("Select 2 or more fixtures, then Next / Prev"), ! ok);
    ui.changed();
}

void ProgrammerBar::stepAll()
{
    bool ok = false;
    { const juce::ScopedLock sl (engine.dataLock); ok = engine.programmer.stepAll(); }
    if (ok) ui.changed();
}

void ProgrammerBar::loadLowlight()
{
    auto v = settings::get ("lowlight");
    const juce::ScopedLock sl (engine.dataLock);
    if (v.hasProperty ("colour")) engine.programmer.lowlightColour = (juce::uint32) (juce::int64) v["colour"];
    if (v.hasProperty ("level"))  engine.programmer.lowlightLevel = juce::jlimit (0.0f, 1.0f, (float) (double) v["level"]);
    if (v.hasProperty ("wheel"))  engine.programmer.lowlightWheel = juce::jlimit (-1.0f, 1.0f, (float) (double) v["wheel"]);
}

void ProgrammerBar::saveLowlight()
{
    auto* o = new juce::DynamicObject();
    {
        const juce::ScopedLock sl (engine.dataLock);
        o->setProperty ("colour", (juce::int64) engine.programmer.lowlightColour);
        o->setProperty ("level", engine.programmer.lowlightLevel);
        o->setProperty ("wheel", engine.programmer.lowlightWheel);
    }
    settings::set ("lowlight", juce::var (o));
}

namespace
{
    /** Lowlight colour (wheel for RGB / CMY fixtures) and the colour wheel position for
        fixtures that only have a colour wheel. */
    class LowlightPanel : public juce::Component
    {
    public:
        LowlightPanel (DmxEngine& e, std::function<void()> changed) : engine (e), onChanged (std::move (changed))
        {
            {
                const juce::ScopedLock sl (engine.dataLock);
                picker.setColour (juce::Colour (engine.programmer.lowlightColour).withBrightness (juce::jmax (0.02f, engine.programmer.lowlightLevel)));
                wheelOn.setToggleState (engine.programmer.lowlightWheel >= 0.0f, juce::dontSendNotification);
                wheel.setValue (juce::jmax (0.0f, engine.programmer.lowlightWheel) * 255.0, juce::dontSendNotification);
            }
            picker.onPick = [this] (juce::Colour c)
            {
                {
                    const juce::ScopedLock sl (engine.dataLock);
                    auto& p = engine.programmer;
                    p.lowlightLevel = c.getBrightness();
                    p.lowlightColour = (c.getBrightness() > 0.001f ? c.withBrightness (1.0f) : juce::Colour (p.lowlightColour)).getARGB();
                    p.setLowlight (true);
                }
                if (onChanged) onChanged();
            };
            addAndMakeVisible (picker);

            title.setText ("Colour wheel fixtures (no RGB / CMY)", juce::dontSendNotification);
            title.setFont (theme::font (13.0f, true));
            title.setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (title);
            wheelOn.setButtonText ("Use wheel slot");
            wheelOn.onClick = [this] { push(); };
            addAndMakeVisible (wheelOn);
            wheel.setSliderStyle (juce::Slider::LinearHorizontal);
            wheel.setRange (0.0, 255.0, 1.0);
            wheel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
            wheel.setTooltip ("Colour wheel DMX value for the lowlight (see it live on the rig)");
            wheel.onValueChange = [this] { wheelOn.setToggleState (true, juce::dontSendNotification); push(); };
            addAndMakeVisible (wheel);
            fromPreset.setButtonText ("From a Color preset...");
            fromPreset.setTooltip ("Use the colour wheel slot stored in one of your Color presets");
            fromPreset.onClick = [this] { presetMenu(); };
            addAndMakeVisible (fromPreset);
            setSize (picker.getWidth(), picker.getHeight() + 110);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            picker.setBounds (r.removeFromTop (picker.getHeight()));
            r.reduce (12, 4);
            title.setBounds (r.removeFromTop (22));
            auto row = r.removeFromTop (30);
            wheelOn.setBounds (row.removeFromLeft (130));
            wheel.setBounds (row);
            r.removeFromTop (6);
            fromPreset.setBounds (r.removeFromTop (30).removeFromLeft (200));
        }

    private:
        void push()
        {
            {
                const juce::ScopedLock sl (engine.dataLock);
                engine.programmer.lowlightWheel = wheelOn.getToggleState() ? (float) (wheel.getValue() / 255.0) : -1.0f;
                engine.programmer.setLowlight (true);
            }
            if (onChanged) onChanged();
        }

        void presetMenu()
        {
            juce::PopupMenu m;
            std::vector<float> values;
            {
                const juce::ScopedLock sl (engine.dataLock);
                for (auto& [n, p] : engine.presets.pool (PresetPool::Color))
                {
                    float v = -1.0f;
                    for (auto& [fid, attrs] : p.values)  if (auto it = attrs.find ("ColorWheel"); it != attrs.end()) { v = it->second; break; }
                    if (v < 0.0f)
                        for (auto& [t, attrs] : p.byType) if (auto it = attrs.find ("ColorWheel"); it != attrs.end()) { v = it->second; break; }
                    if (v < 0.0f) continue;
                    values.push_back (v);
                    m.addItem ((int) values.size(), juce::String (n) + "  " + (p.name.isNotEmpty() ? p.name : "Color " + juce::String (n))
                                                    + "   (DMX " + juce::String (juce::roundToInt (v * 255.0f)) + ")");
                }
            }
            if (values.empty()) m.addItem (-1, "No Color presets with a colour wheel value", false);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fromPreset),
                             [safe = juce::Component::SafePointer<LowlightPanel> (this), values] (int r)
            {
                if (safe == nullptr || r < 1 || r > (int) values.size()) return;
                safe->wheel.setValue (values[(size_t) r - 1] * 255.0);
            });
        }

        DmxEngine& engine;
        std::function<void()> onChanged;
        ColorPickerPanel picker;
        juce::Label title;
        juce::ToggleButton wheelOn;
        juce::Slider wheel;
        juce::TextButton fromPreset;
    };
}

void ProgrammerBar::openLowlightPicker()
{
    auto panel = std::make_unique<LowlightPanel> (engine, [safe = juce::Component::SafePointer<ProgrammerBar> (this)]
    {
        if (safe == nullptr) return;
        safe->saveLowlight();
        safe->ui.changed();
        safe->repaint();
    });
    juce::CallOutBox::launchAsynchronously (std::move (panel), lowlightColourButton.getScreenBounds(), nullptr);
}

void ProgrammerBar::focusCommandLine (const juce::String& text)
{
    commandLine->grabKeyboardFocus();
    if (text.isNotEmpty()) commandLine->insertTextAtCaret (text);
}

void ProgrammerBar::runCommand (const juce::String& c)
{
    command::Result r;
    {
        const juce::ScopedLock sl (engine.dataLock);
        r = command::execute (c, engine.programmer, engine.patch, engine.library, engine.presets);
    }
    if (onMessage) onMessage (r.error.isNotEmpty() ? r.error : r.message, r.error.isNotEmpty());
    if (r.selectionChanged || r.valuesChanged || r.highlightChanged) ui.changed();
}

void ProgrammerBar::cycleFan()
{
    switch (fanMode)
    {
        case FanMode::Off:      fanMode = FanMode::Spread;   break;
        case FanMode::Spread:   fanMode = FanMode::RampUp;   break;
        case FanMode::RampUp:   fanMode = FanMode::RampDown; break;
        case FanMode::RampDown: fanMode = FanMode::V;        break;
        case FanMode::V:        fanMode = FanMode::Off;      break;
    }
    updateFanButton();
}

void ProgrammerBar::updateFanButton()
{
    const bool on = fanMode != FanMode::Off;
    fanButton.setButtonText (on ? "Fan " + fanModeName (fanMode) : juce::String ("Fan"));
    fanButton.setColour (juce::TextButton::buttonColourId, on ? theme::familyColour (Family::Beam) : theme::panel2);
    fanButton.setColour (juce::TextButton::textColourOffId, on ? theme::bg : theme::text);
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setLabel ((i < 8 ? juce::String (i + 1) + "  " : juce::String())        // MIDI encoder number
                            + attributeLabel (knobAttrs[(size_t) i]) + (on ? " ~" : ""));
}

void ProgrammerBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildKnobs();
}

void ProgrammerBar::timerCallback()
{
    ++tick;
    const bool blink = (tick / 3) % 2 == 0;
    storeButton.setColour (juce::TextButton::buttonColourId,
                           ui.storeArmed ? (blink ? theme::danger : theme::danger.darker (0.5f)) : theme::panel2);
    auto editColour = [&] (juce::TextButton& b, UiState::EditMode m, juce::Colour c)
    {
        const auto want = ui.editMode == m ? (blink ? c : c.darker (0.5f)) : theme::panel2;
        if (b.findColour (juce::TextButton::buttonColourId) != want) b.setColour (juce::TextButton::buttonColourId, want);
    };
    editColour (copyButton, UiState::EditMode::Copy, theme::familyColour (Family::Position));
    editColour (moveButton, UiState::EditMode::Move, theme::familyColour (Family::Position));
    editColour (deleteButton, UiState::EditMode::Delete, theme::danger);
    syncKnobs();   // live values: playback, effects, presets
    bool hl = false;
    { const juce::ScopedLock sl (engine.dataLock); hl = engine.programmer.isHighlight(); }
    if (hl != highlightButton.getToggleState()) highlightButton.setToggleState (hl, juce::dontSendNotification);
    bool ll = false; bool stepping = false; juce::Colour llc;
    {
        const juce::ScopedLock sl (engine.dataLock);
        ll = engine.programmer.isLowlight();
        stepping = engine.programmer.isStepping();
        llc = juce::Colour (engine.programmer.lowlightColour).withBrightness (0.35f + 0.65f * engine.programmer.lowlightLevel);
    }
    if (ll != lowlightButton.getToggleState()) lowlightButton.setToggleState (ll, juce::dontSendNotification);
    if (ui.fine != fineButton.getToggleState()) fineButton.setToggleState (ui.fine, juce::dontSendNotification);
    if (ui.fine != shownFine) { shownFine = ui.fine; for (auto* k : knobs) k->setFine (ui.fine); }
    if (llc != lowlightColourButton.findColour (juce::TextButton::buttonColourId))
        lowlightColourButton.setColour (juce::TextButton::buttonColourId, llc);
    allButton.setEnabled (stepping);
    if (tick % 5 == 0) updateInfo();
}

//==============================================================================
void ProgrammerBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme::line);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void ProgrammerBar::resized()
{
    auto r = getLocalBounds().reduced (theme::gap, 6);

    {
        auto cmd = r.removeFromTop (28);
        commandLabel.setBounds (cmd.removeFromLeft (40));
        commandLine->setBounds (cmd);
        r.removeFromTop (6);
    }

    auto left = r.removeFromLeft (250);
    {
        auto top = left.removeFromTop (20);
        header.setBounds (top.removeFromLeft (100));
        info.setBounds (top);
    }
    left.removeFromTop (6);
    const int rowH = (left.getHeight() - 6) / 2;
    const int perRow = 4;
    const int bw = (left.getWidth() - 4 * (perRow - 1)) / perRow;
    for (int i = 0; i < familyButtons.size(); ++i)
    {
        const int row = i / perRow, col = i % perRow;
        familyButtons[i]->setBounds (left.getX() + col * (bw + 4), left.getY() + row * (rowH + 6), bw, rowH);
    }
    {
        const int i = familyButtons.size();          // the free slot after the feature groups
        fineButton.setBounds (left.getX() + (i % perRow) * (bw + 4), left.getY() + (i / perRow) * (rowH + 6), bw, rowH);
    }
    r.removeFromLeft (theme::gap);

    auto right = r.removeFromRight (juce::jlimit (460, 640, r.getWidth() / 2));
    {
        // 8 columns. Row A: Prev Next All Full DimOff Highlight Lowlight LL-colour
        //            Row B: Copy Move Delete Fan [ Store x2 ] [ Clear x2 ]
        auto rowA = right.removeFromTop ((right.getHeight() - 6) / 2);
        right.removeFromTop (6);
        auto rowB = right;
        const int gap = 5, w = (rowA.getWidth() - 7 * gap) / 8;
        for (auto* b : { &prevButton, &nextButton, &allButton, &fullButton, &dimOffButton, &highlightButton, &lowlightButton, &lowlightColourButton })
            { b->setBounds (rowA.removeFromLeft (w)); rowA.removeFromLeft (gap); }
        for (auto* b : { &copyButton, &moveButton, &deleteButton, &fanButton })
            { b->setBounds (rowB.removeFromLeft (w)); rowB.removeFromLeft (gap); }
        storeButton.setBounds (rowB.removeFromLeft (2 * w + gap));
        rowB.removeFromLeft (gap);
        clearButton.setBounds (rowB.removeFromLeft (2 * w + gap));
    }
    r.removeFromRight (theme::gap);

    const bool colourTools = ui.family == Family::Color;
    pickerButton.setVisible (colourTools);
    for (auto* b : swatchButtons) b->setVisible (colourTools);
    if (colourTools)
    {
        auto tools = r.removeFromLeft (300);
        pickerButton.setBounds (tools.removeFromLeft (70).reduced (0, 2));
        tools.removeFromLeft (6);
        const int cols = 7, gap = 4;
        const int w = (tools.getWidth() - gap * (cols - 1)) / cols;
        const int h = (tools.getHeight() - gap) / 2;
        for (int i = 0; i < swatchButtons.size(); ++i)
            swatchButtons[i]->setBounds (tools.getX() + (i % cols) * (w + gap), tools.getY() + (i / cols) * (h + gap), w, h);
        r.removeFromLeft (theme::gap);
    }

    knobViewport.setBounds (r);
    emptyHint.setBounds (r);
    layoutKnobs();
}

//==============================================================================
void ProgrammerBar::rebuildKnobs()
{
    for (int i = 0; i < familyButtons.size(); ++i)
        familyButtons[i]->setToggleState (familyOrder[(size_t) i] == ui.family, juce::dontSendNotification);

    std::vector<juce::String> attrs;
    std::map<juce::String, float> defaults;
    bool nothingSelected = false;
    {
        const juce::ScopedLock sl (engine.dataLock);
        nothingSelected = engine.programmer.selection().empty();
        for (int fid : engine.programmer.selection())
            if (auto* pf = engine.patch.find (fid))
                if (auto* type = engine.library.find (pf->typeId))
                    for (auto& s : type->attributeSlots())
                    {
                        if (isCellTarget (fid) && cellOf (s.attribute) != targetCell (fid))
                            continue;                                    // a pixel: only its own attributes
                        const auto base = baseAttribute (s.attribute);   // one knob for all cells
                        if (familyOf (base) == ui.family
                            && std::find (attrs.begin(), attrs.end(), base) == attrs.end())
                        {
                            attrs.push_back (base);
                            defaults[base] = s.defaultNormalised;
                        }
                    }
    }

    if (attrs != knobAttrs)
    {
        knobs.clear();
        knobAttrs = attrs;
        for (auto& a : attrs)
        {
            auto* k = knobs.add (new Knob (attributeLabel (a), theme::familyColour (ui.family), true));
            k->setRange (0.0, 1.0, 0.0, 1.0);
            k->setFine (ui.fine);
            k->setDefaultMarker (defaults[a]);
            k->onChange = [this, a] (double v) { setAttribute (a, (float) v); };
            k->onKnockout = [this, a] { knockoutAttribute (a); };
            k->onTypeValue = [this, a, k]
            {
                ui::askForText (k, attributeLabel (a), "Value in % - one value, or a spread like 0 thru 100", {},
                                [safe = juce::Component::SafePointer<ProgrammerBar> (this), a] (const juce::String& v)
                {
                    if (safe != nullptr && v.isNotEmpty()) safe->runCommand (a + " at " + v);
                });
            };
            knobHolder.addAndMakeVisible (k);
        }
        updateFanButton();
    }
    syncKnobs();

    emptyHint.setText (nothingSelected ? "Select fixtures in the Layout view, or recall a group"
                                       : "No " + familyName (ui.family) + " attributes on the selected fixtures",
                       juce::dontSendNotification);
    emptyHint.setVisible (knobs.isEmpty());
    updateInfo();
    resized();
}

void ProgrammerBar::layoutKnobs()
{
    const int w = 86;
    const int h = knobViewport.getMaximumVisibleHeight();
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (i * w, 0, w - 4, h);
    knobHolder.setSize (juce::jmax (1, knobs.size() * w), juce::jmax (1, h));
}

void ProgrammerBar::syncKnobs()
{
    for (int i = 0; i < knobs.size(); ++i)
    {
        if (knobs[i]->isBeingDragged()) continue;
        auto& attr = knobAttrs[(size_t) i];
        auto [v, active] = currentValue (attr);
        knobs[i]->setValue (v, active);
        lastKnobValue[attr] = v;

        // show the range when the selection holds different values (fans, gradients)
        float lo = 1.0f, hi = 0.0f;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (int fid : selectedWith (attr))
            {
                const float x = valueOf (fid, attr, 0.0f);
                lo = juce::jmin (lo, x); hi = juce::jmax (hi, x);
            }
        }
        knobs[i]->setValueText (hi - lo > 0.005f ? juce::String (juce::roundToInt (lo * 100.0f)) + "-"
                                                    + juce::String (juce::roundToInt (hi * 100.0f)) + "%"
                                                 : juce::String());
    }
}

void ProgrammerBar::updateInfo()
{
    size_t n = 0, values = 0, fx = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        n = engine.programmer.selection().size();
        values = engine.programmer.allValues().size();
        fx = engine.programmer.effects().size();
    }
    int pixels = 0;
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int t : engine.programmer.selection()) pixels += isCellTarget (t) ? 1 : 0;
    }
    juce::String t = pixels > 0 ? juce::String ((int) n) + " selected (" + juce::String (pixels) + " px)"
                                : juce::String ((int) n) + " selected";
    if (values > 0 || fx > 0)
        t << "  -  " << (int) values << " in prog" << (fx > 0 ? ", " + juce::String ((int) fx) + " fx" : juce::String());
    info.setText (t, juce::dontSendNotification);
}

//==============================================================================
std::vector<int> ProgrammerBar::selectedWith (const juce::String& attr) const
{
    std::vector<int> out;
    for (int fid : engine.programmer.selection())
        if (! keysFor (fid, attr).empty()) out.push_back (fid);
    return out;
}

std::vector<juce::String> ProgrammerBar::keysFor (int fid, const juce::String& base) const
{
    std::vector<juce::String> keys;
    const int cell = targetCell (fid);                  // a pixel, or 0 for the whole fixture
    if (auto* pf = engine.patch.find (fid))
        if (auto* type = engine.library.find (pf->typeId))
            for (auto& s : type->attributeSlots())
                if (baseAttribute (s.attribute) == base && (cell == 0 || cellOf (s.attribute) == cell))
                    keys.push_back (s.attribute);
    // the whole fixture's Dimmer knob is its master dimmer, not every pixel dimmer
    if (cell == 0 && base == "Dimmer" && std::find (keys.begin(), keys.end(), juce::String ("Dimmer")) != keys.end())
        keys = { "Dimmer" };
    return keys;
}

void ProgrammerBar::setAll (int fid, const juce::String& base, float v)
{
    for (auto& k : keysFor (fid, base))
        engine.programmer.set (fid, k, v);
}

float ProgrammerBar::valueOf (int fid, const juce::String& attr, float fallback) const
{
    // the attribute itself, or its first cell on pixel fixtures
    auto keys = keysFor (fid, attr);
    const juce::String key = keys.empty() ? attr : keys.front();
    if (auto v = engine.programmer.get (fid, key))
        return *v;
    auto live = engine.getFixtureOutput (fid);
    auto it = live.find (key);
    return it != live.end() ? it->second : fallback;
}

void ProgrammerBar::setAttribute (const juce::String& attr, float v)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        const auto fids = selectedWith (attr);
        const int n = (int) fids.size();

        if (fanMode == FanMode::Off)
        {
            for (int fid : fids)
                setAll (fid, attr, v);
        }
        else
        {
            // spread the change: each fixture moves by delta x its fan weight
            const float delta = v - (float) lastKnobValue[attr];
            for (int i = 0; i < n; ++i)
            {
                const float base = valueOf (fids[(size_t) i], attr, 0.0f);
                setAll (fids[(size_t) i], attr, base + delta * fanWeight (fanMode, i, n));
            }
        }
    }
    lastKnobValue[attr] = v;
    for (int i = 0; i < knobs.size(); ++i)
        if (knobAttrs[(size_t) i] == attr) knobs[i]->setActive (true);
    updateInfo();
}

float ProgrammerBar::encoderValue (int encoder) const
{
    const int i = encoder - 1;
    if (i < 0 || i >= (int) knobAttrs.size()) return -1.0f;
    auto it = lastKnobValue.find (knobAttrs[(size_t) i]);
    return it != lastKnobValue.end() ? (float) it->second : -1.0f;
}

void ProgrammerBar::encoderInput (int encoder, float delta, bool absolute, float value)
{
    const int i = encoder - 1;
    if (i >= (int) knobAttrs.size()) rebuildKnobs();      // selection may have changed this instant
    if (i < 0 || i >= (int) knobAttrs.size()) return;
    const auto attr = knobAttrs[(size_t) i];
    const float current = (float) lastKnobValue[attr];
    const float target = juce::jlimit (0.0f, 1.0f, absolute ? value : current + delta * (ui.fine ? UiState::fineFactor : 1.0f));
    setAttribute (attr, target);
    knobs[i]->setValue (target, true);
}

void ProgrammerBar::knockoutAttribute (const juce::String& attr)
{
    {
        const juce::ScopedLock sl (engine.dataLock);
        for (int fid : engine.programmer.selection())
            for (auto& k : keysFor (fid, attr))
                engine.programmer.clearAttribute (fid, k);
    }
    syncKnobs();
    updateInfo();
}

std::pair<float, bool> ProgrammerBar::currentValue (const juce::String& attr) const
{
    const juce::ScopedLock sl (engine.dataLock);
    for (int fid : selectedWith (attr))
    {
        auto keys = keysFor (fid, attr);
        if (auto v = engine.programmer.get (fid, keys.empty() ? attr : keys.front()))
            return { *v, true };
        return { valueOf (fid, attr, 0.0f), false };
    }
    return { 0.0f, false };
}

//==============================================================================
juce::Colour ProgrammerBar::currentColour (int fid) const
{
    const float w = valueOf (fid, "ColorW", 0.0f);
    float r = valueOf (fid, "ColorR", -1.0f), g = valueOf (fid, "ColorG", -1.0f), b = valueOf (fid, "ColorB", -1.0f);
    if (r < 0 && g < 0 && b < 0)
    {
        r = 1.0f - valueOf (fid, "ColorC", 0.0f);
        g = 1.0f - valueOf (fid, "ColorM", 0.0f);
        b = 1.0f - valueOf (fid, "ColorY", 0.0f);
    }
    auto c = [w] (float x) { return juce::jlimit (0.0f, 1.0f, juce::jmax (0.0f, x) + w); };
    return juce::Colour::fromFloatRGBA (c (r), c (g), c (b), 1.0f);
}

void ProgrammerBar::applyColour (juce::Colour target)
{
    {
        const juce::ScopedLock sl (engine.dataLock);

        // fixtures that can make colour, in selection order
        std::vector<int> fids;
        for (int fid : engine.programmer.selection())
            if (auto* pf = engine.patch.find (fid))
                if (auto* type = engine.library.find (pf->typeId))
                    for (auto& a : type->attributes)
                        if ((a.attribute == "ColorR" || a.attribute == "ColorC")
                            && (! isCellTarget (fid) || a.cell == targetCell (fid))) { fids.push_back (fid); break; }

        const int n = (int) fids.size();
        // the gradient starts from the fixture sitting at position 0 (it keeps its colour)
        int anchor = 0;
        if (fanMode == FanMode::RampDown) anchor = n - 1;
        else if (fanMode == FanMode::V)   anchor = n / 2;
        const juce::Colour start = n > 0 ? currentColour (fids[(size_t) anchor]) : target;

        for (int i = 0; i < n; ++i)
        {
            const int fid = fids[(size_t) i];
            auto c = fanMode == FanMode::Off ? target
                                             : start.interpolatedWith (target, fanGradientPosition (fanMode, i, n));
            float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();

            auto* type = engine.library.find (engine.patch.find (fid)->typeId);
            auto has = [type] (const char* a) { for (auto& x : type->attributes) if (x.attribute == a) return true; return false; };

            if (has ("ColorW"))
            {
                const float w = juce::jmin (r, g, b);           // pull the common part into the white LED
                r -= w; g -= w; b -= w;
                setAll (fid, "ColorW", w);
            }
            if (has ("ColorR")) setAll (fid, "ColorR", r);
            if (has ("ColorG")) setAll (fid, "ColorG", g);
            if (has ("ColorB")) setAll (fid, "ColorB", b);
            if (has ("ColorC")) setAll (fid, "ColorC", 1.0f - c.getFloatRed());
            if (has ("ColorM")) setAll (fid, "ColorM", 1.0f - c.getFloatGreen());
            if (has ("ColorY")) setAll (fid, "ColorY", 1.0f - c.getFloatBlue());
            for (auto* other : { "ColorA", "ColorUV", "ColorLime" })
            {
                auto keys = keysFor (fid, other);
                if (has (other) && ! keys.empty() && ! engine.programmer.get (fid, keys.front()).has_value())
                    setAll (fid, other, 0.0f);
            }
        }
    }
    syncKnobs();
    updateInfo();
}

void ProgrammerBar::openPicker()
{
    auto panel = std::make_unique<ColorPickerPanel>();
    {
        const juce::ScopedLock sl (engine.dataLock);
        auto& sel = engine.programmer.selection();
        if (! sel.empty()) panel->setColour (currentColour (sel.front()));
    }
    panel->onPick = [safe = juce::Component::SafePointer<ProgrammerBar> (this)] (juce::Colour c)
    {
        if (safe != nullptr) safe->applyColour (c);
    };
    panel->getFixtureColours = [safe = juce::Component::SafePointer<ProgrammerBar> (this)]
    {
        std::vector<juce::Colour> out;
        if (safe == nullptr) return out;
        const juce::ScopedLock sl (safe->engine.dataLock);
        for (int fid : safe->engine.programmer.selection())
            if (auto* pf = safe->engine.patch.find (fid))
                if (auto* type = safe->engine.library.find (pf->typeId))
                    for (auto& a : type->attributes)
                        if ((a.attribute == "ColorR" || a.attribute == "ColorC")
                            && (! isCellTarget (fid) || a.cell == targetCell (fid))) { out.push_back (safe->currentColour (fid)); break; }
        return out;
    };
    juce::CallOutBox::launchAsynchronously (std::move (panel), pickerButton.getScreenBounds(), nullptr);
}

} // namespace pl
