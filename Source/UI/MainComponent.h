#pragma once
#include "Widgets.h"
#include "Engine/UndoHistory.h"

namespace pl
{

class LayoutView;
class Visualizer3DView;
class CuesView;
class PatchView;
class PresetsView;
class DmxMonitorView;
class NetworkView;
class EffectsPanel;
class ProgrammerBar;
class ExecutorBar;
class MidiView;
class AudioView;
class MidiControl;
class AudioInput;
class NdiInput;
class ProDjLink;
class DjLinkView;
class ScreenWindow;

/** Screen layout:
      top bar (file, views, Effects toggle, output status)
      main view (Layout / Presets / Cues / Patch / Audio / MIDI / DMX / Network) + Effects dock
      programmer bar (feature groups, knobs, Store, Clear)
      executor bar (page, 10 executors, masters) */
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit MainComponent (DmxEngine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void newShow();
    void openShow();
    void undo();
    void redo();
    void saveShow (bool saveAs);
    void loadShowFile (const juce::File&);

    static juce::File autosaveFile();

    /** Feeds MIDI messages through learn + mapping and prints the results (--midi-selftest). */
    void runMidiSelfTest();
    std::function<void (const juce::String&)> onTitleChanged;

private:
    void timerCallback() override;
    void showView (int index);
    void updateTitle();
    void flashMessage (const juce::String&, bool error);

    DmxEngine& engine;
    UiState ui;
    std::unique_ptr<AudioInput> audioInput;
    std::unique_ptr<MidiControl> midi;
    std::unique_ptr<NdiInput> ndi;
    std::unique_ptr<ProDjLink> djLink;

    std::unique_ptr<LayoutView> layoutView;
    std::unique_ptr<Visualizer3DView> view3D;
    std::unique_ptr<CuesView> cuesView;
    std::unique_ptr<PatchView> patchView;
    std::unique_ptr<PresetsView> presetsView;
    std::unique_ptr<DmxMonitorView> monitorView;
    std::unique_ptr<NetworkView> networkView;
    std::unique_ptr<DjLinkView> djLinkView;
    std::unique_ptr<AudioView> audioView;
    std::unique_ptr<MidiView> midiView;
    std::unique_ptr<EffectsPanel> effectsPanel;
    std::unique_ptr<ProgrammerBar> programmerBar;
    std::unique_ptr<ExecutorBar> executorBar;
    std::vector<juce::Component*> views;
    int currentView = 0;

    juce::TextButton newButton { "New" }, openButton { "Open" }, saveButton { "Save" },
                     undoButton { "Undo" }, redoButton { "Redo" };
    UndoHistory history { engine };
    juce::int64 libraryHash = 0;

    /** Lets extra screen windows use the same shortcuts (Cmd+Z ...). */
    struct ShortcutForwarder : public juce::KeyListener
    {
        MainComponent& owner;
        explicit ShortcutForwarder (MainComponent& o) : owner (o) {}
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override { return owner.keyPressed (k); }
    } shortcutForwarder { *this };
    juce::TextButton effectsToggle { "Effects" }, screensButton { "Screens" };
    juce::OwnedArray<ScreenWindow> screens;
    void showScreensMenu();
    void openScreen (int number, const juce::var& state = {});
    void closeScreen (int number);
    void saveScreens();
    std::unique_ptr<juce::Component> createView (const juce::String& name);
    juce::OwnedArray<juce::TextButton> viewTabs;

    juce::Rectangle<int> topBar, statusArea;
    juce::String statusText, messageText;
    bool statusOk = false, messageError = false;
    int messageTicks = 0;
    int tick = 0, lastBeatSeen = 0;

    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace pl
