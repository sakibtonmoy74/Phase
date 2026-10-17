#pragma once
#include "Theme.h"
#include "Widgets.h"
#include "Engine/DmxEngine.h"

namespace pl
{

/** Art-Net: a list of outputs (interface, broadcast or node IP, which universes,
    the node's Art-Net start), node discovery, and an Art-Net monitor that shows
    what is on the wire (any universe, any source, packet rate and values). */
class NetworkView : public juce::Component,
                    private juce::Timer
{
public:
    explicit NetworkView (DmxEngine&);
    ~NetworkView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void loadFromEngine();
    void pushSettings (const NetSettings&);
    void addOutput (ArtNetOutput);
    void rebuildRows();
    void layoutRows();
    void refreshNodes();
    void nodeMenu (int index, juce::Component* target);

    DmxEngine& engine;
    ArtNetDiscovery discovery;
    ArtNetMonitor monitor;
    juce::StringArray interfaces;          // local IPv4 addresses
    NetSettings shown;                     // what the rows show

    // outputs
    class OutputRow;
    juce::Label header, rateLabel, status, columns;
    juce::ToggleButton enableToggle { "Output on" };
    juce::Slider rateSlider;
    juce::TextButton addButton { "+ Add output" }, refreshButton { "Refresh interfaces" };
    juce::Viewport rowsViewport;
    juce::Component rowsHolder;
    juce::OwnedArray<OutputRow> rows;

    // nodes
    juce::Label nodesHeader, nodesHint;
    juce::TextButton discoverButton { "Discover nodes" };
    juce::OwnedArray<juce::TextButton> nodeButtons;
    std::vector<artnet::NodeInfo> nodes;
    bool polling = false;

    // monitor
    class StreamList;
    class Grid;
    juce::Label monitorHeader, monitorInfo;
    juce::ComboBox listenCombo;
    juce::ToggleButton listenToggle { "Listen" };
    juce::TextButton clearButton { "Clear" };
    std::unique_ptr<StreamList> streamList;
    std::unique_ptr<Grid> grid;
    std::vector<ArtNetMonitor::Stream> streams;
    int selectedStream = 0;
    std::pair<juce::String, int> selectedKey { {}, -1 };

    juce::Rectangle<int> outputsPanel, nodesPanel, monitorPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetworkView)
};

} // namespace pl
