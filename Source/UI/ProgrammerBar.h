#pragma once
#include "Widgets.h"

namespace pl
{

/** Bottom programmer strip: feature groups, one encoder per attribute of the
    selected fixtures (showing the fixture's live value, with a tick at its
    default), colour tools, Fan, and the Store / Clear keys. */
class ProgrammerBar : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    ProgrammerBar (DmxEngine&, UiState&);
    ~ProgrammerBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void toggleStore();
    void clearStep();
    void cycleFan();
    void full (bool on);                  // Full / Dim Off for the selection
    void toggleHighlight();
    void toggleLowlight();
    void toggleFine();
    void step (int direction);            // Next (+1) / Prev (-1) through the selection
    void stepAll();

    /** Puts the keyboard in the command line, starting with `text`. */
    void focusCommandLine (const juce::String& text = {});
    void runCommand (const juce::String&);

    /** Status messages (command results, errors) for the main window. */
    std::function<void (const juce::String&, bool isError)> onMessage;

    /** MIDI encoder 1-4: controls the n-th attribute shown (follows the feature group). */
    /** Value 0..1 shown on encoder n's knob (for controller rings), or -1. */
    float encoderValue (int encoder) const;
    void encoderInput (int encoder, float delta, bool absolute, float value);

    /** Applies a colour to the selection (a gradient when Fan is on). */
    void applyColour (juce::Colour);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildKnobs();
    void layoutKnobs();
    void syncKnobs();
    void updateInfo();
    void updateFanButton();
    void openPicker();
    void setAttribute (const juce::String& attr, float v);
    void knockoutAttribute (const juce::String& attr);

    /** Selected fixtures that have `attr`, in selection order. */
    std::vector<int> selectedWith (const juce::String& attr) const;
    /** Programmer keys for an attribute on a fixture: "ColorR", or "ColorR@1".. on pixel fixtures. */
    std::vector<juce::String> keysFor (int fid, const juce::String& base) const;
    /** Sets an attribute on a fixture (every cell). Call with dataLock held. */
    void setAll (int fid, const juce::String& base, float v);
    /** Programmer value, else the live output value. Call with dataLock held. */
    float valueOf (int fid, const juce::String& attr, float fallback) const;
    std::pair<float, bool> currentValue (const juce::String& attr) const;
    juce::Colour currentColour (int fid) const;

    DmxEngine& engine;
    UiState& ui;

    juce::Label header, info;
    juce::OwnedArray<juce::TextButton> familyButtons;
    std::vector<Family> familyOrder;

    juce::TextButton pickerButton { "Picker" };
    juce::OwnedArray<juce::TextButton> swatchButtons;

    juce::Viewport knobViewport;
    juce::Component knobHolder;
    juce::OwnedArray<Knob> knobs;
    std::vector<juce::String> knobAttrs;
    std::map<juce::String, double> lastKnobValue;
    juce::Label emptyHint;

    FanMode fanMode = FanMode::Off;
    juce::TextButton fanButton { "Fan" }, storeButton { "Store" }, clearButton { "Clear" },
                     fullButton { "Full" }, dimOffButton { "Dim Off" }, highlightButton { "Highlight" },
                     prevButton { "Prev" }, nextButton { "Next" }, allButton { "All" },
                     lowlightButton { "Lowlight" }, lowlightColourButton,
                     copyButton { "Copy" }, moveButton { "Move" }, deleteButton { "Delete" }, fineButton { "Fine" };
    bool shownFine = false;
public:
    /** Copy / Move / Delete keys: press, then tap (press again or Clear / Esc to cancel). */
    void toggleEditMode (UiState::EditMode);
private:
    void openLowlightPicker();
    void loadLowlight();
    void saveLowlight();

    class CommandEditor;
    std::unique_ptr<CommandEditor> commandLine;
    juce::Label commandLabel;
    juce::StringArray history;
    int historyPos = 0;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgrammerBar)
};

} // namespace pl
