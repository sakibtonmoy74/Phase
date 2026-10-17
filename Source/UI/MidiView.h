#pragma once
#include "Widgets.h"
#include "Control/MidiControl.h"

namespace pl
{

/** MIDI setup: choose inputs and the feedback output, then click a cell and
    move a control on your MIDI surface to map it (right-click clears).
    Executor mappings follow the current page; encoders follow the programmer. */
class MidiView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    MidiView (MidiControl&);
    ~MidiView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    class LearnCell;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshDevices();

    MidiControl& midi;

    juce::Label devicesHeader, outputLabel, lastLabel, lastMessage, hint;
    juce::OwnedArray<juce::ToggleButton> inputToggles;
    juce::StringArray inputIds;
    juce::ComboBox outputCombo;
    juce::StringArray outputIds;
    juce::TextButton refreshButton { "Refresh devices" }, presetButton { "MIDI maps..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label monitorLabel;
    juce::TextEditor monitor;
    juce::StringArray shownRecent;
    std::vector<std::pair<MidiTarget, int>> modeTargets;   // which binding each mode combo edits

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<LearnCell> cells;
    juce::OwnedArray<juce::ComboBox> encoderModes;

    juce::Rectangle<int> leftPanel, rightPanel;
    bool blink = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiView)
};

} // namespace pl
