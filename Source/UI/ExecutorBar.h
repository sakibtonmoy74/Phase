#pragma once
#include "Widgets.h"

namespace pl
{

/** Bottom playback strip: page selector, 10 virtual executors (fader + four
    buttons whose functions are set per executor: right-click the name) and the masters (Grand, Dimmer FX speed, Movement FX speed,
    FX size, Blackout). */
class ExecutorBar : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    void stripTapped (int slot);          // header tap: store target or select for Cues view
    ExecutorBar (DmxEngine&, UiState&);
    ~ExecutorBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void changePage (int delta);
    void toggleBlackout();

    /** Tap tempo for the Dimmer FX speed master (`when` in DmxEngine::now() seconds). */
    void tapTempo (double when = DmxEngine::now());

    /** Store results and errors, shown by the main window's status area. */
    std::function<void (const juce::String&, bool isError)> onMessage;

    class Strip;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    friend class Strip;
    void storeTo (int slot);
    void showExecutorMenu (int slot, juce::Component* target);   // button / fader functions, chase
    void doStore (int page, int slot, StoreMode);
    void showMessage (const juce::String&, bool error = false);

    DmxEngine& engine;
    UiState& ui;

    juce::Label pageLabel, pageNumber;
    juce::TextButton pageUp { "+" }, pageDown { "-" };
    juce::OwnedArray<Strip> strips;

    LabelledFader grandFader { "GRAND", theme::accent };
    LabelledFader dimSpeedFader { "DIM FX SPD", theme::familyColour (Family::Dimmer) };
    LabelledFader moveSpeedFader { "MOVE FX SPD", theme::familyColour (Family::Position) };
    LabelledFader sizeFader { "MOVE SIZE", theme::familyColour (Family::Position) };
    juce::TextButton blackoutButton { "B.O." }, tapButton { "TAP" };
    TapTempo tapper;
    float tapFlash = 0.0f;

    juce::Rectangle<int> pagePanel, masterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExecutorBar)
};

} // namespace pl
