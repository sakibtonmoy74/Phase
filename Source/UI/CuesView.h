#pragma once
#include "Widgets.h"

namespace pl
{

/** Cue list of the executor last tapped (or stored to): rename cues, set fade
    times, jump to a cue, delete cues, store into a cue, and see / stop the
    effects running in each cue (they track from earlier cues). */
class CuesView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    CuesView (DmxEngine&, UiState&);
    ~CuesView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    class CueRow;
    void openFolders (int cueIndex);
    void storeMenu (int cueIndex, juce::Component* target);
    void effectsMenu (int cueIndex, juce::Component* target);

    std::function<void (const juce::String&, bool error)> onMessage;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();
    void layoutRows();

    DmxEngine& engine;
    UiState& ui;

    juce::Label header, nameLabel, hint;
    juce::TextEditor nameEdit;
    juce::TextButton deleteExecButton { "Delete executor" };
    juce::ToggleButton trackingToggle { "Tracking" }, chaseToggle { "Chase" };
    juce::ComboBox rateCombo, fadeCombo, orderCombo, faderCombo;
    juce::OwnedArray<juce::ComboBox> buttonCombos;
    juce::Label faderLabel { {}, "Fader" }, buttonsLabel { {}, "Buttons" };
    void pushChase();
    std::vector<juce::Component*> optionComponents();
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<CueRow> rows;
    juce::Rectangle<int> panel;
    int shownPage = 0, shownSlot = 0, shownCount = -1;
    juce::Component::SafePointer<juce::DialogWindow> foldersWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CuesView)
};

} // namespace pl
