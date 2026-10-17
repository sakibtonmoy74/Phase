#pragma once
#include "Widgets.h"

namespace pl
{

/** Preset pools as rows of tiles (Groups, Dimmer, Position, Gobo, Color, Beam,
    Focus, Effects, All). Tap a filled tile to recall it, tap an empty tile (or
    Store then a tile) to store. Right-click a tile to rename or delete. */
class PresetsView : public juce::Component,
                    private juce::ChangeListener
{
public:
    PresetsView (DmxEngine&, UiState&);
    ~PresetsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String&, bool isError)> onMessage;

    class Tile;
    class PoolRow;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    friend class Tile;
    void tileClicked (PresetPool, int number, const juce::ModifierKeys&);
    void tileMenu (PresetPool, int number, juce::Component* target);
    void store (PresetPool, int number, PresetStoreMode);
    void rename (PresetPool, int number);

    DmxEngine& engine;
    UiState& ui;

    juce::Label header, hint;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<PoolRow> rows;
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetsView)
};

} // namespace pl
