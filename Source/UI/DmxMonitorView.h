#pragma once
#include "Theme.h"
#include "Engine/DmxEngine.h"
#include <set>

namespace pl
{

/** Live view of the DMX output, one universe at a time (32 x 16 grid), and a DMX
    tester: select channels (click, drag, Shift / Cmd-click) and send raw values
    or a chase on top of the show, to check fixtures, cabling and addresses. */
class DmxMonitorView : public juce::Component,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit DmxMonitorView (DmxEngine&);
    ~DmxMonitorView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshUniverses();
    void refreshFixtureMap();

    DmxEngine& engine;
    juce::Label header, info;
    juce::ComboBox universeCombo;
    std::vector<int> universes;
    int shownUniverse = 1;

    DmxUniverse values {};
    std::array<int, 512> slotFid {};       // fixture starting at this slot (0 = none)
    std::array<int, 512> slotOwner {};     // fixture occupying this slot (0 = none)

    juce::Rectangle<int> gridArea, testPanel;

    // tester
    int channelAt (juce::Point<float>) const;          // 1-512, 0 = none
    void setLevel (int value);                         // selected channels -> tester value
    void release (bool all);
    void pushChase();
    void selectFixtureChannels();
    std::set<int> selected;                            // channels 1-512 of the shown universe
    int anchor = 0;
    std::set<int> dragBase;
    std::map<int, int> testValues;                     // shown universe: tester values (for drawing)
    juce::Label testHeader, levelLabel, testInfo;
    juce::Slider levelSlider, chaseSpeed;
    juce::TextButton fullButton { "Full" }, zeroButton { "0" }, halfButton { "50%" },
                     releaseButton { "Release" }, releaseAllButton { "Release all" },
                     fixtureButton { "Select fixture" }, allButton { "Select all 512" };
    juce::ToggleButton chaseToggle { "Chase through selection" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxMonitorView)
};

} // namespace pl
