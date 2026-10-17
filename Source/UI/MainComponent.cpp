#include "MainComponent.h"
#include "LayoutView.h"
#include "CuesView.h"
#include "PatchView.h"
#include "PresetsView.h"
#include "DmxMonitorView.h"
#include "NetworkView.h"
#include "DjLinkView.h"
#include "EffectsPanel.h"
#include "ProgrammerBar.h"
#include "ExecutorBar.h"
#include "MidiView.h"
#include "AudioView.h"
#include "Control/MidiControl.h"
#include "Control/AudioInput.h"
#include "Control/NdiInput.h"
#include "Control/Settings.h"
#include "ScreenWindow.h"
#include "Visualizer3DView.h"
#include "Engine/ShowFile.h"
#include <BinaryData.h>

namespace pl
{

MainComponent::MainComponent (DmxEngine& e) : engine (e)
{
    audioInput    = std::make_unique<AudioInput> (engine);
    midi          = std::make_unique<MidiControl> (engine, ui);
    djLink        = std::make_unique<ProDjLink>();
    DjLinkView::restoreSettings (*djLink);
    ndi           = std::make_unique<NdiInput> (engine.video);
    ui.videoSources = [this]
    {
        auto s = pixelmap::testSources();
        s.addArray (ndi->getSources());
        return s;
    };
    ui.ndiStatus = [this] { return ndi->getStatus(); };
    layoutView    = std::make_unique<LayoutView> (engine, ui);
    view3D        = std::make_unique<Visualizer3DView> (engine, ui);
    cuesView      = std::make_unique<CuesView> (engine, ui);
    patchView     = std::make_unique<PatchView> (engine, ui);
    presetsView   = std::make_unique<PresetsView> (engine, ui);
    monitorView   = std::make_unique<DmxMonitorView> (engine);
    networkView   = std::make_unique<NetworkView> (engine);
    djLinkView    = std::make_unique<DjLinkView> (*djLink, engine);
    effectsPanel  = std::make_unique<EffectsPanel> (engine, ui);
    programmerBar = std::make_unique<ProgrammerBar> (engine, ui);
    executorBar   = std::make_unique<ExecutorBar> (engine, ui);
    audioView     = std::make_unique<AudioView> (engine, ui, *audioInput);
    midiView      = std::make_unique<MidiView> (*midi);

    midi->onEncoder = [this] (int enc, float delta, bool absolute, float value)
    {
        programmerBar->encoderInput (enc, delta, absolute, value);
    };
    midi->encoderValue = [this] (int enc) { return programmerBar->encoderValue (enc); };
    midi->onEffectKnob = [this] (int knob, float delta) { effectsPanel->nudgeKnob (knob, delta); };
    midi->effectKnobValue = [this] (int knob) { return effectsPanel->knobValue (knob); };
    midi->onKey = [this] (MidiTarget t)
    {
        if      (t == MidiTarget::Blackout) executorBar->toggleBlackout();
        else if (t == MidiTarget::Tap)      executorBar->tapTempo();
        else if (t == MidiTarget::PageUp)   executorBar->changePage (+1);
        else if (t == MidiTarget::PageDown) executorBar->changePage (-1);
        else if (t == MidiTarget::Store)    programmerBar->toggleStore();
        else if (t == MidiTarget::Clear)    programmerBar->clearStep();
        else if (t == MidiTarget::Fan)      programmerBar->cycleFan();
        else if (t == MidiTarget::Next)     programmerBar->step (+1);
        else if (t == MidiTarget::Prev)     programmerBar->step (-1);
        else if (t == MidiTarget::StepAll)  programmerBar->stepAll();
        else if (t == MidiTarget::Highlight) programmerBar->toggleHighlight();
        else if (t == MidiTarget::Lowlight) programmerBar->toggleLowlight();
        else if (t == MidiTarget::Fine)     programmerBar->toggleFine();
    };
    midi->onExecutorTap = [this] (int slot) { executorBar->stripTapped (slot); };
    midi->keyState = [this] (MidiTarget t)
    {
        const juce::ScopedLock sl (engine.dataLock);
        return t == MidiTarget::Highlight ? engine.programmer.isHighlight() : engine.programmer.isLowlight();
    };
    executorBar->onMessage = [this] (const juce::String& m, bool err) { flashMessage (m, err); };
    programmerBar->onMessage = [this] (const juce::String& m, bool err) { flashMessage (m, err); };
    presetsView->onMessage = [this] (const juce::String& m, bool err) { flashMessage (m, err); };
    cuesView->onMessage = [this] (const juce::String& m, bool err) { flashMessage (m, err); };
    ui.message = [this] (const juce::String& m, bool err) { flashMessage (m, err); };

    views = { layoutView.get(), view3D.get(), presetsView.get(), cuesView.get(), patchView.get(),
              audioView.get(), midiView.get(), monitorView.get(), networkView.get(), djLinkView.get() };
    const char* names[] = { "Layout", "3D", "Presets", "Cues", "Patch", "Audio", "MIDI", "DMX", "Network", "DJ Link" };
    for (int i = 0; i < (int) views.size(); ++i)
    {
        addChildComponent (views[(size_t) i]);
        auto* tab = viewTabs.add (new juce::TextButton (names[i]));
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (2001);
        if (i < 9) tab->setTooltip (juce::String ("Cmd+") + juce::String (i + 1));
        tab->onClick = [this, i] { showView (i); };
        addAndMakeVisible (tab);
    }

    newButton.onClick  = [this] { newShow(); };
    openButton.onClick = [this] { openShow(); };
    saveButton.onClick = [this] { saveShow (false); };
    undoButton.setTooltip ("Undo (Cmd+Z) - patch, cues, presets, layout, programmer...");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    for (auto* b : { &newButton, &openButton, &saveButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    history.reset();

    effectsToggle.setClickingTogglesState (true);
    effectsToggle.setToggleState (true, juce::dontSendNotification);
    effectsToggle.setColour (juce::TextButton::buttonOnColourId, theme::familyColour (Family::Beam));
    effectsToggle.setTooltip ("Show the effects editor (E)");
    effectsToggle.onClick = [this] { resized(); };
    addAndMakeVisible (effectsToggle);

    screensButton.setTooltip ("Open extra screens (separate windows you can put on other displays)");
    screensButton.onClick = [this] { showScreensMenu(); };
    addAndMakeVisible (screensButton);

    addAndMakeVisible (effectsPanel.get());
    addAndMakeVisible (programmerBar.get());
    addAndMakeVisible (executorBar.get());

    setWantsKeyboardFocus (true);
    showView (0);
    startTimerHz (20);

    // reopen the extra screens from last time
    auto saved = settings::get ("screens");
    for (int n : { 2, 3 })
        if ((bool) saved[juce::String (n).toRawUTF8()]["open"])
            openScreen (n, saved[juce::String (n).toRawUTF8()]);
}

MainComponent::~MainComponent()
{
    saveScreens();
    screens.clear();           // their views reference engine, midi and audio
    // views reference midi and audio: destroy them first
    for (auto* v : views) removeChildComponent (v);
    audioView = nullptr;
    midiView = nullptr;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    g.setColour (theme::panel);
    g.fillRect (topBar);
    g.setColour (theme::line);
    g.drawHorizontalLine (topBar.getBottom() - 1, 0.0f, (float) getWidth());

    static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::LogoMark_png, BinaryData::LogoMark_pngSize);
    g.drawImage (logo, juce::Rectangle<float> (12.0f, (float) topBar.getCentreY() - 19.0f, 38.0f, 38.0f),
                 juce::RectanglePlacement::centred);
    g.setColour (theme::text);
    g.setFont (theme::font (20.0f, true));
    g.drawText ("PHASE", topBar.withTrimmedLeft (54).withWidth (90), juce::Justification::centredLeft);

    auto pill = statusArea.toFloat().reduced (0, 10);
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (pill, pill.getHeight() / 2);
    const bool showMsg = messageTicks > 0;
    g.setColour (showMsg ? (messageError ? theme::danger : theme::ok) : (statusOk ? theme::ok : theme::danger));
    g.fillEllipse (pill.getX() + 12, pill.getCentreY() - 5, 10, 10);
    g.setColour (showMsg ? (messageError ? theme::danger : theme::ok) : theme::text);
    g.setFont (theme::font (13.0f, showMsg));
    g.drawText (showMsg ? messageText : statusText, pill.withTrimmedLeft (30).withTrimmedRight (12),
                juce::Justification::centredLeft, true);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (theme::topBarH);

    auto t = topBar.reduced (theme::gap, 6);
    t.removeFromLeft (130);
    for (auto* b : { &newButton, &openButton, &saveButton, &undoButton, &redoButton })
    {
        if (b == &undoButton) t.removeFromLeft (6);
        b->setBounds (t.removeFromLeft (52));
        t.removeFromLeft (3);
    }
    t.removeFromLeft (10);
    statusArea = t.removeFromRight (juce::jmin (320, juce::jmax (180, t.getWidth() / 6)));
    t.removeFromRight (8);
    effectsToggle.setBounds (t.removeFromRight (76));
    t.removeFromRight (4);
    screensButton.setBounds (t.removeFromRight (78));
    t.removeFromRight (8);
    // view tabs share what's left (Presets and Network are longer words)
    const int unit = juce::jlimit (56, 86, (t.getWidth() - 4 * viewTabs.size()) / juce::jmax (1, viewTabs.size()));
    for (int i = 0; i < viewTabs.size(); ++i)
    {
        viewTabs[i]->setBounds (t.removeFromLeft (i == 2 || i == 8 ? unit + 8 : i == 1 ? unit - 14 : unit - 2));
        t.removeFromLeft (4);
    }

    const int execH = juce::jlimit (230, 300, getHeight() * 30 / 100);
    executorBar->setBounds (r.removeFromBottom (execH));
    programmerBar->setBounds (r.removeFromBottom (158));

    // the effects dock belongs to programming screens; editors get the full width
    const bool fx = effectsToggle.getToggleState() && currentView <= 3;      // Layout, 3D, Presets, Cues
    effectsPanel->setVisible (fx);
    if (fx)
        effectsPanel->setBounds (r.removeFromRight (400));

    for (auto* v : views)
        v->setBounds (r);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const bool plain = ! key.getModifiers().isAnyModifierKeyDown();

    if (key == juce::KeyPress::escapeKey)                 { programmerBar->clearStep(); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))              { saveShow (false); return true; }
    if (key == juce::KeyPress ('s', cmd | juce::ModifierKeys::shiftModifier, 0)) { saveShow (true); return true; }
    if (key == juce::KeyPress ('o', cmd, 0))              { openShow(); return true; }
    if (key == juce::KeyPress ('z', cmd, 0))              { undo(); return true; }
    if (key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', cmd, 0))           { redo(); return true; }
    if (key == juce::KeyPress ('n', cmd, 0))              { newShow(); return true; }
    if (key == juce::KeyPress ('a', cmd, 0))              { layoutView->selectAll(); return true; }
    for (int i = 0; i < (int) views.size(); ++i)
        if (key == juce::KeyPress ('1' + i, cmd, 0))      { showView (i); return true; }

    if (plain)
    {
        const auto c = key.getTextCharacter();
        // typing a number starts a command ("1 thru 10 at 50"), like a console
        if (juce::CharacterFunctions::isDigit (c)) { programmerBar->focusCommandLine (juce::String::charToString (c)); return true; }
        if (c == 'h' || c == 'H') { programmerBar->toggleHighlight(); return true; }
        if (c == 'l' || c == 'L') { programmerBar->toggleLowlight(); return true; }
        if (c == '.' || c == '>') { programmerBar->step (+1); return true; }
        if (c == ',' || c == '<') { programmerBar->step (-1); return true; }
        if (c == '/')             { programmerBar->stepAll(); return true; }
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
            { programmerBar->toggleEditMode (UiState::EditMode::Delete); return true; }
        if (key.getKeyCode() == juce::KeyPress::escapeKey && (ui.editMode != UiState::EditMode::None || ui.storeArmed))
            { programmerBar->clearStep(); return true; }
        if (c == 'b' || c == 'B') { executorBar->toggleBlackout(); return true; }
        if (c == 's' || c == 'S') { programmerBar->toggleStore(); return true; }
        if (c == 'e' || c == 'E') { effectsToggle.triggerClick(); return true; }
        if (c == 'f' || c == 'F') { programmerBar->cycleFan(); return true; }
        if (c == 't' || c == 'T') { executorBar->tapTempo(); return true; }
        if (c == ']')             { executorBar->changePage (+1); return true; }
        if (c == '[')             { executorBar->changePage (-1); return true; }
    }
    return false;
}

//==============================================================================
void MainComponent::timerCallback()
{
    // detected beats tap the dimmer FX tempo (using the beat's own timestamp)
    const int beats = audioInput->beatCount.load();
    if (beats != lastBeatSeen)
    {
        lastBeatSeen = beats;
        if (audioView->followBeat())
            executorBar->tapTempo (audioInput->lastBeatTime.load());
    }
    if (++tick % 5 != 0) return;

    // custom fixture types also go to the machine library (every show can use them)
    if (tick % 100 == 0)
    {
        juce::String types;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& t : engine.library.types())
                if (! t.builtIn) types << juce::JSON::toString (t.toVar(), true);
        }
        const auto hash = types.hashCode64();
        if (hash != libraryHash) { libraryHash = hash; if (types.isNotEmpty()) showfile::saveMachineLibrary (engine); }
    }

    // undo steps: record finished edits (not while a knob or fader is being dragged)
    if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        history.capture();
    undoButton.setEnabled (history.canUndo());
    redoButton.setEnabled (history.canRedo());

    if (tick % 20 == 0)
    {
        // receive the NDI sources that pixel maps use
        juce::StringArray wanted;
        {
            const juce::ScopedLock sl (engine.dataLock);
            for (auto& m : engine.pixelMaps)
                if (m.enabled && m.source.startsWith ("NDI: ")) wanted.addIfNotAlreadyThere (m.source);
        }
        ndi->setWanted (wanted);
    }

    auto s = engine.getNetSettings();
    const int pps = engine.getPacketsPerSecond();
    const int unis = (int) engine.outputUniverses().size();
    statusOk = s.enabled && pps > 0;
    statusText = ! s.enabled ? juce::String ("Art-Net output off")
                             : "Art-Net  " + juce::String (unis) + " univ  -  " + juce::String (pps) + " pkt/s";
    if (messageTicks > 0) --messageTicks;   // 4 per second
    repaint (statusArea);
}

void MainComponent::undo()
{
    if (! history.undo()) { flashMessage ("Nothing to undo", true); return; }
    engine.showChanged();
    ui.changed();
    flashMessage ("Undo  (" + juce::String (history.undoSteps()) + " more)", false);
}

void MainComponent::redo()
{
    if (! history.redo()) { flashMessage ("Nothing to redo", true); return; }
    engine.showChanged();
    ui.changed();
    flashMessage ("Redo  (" + juce::String (history.redoSteps()) + " more)", false);
}

void MainComponent::flashMessage (const juce::String& m, bool error)
{
    messageText = m;
    messageError = error;
    messageTicks = 4 * 4;
    repaint (statusArea);
}

void MainComponent::showView (int index)
{
    currentView = juce::jlimit (0, (int) views.size() - 1, index);
    for (int i = 0; i < (int) views.size(); ++i)
    {
        views[(size_t) i]->setVisible (i == currentView);
        viewTabs[i]->setToggleState (i == currentView, juce::dontSendNotification);
    }
    resized();
    grabKeyboardFocus();
}

void MainComponent::updateTitle()
{
    if (onTitleChanged)
        onTitleChanged (currentFile.existsAsFile() ? "Phase - " + currentFile.getFileNameWithoutExtension()
                                                   : juce::String ("Phase - Untitled"));
}

//==============================================================================
std::unique_ptr<juce::Component> MainComponent::createView (const juce::String& name)
{
    auto message = [this] (const juce::String& m, bool err) { flashMessage (m, err); };
    if (name == "Layout")    return std::make_unique<LayoutView> (engine, ui);
    if (name == "3D")        return std::make_unique<Visualizer3DView> (engine, ui);
    if (name == "Presets")   { auto v = std::make_unique<PresetsView> (engine, ui); v->onMessage = message; return v; }
    if (name == "Cues")      { auto v = std::make_unique<CuesView> (engine, ui); v->onMessage = message; return v; }
    if (name == "Patch")     return std::make_unique<PatchView> (engine, ui);
    if (name == "Effects")   return std::make_unique<EffectsPanel> (engine, ui);
    if (name == "Executors") { auto v = std::make_unique<ExecutorBar> (engine, ui); v->onMessage = message; return v; }
    if (name == "Audio")     return std::make_unique<AudioView> (engine, ui, *audioInput);
    if (name == "MIDI")      return std::make_unique<MidiView> (*midi);
    if (name == "DMX")       return std::make_unique<DmxMonitorView> (engine);
    if (name == "Network")   return std::make_unique<NetworkView> (engine);
    if (name == "DJ Link")   return std::make_unique<DjLinkView> (*djLink, engine);
    return nullptr;
}

void MainComponent::showScreensMenu()
{
    juce::PopupMenu m;
    for (int n : { 2, 3 })
    {
        bool open = false;
        for (auto* w : screens) open |= w->getNumber() == n;
        m.addItem (n, "Screen " + juce::String (n), true, open);
    }
    m.addSeparator();
    m.addItem (100, "Tip: drag a screen window to another display, then use full screen", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&screensButton),
                     [safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r < 2 || r > 3) return;
        bool open = false;
        for (auto* w : safe->screens) open |= w->getNumber() == r;
        if (open) safe->closeScreen (r); else safe->openScreen (r);
    });
}

void MainComponent::openScreen (int number, const juce::var& state)
{
    for (auto* w : screens) if (w->getNumber() == number) { w->toFront (true); return; }
    auto* w = screens.add (new ScreenWindow (number, [this] (const juce::String& n) { return createView (n); }));
    w->addKeyListener (&shortcutForwarder);
    if (state.isObject())
    {
        w->content().fromVar (state);
        const auto bounds = state["window"].toString();
        if (bounds.isNotEmpty()) w->restoreWindowStateFromString (bounds);
    }
    else if (number == 3)
        w->content().setLayout (0);
    w->content().onChanged = [this] { saveScreens(); };
    w->onClose = [this] (ScreenWindow& sw)
    {
        // never delete a window inside its own callback
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), n = sw.getNumber()]
                                         { if (safe != nullptr) safe->closeScreen (n); });
    };
    w->setVisible (true);
    saveScreens();
}

void MainComponent::closeScreen (int number)
{
    for (int i = screens.size(); --i >= 0;)
        if (screens[i]->getNumber() == number)
        {
            screens.remove (i);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                             { if (safe != nullptr) safe->saveScreens(); });
        }
}

void MainComponent::saveScreens()
{
    auto saved = settings::get ("screens");
    auto* o = new juce::DynamicObject();
    for (int n : { 2, 3 })
    {
        juce::var state = saved[juce::String (n).toRawUTF8()];
        ScreenWindow* w = nullptr;
        for (auto* x : screens) if (x->getNumber() == n) w = x;
        if (w != nullptr)
        {
            state = w->content().toVar();
            state.getDynamicObject()->setProperty ("window", w->getWindowStateAsString());
            state.getDynamicObject()->setProperty ("open", true);
        }
        else if (state.isObject())
            state.getDynamicObject()->setProperty ("open", false);
        if (state.isObject()) o->setProperty (juce::String (n), state);
    }
    settings::set ("screens", juce::var (o));
}

//==============================================================================
void MainComponent::runMidiSelfTest()
{
    int fails = 0;
    auto check = [&fails] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
        if (! ok) ++fails;
    };
    using M = juce::MidiMessage;

    // a cue on executor 1.1 to play with
    {
        const juce::ScopedLock sl (engine.dataLock);
        if (engine.patch.fixtures().empty())
        {
            PatchedFixture f;
            f.fid = 1; f.name = "Dim"; f.typeId = "builtin.dimmer";
            engine.patch.add (f, engine.library);
        }
        const int fid = engine.patch.fixtures().front().fid;
        engine.playback.store (1, 1, ValueMap { { fid, { { "Dimmer", 1.0f } } } }, {}, StoreMode::Overwrite, DmxEngine::now());
        engine.playback.off (1, 1);
        engine.programmer.clearValues();
        engine.programmer.setSelection ({ fid });
    }
    ui.page = 1; ui.family = Family::Dimmer; ui.changed();
    programmerBar->resized();

    // learn: fader = CC 7 ch 1, Go = note 36, encoder 1 = CC 20 relative, grand = pitch bend ch 9, tap = note 40
    midi->startLearn (MidiTarget::ExecFader, 1);  midi->handleMessage (M::controllerEvent (1, 7, 10));
    midi->startLearn (MidiTarget::ExecGo, 1);     midi->handleMessage (M::noteOn (1, 36, (juce::uint8) 100));
    midi->startLearn (MidiTarget::Encoder, 1);    midi->handleMessage (M::controllerEvent (1, 20, 1));
    midi->setEncoderMode (1, MidiBinding::EncoderMode::Relative2s);
    midi->startLearn (MidiTarget::Grand, 0);      midi->handleMessage (M::pitchWheel (9, 8192));
    midi->startLearn (MidiTarget::PageUp, 0);     midi->handleMessage (M::noteOn (1, 41, (juce::uint8) 100));
    check (midi->bindingFor (MidiTarget::ExecFader, 1) != nullptr && midi->bindingFor (MidiTarget::ExecFader, 1)->number == 7, "learn fader CC 7");
    check (midi->bindingFor (MidiTarget::ExecGo, 1) != nullptr && midi->bindingFor (MidiTarget::ExecGo, 1)->kind == MidiBinding::Kind::Note, "learn Go note");
    check (midi->bindingFor (MidiTarget::Grand, 0) != nullptr && midi->bindingFor (MidiTarget::Grand, 0)->kind == MidiBinding::Kind::PitchBend, "learn grand pitch bend");

    // fader up from 0 auto-starts the executor
    midi->handleMessage (M::controllerEvent (1, 7, 0));
    juce::Thread::sleep (300);   // past the motor-fader guard time
    midi->handleMessage (M::controllerEvent (1, 7, 127));
    { const juce::ScopedLock sl (engine.dataLock);
      check (engine.playback.state (1, 1).isOn() && engine.playback.state (1, 1).fader > 0.99f, "fader CC starts executor 1.1"); }

    midi->handleMessage (M::controllerEvent (1, 7, 64));
    { const juce::ScopedLock sl (engine.dataLock);
      check (std::abs (engine.playback.state (1, 1).fader - 64.0f / 127.0f) < 0.01f, "fader CC 64 -> 50%"); }

    midi->handleMessage (M::pitchWheel (9, 16383));
    check (engine.masters.grand.load() > 0.99f, "pitch bend -> grand master 100%");
    midi->handleMessage (M::pitchWheel (9, 4096));
    check (std::abs (engine.masters.grand.load() - 0.25f) < 0.01f, "pitch bend -> grand master 25%");
    engine.masters.grand = 1.0f;

    // encoder 1 controls the first knob (Dimmer): 20 ticks up = +10%
    for (int i = 0; i < 20; ++i) midi->handleMessage (M::controllerEvent (1, 20, 1));
    float dim = -1.0f;
    { const juce::ScopedLock sl (engine.dataLock);
      dim = engine.programmer.get (engine.programmer.selection().front(), "Dimmer").value_or (-1.0f); }
    check (dim > 0.05f && dim < 0.2f, "relative encoder ticks set Dimmer (" + juce::String (dim, 3) + ")");
    for (int i = 0; i < 10; ++i) midi->handleMessage (M::controllerEvent (1, 20, 127));   // -1 each
    { const juce::ScopedLock sl (engine.dataLock);
      const float d2 = engine.programmer.get (engine.programmer.selection().front(), "Dimmer").value_or (-1.0f);
      check (d2 < dim && d2 > 0.0f, "encoder turns back down (" + juce::String (d2, 3) + ")"); }

    // page key: fader mapping now follows page 2
    midi->handleMessage (M::noteOn (1, 41, (juce::uint8) 100));
    check (ui.page == 2, "page + key -> page 2");
    midi->handleMessage (M::controllerEvent (1, 7, 30));
    { const juce::ScopedLock sl (engine.dataLock);
      check (std::abs (engine.playback.state (2, 1).fader - 30.0f / 127.0f) < 0.01f
             && std::abs (engine.playback.state (1, 1).fader - 64.0f / 127.0f) < 0.01f, "fader moves page 2 exec, page 1 untouched"); }
    executorBar->changePage (-1);

    // Go note on executor 1.1 with one cue wraps and keeps it on
    midi->handleMessage (M::noteOn (1, 36, (juce::uint8) 100));
    { const juce::ScopedLock sl (engine.dataLock); check (engine.playback.state (1, 1).isOn(), "Go note keeps executor running"); }

    // tap tempo: 4 taps 0.5 s apart -> 120 BPM -> dimmer FX speed 200%
    const double t0 = DmxEngine::now();
    for (int i = 0; i < 4; ++i) executorBar->tapTempo (t0 + i * 0.5);
    check (std::abs (Masters::speedMultiplier (engine.masters.dimmerSpeed.load()) - 2.0f) < 0.01f, "tap 120 BPM -> dimmer speed 2.0x");

    // clean up the test bindings
    for (auto t : { MidiTarget::ExecFader, MidiTarget::ExecGo, MidiTarget::Encoder, MidiTarget::Grand, MidiTarget::PageUp })
        midi->clearBinding (t, t == MidiTarget::Grand || t == MidiTarget::PageUp ? 0 : 1);

    // --- X-Touch Compact preset (factory Layer A) -----------------------------------
    const auto keep = midi->getBindings();
    midi->loadPreset ("Behringer X-Touch Compact (Standard MIDI)");
    check (midi->bindingFor (MidiTarget::ExecFader, 8) != nullptr && midi->bindingFor (MidiTarget::ExecFader, 8)->number == 8
           && midi->bindingFor (MidiTarget::Grand, 0)->number == 9, "X-Touch: faders 1-8 + main fader");
    { const juce::ScopedLock sl (engine.dataLock); engine.playback.off (1, 1); engine.playback.setFader (1, 1, 0.0f, DmxEngine::now()); }
    juce::Thread::sleep (300);
    midi->handleMessage (M::controllerEvent (1, 1, 127));
    { const juce::ScopedLock sl (engine.dataLock); check (engine.playback.state (1, 1).isOn(), "X-Touch: fader 1 starts executor 1"); }
    midi->handleMessage (M::controllerEvent (1, 9, 64));
    check (std::abs (engine.masters.grand.load() - 64.0f / 127.0f) < 0.01f, "X-Touch: main fader -> grand master");
    engine.masters.grand = 1.0f;

    // top encoder 1 (CC 10, absolute knob used as endless): +10 steps, and keeps going at the end
    const float x0 = juce::jmax (0.0f, programmerBar->encoderValue (1));
    midi->handleMessage (M::controllerEvent (1, 10, 64));            // first value only sets the start
    midi->handleMessage (M::controllerEvent (1, 10, 74));
    float x1 = -1.0f;
    { const juce::ScopedLock sl (engine.dataLock); x1 = engine.programmer.get (engine.programmer.selection().front(), "Dimmer").value_or (-1.0f); }
    check (std::abs (x1 - x0 - 10.0f / 127.0f) < 0.02f, "X-Touch: encoder 1 +10 steps -> Dimmer " + juce::String (x1, 3));
    midi->handleMessage (M::controllerEvent (1, 10, 127));
    midi->handleMessage (M::controllerEvent (1, 10, 127));           // knob at its end: still counts
    float x2 = -1.0f;
    { const juce::ScopedLock sl (engine.dataLock); x2 = engine.programmer.get (engine.programmer.selection().front(), "Dimmer").value_or (-1.0f); }
    check (x2 > x1 + 53.0f / 127.0f, "X-Touch: turning past the end keeps going (" + juce::String (x2, 3) + ")");

    engine.masters.size = 0.5f;
    midi->handleMessage (M::controllerEvent (1, 17, 50));
    midi->handleMessage (M::controllerEvent (1, 17, 40));
    check (std::abs (engine.masters.size.load() - (0.5f - 10.0f / 127.0f)) < 0.01f, "X-Touch: encoder 8 -> FX size master");
    engine.masters.size = 1.0f;

    { const juce::ScopedLock sl (engine.dataLock); engine.playback.setButtonFunction (1, 1, 3, ButtonFunction::Flash); }
    midi->handleMessage (M::noteOn (1, 40, (juce::uint8) 127));      // button under fader 1 = executor button 4 (Flash)
    { const juce::ScopedLock sl (engine.dataLock); check (engine.playback.state (1, 1).flash, "X-Touch: button under fader 1 -> Flash"); }
    midi->handleMessage (M::noteOff (1, 40));
    const bool wasBlack = engine.masters.blackout.load();
    midi->handleMessage (M::noteOn (1, 48, (juce::uint8) 127));
    check (engine.masters.blackout.load() != wasBlack, "X-Touch: button under the main fader -> blackout");
    if (engine.masters.blackout.load() != wasBlack) executorBar->toggleBlackout();
    midi->handleMessage (M::noteOn (1, 50, (juce::uint8) 127));
    check (ui.page == 2, "X-Touch: right button 2 -> page +");
    midi->handleMessage (M::noteOn (1, 49, (juce::uint8) 127));
    check (ui.page == 1, "X-Touch: right button 1 -> page -");

    // touch-sensitive faders: CC 101 = finger on fader 1
    check (midi->bindingFor (MidiTarget::ExecFader, 1)->touchNumber == 101 && midi->bindingFor (MidiTarget::Grand, 0)->touchNumber == 109,
           "X-Touch: fader touch CC 101-109 mapped");
    midi->handleMessage (M::controllerEvent (1, 101, 127));
    check (midi->isTouched (MidiTarget::ExecFader, 1), "X-Touch: touch fader 1");
    midi->handleMessage (M::controllerEvent (1, 1, 40));
    { const juce::ScopedLock sl (engine.dataLock);
      check (std::abs (engine.playback.state (1, 1).fader - 40.0f / 127.0f) < 0.01f, "X-Touch: touched fader moves executor 1"); }
    midi->handleMessage (M::controllerEvent (1, 101, 0));
    check (! midi->isTouched (MidiTarget::ExecFader, 1), "X-Touch: release fader 1");

    // learning a touch fader picks up its touch message too
    midi->startLearn (MidiTarget::ExecFader, 2);
    midi->handleMessage (M::controllerEvent (1, 102, 127));      // touch arrives first...
    midi->handleMessage (M::controllerEvent (1, 2, 70));         // ...then the movement
    check (! midi->isLearning() && midi->bindingFor (MidiTarget::ExecFader, 2)->number == 2
           && midi->bindingFor (MidiTarget::ExecFader, 2)->touchNumber == 102, "learn: fader + its touch CC");

    // --- X-Touch Compact in Mackie Control mode --------------------------------------
    midi->loadPreset ("Behringer X-Touch Compact (Mackie Control)");
    { const juce::ScopedLock sl (engine.dataLock); engine.playback.setFader (1, 1, 0.0f, DmxEngine::now()); }
    midi->handleMessage (M::noteOn (1, 104, (juce::uint8) 127));          // touch fader 1 (touch notes on channel 1)
    check (midi->isTouched (MidiTarget::ExecFader, 1), "MC: touch note 104 -> fader 1 touched");
    midi->handleMessage (M::pitchWheel (1, 8192));                        // fader 1 = pitch bend channel 1
    { const juce::ScopedLock sl (engine.dataLock);
      check (std::abs (engine.playback.state (1, 1).fader - 0.5f) < 0.01f, "MC: fader 1 pitch bend -> executor 1 at 50%"); }
    midi->handleMessage (M::noteOn (1, 104, (juce::uint8) 0));
    check (! midi->isTouched (MidiTarget::ExecFader, 1), "MC: release fader 1");
    midi->handleMessage (M::noteOn (1, 105, (juce::uint8) 127));
    midi->handleMessage (M::pitchWheel (2, 16383));
    midi->handleMessage (M::noteOn (1, 105, (juce::uint8) 0));
    { const juce::ScopedLock sl (engine.dataLock);
      check (engine.playback.state (1, 2).fader > 0.99f, "MC: fader 2 = pitch bend channel 2"); }
    midi->handleMessage (M::noteOn (1, 112, (juce::uint8) 127));
    midi->handleMessage (M::pitchWheel (9, 4096));
    midi->handleMessage (M::noteOn (1, 112, (juce::uint8) 0));
    check (std::abs (engine.masters.grand.load() - 0.25f) < 0.01f, "MC: main fader -> grand master");
    engine.masters.grand = 1.0f;

    // V-Pot 1: sign + ticks
    const float m0 = juce::jmax (0.0f, programmerBar->encoderValue (1));
    midi->handleMessage (M::controllerEvent (1, 16, 5));                  // 5 ticks clockwise
    midi->handleMessage (M::controllerEvent (1, 16, 65));                 // 1 tick back
    float m1 = -1.0f;
    { const juce::ScopedLock sl (engine.dataLock); m1 = engine.programmer.get (engine.programmer.selection().front(), "Dimmer").value_or (-1.0f); }
    check (std::abs (m1 - m0 - 4.0f / 127.0f) < 0.01f, "MC: V-Pot 1 +5 -1 ticks -> encoder 1 (" + juce::String (m1, 3) + ")");
    engine.masters.size = 0.5f;
    midi->handleMessage (M::controllerEvent (1, 23, 10));
    check (std::abs (engine.masters.size.load() - (0.5f + 10.0f / 127.0f)) < 0.01f, "MC: V-Pot 8 -> FX size");
    engine.masters.size = 1.0f;

    // knob layers: push right encoder 2 -> top knobs drive the Effects panel
    midi->handleMessage (M::noteOn (1, 41, (juce::uint8) 127));
    check (midi->getKnobLayer() == 1, "MC: encoder push (note 41) -> knobs = Effects 1-8");
    engine.masters.size = 0.5f;
    midi->handleMessage (M::controllerEvent (1, 23, 10));
    check (std::abs (engine.masters.size.load() - 0.5f) < 0.001f, "MC: in the Effects layer V-Pot 8 leaves the size master");
    engine.masters.size = 1.0f;
    midi->handleMessage (M::noteOn (1, 40, (juce::uint8) 127));
    check (midi->getKnobLayer() == 0, "MC: note 40 -> back to programmer knobs");

    // buttons: SELECT row = executor button 4, Flip = blackout, Channel > = Next
    midi->handleMessage (M::noteOn (1, 24, (juce::uint8) 127));
    { const juce::ScopedLock sl (engine.dataLock); check (engine.playback.state (1, 1).flash, "MC: SELECT 1 -> executor button 4 (Flash)"); }
    midi->handleMessage (M::noteOn (1, 24, (juce::uint8) 0));
    const bool wasBlack2 = engine.masters.blackout.load();
    midi->handleMessage (M::noteOn (1, 50, (juce::uint8) 127));
    check (engine.masters.blackout.load() != wasBlack2, "MC: Flip -> blackout");
    if (engine.masters.blackout.load() != wasBlack2) executorBar->toggleBlackout();
    { const juce::ScopedLock sl (engine.dataLock); engine.programmer.setSelection ({ 1, 2, 3 }); }
    midi->handleMessage (M::noteOn (1, 49, (juce::uint8) 127));
    midi->handleMessage (M::noteOn (1, 49, (juce::uint8) 127));
    { const juce::ScopedLock sl (engine.dataLock);
      check (engine.programmer.selection() == std::vector<int> { 2 }, "MC: Channel > twice -> Next, Next = fixture 2"); }
    midi->handleMessage (M::noteOn (1, 45, (juce::uint8) 127));
    { const juce::ScopedLock sl (engine.dataLock);
      check (engine.programmer.selection().size() == 3, "MC: encoder push 6 -> All"); }
    midi->handleMessage (M::noteOn (1, 44, (juce::uint8) 127));
    { const juce::ScopedLock sl (engine.dataLock); check (engine.programmer.isLowlight(), "MC: encoder push 5 -> Lowlight"); }
    programmerBar->toggleLowlight();

    // Store armed + an executor button on the controller stores there (empty slot: straight away)
    { const juce::ScopedLock sl (engine.dataLock);
      engine.playback.deleteExecutor (1, 8); engine.playback.deleteExecutor (1, 7);
      engine.programmer.setSelection ({ 1 }); engine.programmer.set (1, "Dimmer", 0.7f); }
    ui.storeArmed = true;
    midi->handleMessage (M::noteOn (1, 7, (juce::uint8) 127));          // REC 8 = executor 8 button 1
    { const juce::ScopedLock sl (engine.dataLock); check (engine.playback.find (1, 8) != nullptr && ! ui.storeArmed, "MC: Store + REC 8 -> stored on executor 8"); }
    ui.setEditMode (UiState::EditMode::Move);
    midi->handleMessage (M::noteOn (1, 7, (juce::uint8) 127));          // source: executor 8
    midi->handleMessage (M::noteOn (1, 6, (juce::uint8) 127));          // target: executor 7
    { const juce::ScopedLock sl (engine.dataLock);
      check (engine.playback.find (1, 7) != nullptr && engine.playback.find (1, 8) == nullptr, "MC: Move with executor buttons 8 -> 7");
      engine.playback.deleteExecutor (1, 7); }

    // learning a Mackie fader: touch note first, then pitch bend
    midi->startLearn (MidiTarget::ExecFader, 3);
    midi->handleMessage (M::noteOn (1, 106, (juce::uint8) 127));
    midi->handleMessage (M::pitchWheel (3, 9000));
    check (! midi->isLearning() && midi->bindingFor (MidiTarget::ExecFader, 3)->kind == MidiBinding::Kind::PitchBend
           && midi->bindingFor (MidiTarget::ExecFader, 3)->touchNumber == 106
           && midi->bindingFor (MidiTarget::ExecFader, 3)->touchChannel == 1, "learn: Mackie fader + touch note");
    midi->setBindings (keep);

    std::cout << (fails == 0 ? "MIDI self-test passed" : "MIDI self-test FAILED") << std::endl;
}

juce::File MainComponent::autosaveFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Phase").getChildFile ("autosave.phase");
}

void MainComponent::newShow()
{
    auto opts = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "New show",
        "Clear the patch, custom fixture types, executors and programmer?", "Clear", "Cancel", this);

    juce::AlertWindow::showAsync (opts, [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
    {
        if (safe == nullptr || result != 1) return;
        showfile::newShow (safe->engine);
        safe->history.reset();
        safe->currentFile = juce::File();
        safe->updateTitle();
        safe->engine.showChanged();
        safe->ui.changed();
    });
}

void MainComponent::openShow()
{
    chooser = std::make_unique<juce::FileChooser> ("Open show", currentFile.getParentDirectory(), "*.phase");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f != juce::File())
            loadShowFile (f);
    });
}

void MainComponent::loadShowFile (const juce::File& f)
{
    auto err = showfile::load (engine, f);
    if (err.isNotEmpty())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
            juce::MessageBoxIconType::WarningIcon, "Couldn't open show", err), nullptr);
        return;
    }
    if (f != autosaveFile())
        currentFile = f;
    history.reset();
    updateTitle();
    engine.showChanged();
    ui.changed();
}

void MainComponent::saveShow (bool saveAs)
{
    auto write = [this] (const juce::File& f)
    {
        auto err = showfile::save (engine, f);
        if (err.isNotEmpty())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                juce::MessageBoxIconType::WarningIcon, "Couldn't save show", err), nullptr);
            return;
        }
        currentFile = f;
        updateTitle();
        flashMessage ("Saved " + f.getFileName(), false);
    };

    if (! saveAs && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    auto start = currentFile != juce::File() ? currentFile
                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Untitled.phase");
    chooser = std::make_unique<juce::FileChooser> ("Save show", start, "*.phase");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f != juce::File())
            write (f.withFileExtension (showfile::extension));
    });
}

} // namespace pl
