#pragma once
#include "Widgets.h"
#include "Control/ProDjLink.h"

namespace pl
{

/** Pro DJ Link decks: what each CDJ plays (title, artist, key, tempo, pitch, beat,
    time), its full-track overview and a scrolling colour waveform with the beat grid.
    The lights' Dimmer FX speed can follow the master deck's tempo. */
class DjLinkView : public juce::Component,
                   private juce::Timer
{
public:
    DjLinkView (ProDjLink&, DmxEngine&);
    ~DjLinkView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Loads the saved settings (machine settings) and starts the link if it was on. */
    static void restoreSettings (ProDjLink&);

private:
    void timerCallback() override;
    void push();
    void paintDeck (juce::Graphics&, juce::Rectangle<int>, const ProDjLink::Deck&, double now);

    ProDjLink& link;
    DmxEngine& engine;
    juce::Label header, ifaceLabel, numberLabel, status;
    juce::ToggleButton enableToggle { "Connect" }, followToggle { "Lights follow the master tempo" };
    juce::ComboBox ifaceCombo, numberCombo;
    juce::Slider zoomSlider;
    juce::Rectangle<int> barArea, decksArea;
    float lastFollowed = 0.0f;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DjLinkView)
};

} // namespace pl
