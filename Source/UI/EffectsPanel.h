#pragma once
#include "Widgets.h"

namespace pl
{

/** MA2-style effect editor: Dimmer, Movement, Color and Attribute effects
    (a waveform on any attribute, e.g. a sine on Zoom). "Apply" runs the effect
    on the current selection in selection order; while an effect is in the
    programmer, every change is live. Mirror makes it symmetrical. */
class EffectsPanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    EffectsPanel (DmxEngine&, UiState&);
    ~EffectsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** MIDI: turn the n-th visible knob (1-based, left to right, top to bottom). */
    void nudgeKnob (int number, float delta);
    /** Position 0..1 of the n-th visible knob (encoder rings), or -1. */
    float knobValue (int number);

private:
    std::vector<Knob*> visibleKnobs();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void setKind (EffectKind);
    void refreshAttributeCombo();
    void loadFromProgrammer();
    void refreshControls();
    void paramChanged();
    void apply();
    void removeEffect();
    void pickColour (bool a);
    EffectParams& draft() { return drafts[(int) kind]; }
    const EffectParams* running() const;
    juce::Colour kindColour() const;

    DmxEngine& engine;
    UiState& ui;
    EffectKind kind = EffectKind::Dimmer;
    std::map<int, EffectParams> drafts;
    bool loading = false;

    juce::Label header, info;
    juce::OwnedArray<juce::TextButton> kindTabs;
    juce::OwnedArray<juce::TextButton> formButtons;
    juce::ComboBox attributeCombo;
    juce::StringArray attributeNames;
    juce::TextButton colourAButton { "Colour A" }, colourBButton { "Colour B" };

    Knob speed { "Speed", theme::accent }, low { "Low", theme::accent }, high { "High", theme::accent },
         size { "Size", theme::accent }, width { "Width", theme::accent },
         phaseStart { "Phase start", theme::accent }, phaseEnd { "Phase end", theme::accent },
         groups { "Groups", theme::accent }, blocks { "Blocks", theme::accent }, wings { "Wings", theme::accent },
         tiltSize { "Tilt size", theme::accent };

    juce::TextButton reverseButton { "Reverse" };
    juce::TextButton mirrorButton { "Mirror" }, applyButton { "Apply to selection" }, removeButton { "Remove" };
    juce::Rectangle<int> panelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

} // namespace pl
