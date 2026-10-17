#pragma once
#include "Widgets.h"

namespace pl
{

/** Stage layout: fixtures placed like the real rig, showing live intensity and
    colour. Click to select, drag a lasso to select several: the lasso orders
    the selection along the drag direction (left→right, right→left, etc.), which
    sets the phase order for effects.

    - Edit layout: drag fixtures, plus tools to arrange, rotate, flip, align,
      distribute and space the selection.
    - Pixels: click / lasso selects single cells of pixel fixtures.
    - Pixel map: draw maps fed by video, test patterns or effects. */
class LayoutView : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    LayoutView (DmxEngine&, UiState&);
    ~LayoutView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    void selectAll();

private:
    struct Item
    {
        int fid = 0; juce::String name; juce::Point<float> pos; bool hasPanTilt = false; bool saved = false;
        int cellsX = 1, cellsY = 1;           // pixel fixtures draw as a grid of cells...
        std::vector<juce::Point<float>> base; // ...or any shape: each cell's offset before rotation (layout units)
        bool custom = false;                  // custom pixel layout (drawn as round pixels)
        float extentW = 1.0f, extentH = 1.0f; // size in cells (for auto placement)
        float rotation = 0.0f;                // degrees clockwise
        bool mirror = false;

        bool hasCells() const  { return base.size() > 1; }
        int cellCount() const  { return (int) base.size(); }

        /** Offset of a cell (1-based) from the fixture centre, in layout units. */
        juce::Point<float> cellOffset (int cell) const;
    };

    /** Screen rectangle around a fixture's body. */
    juce::Rectangle<float> bodyOf (const Item&) const;
    juce::Point<float> cellCentre (const Item&, int cell) const;
    int cellAt (const Item&, juce::Point<float> screen) const;     // 0 = none

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildItems();
    int fixtureAt (juce::Point<float> screen) const;
    int targetAt (juce::Point<float> screen) const;                // pixel in Pixels mode
    juce::Point<float> toScreen (juce::Point<float> layout) const;
    juce::Point<float> toLayout (juce::Point<float> screen) const;
    juce::Rectangle<int> canvasArea() const;
    bool editing() const { return editButton.getToggleState(); }

    void paintFixture (juce::Graphics&, const Item&, const std::vector<int>& selection);

    // edit tools (act on the selected fixtures)
    std::vector<int> selectedFixtures() const;
    Item* itemFor (int fid);
    void arrange (const juce::String& how);
    void rotateSelection (float degrees);
    void flipSelection (bool horizontal);
    void alignSelection (const juce::String& how);
    void distributeSelection (bool horizontal);
    void scaleSpacing (float factor);
    void autoPlaceAll (bool onlyMissing);
    void fitToView();
    void commitPositions (const std::vector<int>& fids);

    // group buttons (the Group pool, shown as a row of buttons above the rig)
    class GroupButton;
    juce::OwnedArray<GroupButton> groupButtons;
    juce::TextButton newGroupButton { "+ Group" };
    juce::String groupSignature;
    void rebuildGroupButtons();
    void groupTapped (int number, bool popup, juce::Component* button);
    void createGroupFromSelection();
    void canvasMenu();
    void deleteFixturesMenu (std::vector<int> fids);
    juce::Rectangle<int> groupRow() const;

    DmxEngine& engine;
    UiState& ui;
    std::vector<Item> items;

    juce::TextButton editButton { "Edit layout" }, fitButton { "Fit" }, allButton { "All" },
                     orderButton { "Order: Drag" }, sortButton { "Sort by FID" },
                     pixelsButton { "Pixels" }, mapButton { "Pixel map" };
    juce::OwnedArray<juce::TextButton> editTools;
    juce::Label hint;

    float zoom = 1.0f;
    int timerTicks = 0;
    bool fittedOnce = false;
    juce::Point<float> origin { 20.0f, 20.0f };

    enum class DragMode { None, Lasso, Move, Pan, NewMap, MoveMap, ResizeMap } dragMode = DragMode::None;

    // pixel maps
    class MapPanel;
    std::unique_ptr<MapPanel> mapPanel;
    int selectedMap = 0;                       // map id, 0 = none
    PixelMap mapStart;
    juce::Rectangle<float> mapScreenRect (const PixelMap&) const;
    int mapAt (juce::Point<float> screen, bool& onResizeHandle) const;
    void selectMap (int id);
    void paintMaps (juce::Graphics&);
    juce::Point<float> dragStart, dragNow;
    std::map<int, juce::Point<float>> moveStart;
    bool lassoAdd = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutView)
};

} // namespace pl
