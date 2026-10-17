#pragma once
#include "Widgets.h"
#include "Control/AudioInput.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace pl
{

/** Audio: pick the sound card input, watch the band meters, and link audio
    bands (level, bass, mid, high, beat) to any attribute on fixtures or groups. */
class AudioView : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    AudioView (DmxEngine&, UiState&, AudioInput&);
    ~AudioView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool followBeat() const { return followBeatToggle.getToggleState(); }

    class LinkRow;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildRows();
    void addLink (int groupNumber);
    void modifyLink (int id, std::function<void (AudioLink&)>);
    void removeLink (int id);

    DmxEngine& engine;
    UiState& ui;
    AudioInput& audio;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::Label inputHeader, status, linksHeader, linksHint;
    juce::Slider gainSlider;
    juce::Label gainLabel;
    juce::ToggleButton autoGainToggle { "Auto gain" }, followBeatToggle { "Beats tap the dimmer FX tempo" };
    juce::TextButton addSelectionButton { "+ Link selection" }, addGroupButton { "+ Link group" };

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<LinkRow> rows;
    std::vector<int> rowIds;

    juce::Rectangle<int> leftPanel, rightPanel, meterArea;
    float beatFlash = 0.0f;
    int lastBeatCount = 0;

    friend class LinkRow;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioView)
};

} // namespace pl
