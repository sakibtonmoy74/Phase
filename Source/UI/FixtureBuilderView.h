#pragma once
#include "Widgets.h"

namespace pl
{

/** Fixture builder. One row per attribute with its DMX channel (and fine
    channel for 16-bit). Rows can be dragged to reorder. With "Keep channels in
    row order" on, channels renumber automatically; turn it off to type your own.
    The channel map shows what each DMX channel of the fixture carries. */
class FixtureBuilderView : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener
{
public:
    explicit FixtureBuilderView (DmxEngine&);
    ~FixtureBuilderView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    class AttrRow;
    class RowsHolder;
    class ChannelMap;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void refreshTypes();
    void loadSelectedType();
    void rebuildRows();
    void layoutRows();
    void modifyType (std::function<void (FixtureType&)> fn, bool renumber = true);

    friend class AttrRow;
    friend class RowsHolder;
    void setAttribute (int index, const AttributeDef&, bool manualChannel);
    void removeAttribute (int index);
    void dragRow (int index, int holderY);
    void finishDrag();

    DmxEngine& engine;
    std::vector<FixtureType> typesCopy;
    juce::String selectedId;
    FixtureType current;
    bool readOnly = true;
    bool ignoreCallbacks = false;

    juce::ListBox typeList { "Types", this };
    juce::TextButton newButton { "New" }, dupButton { "Duplicate" }, delButton { "Delete" },
                     gdtfFileButton { "Import GDTF..." }, gdtfShareButton { "GDTF Share..." },
                     libImportButton { "Import library..." }, libExportButton { "Export library..." };
    void showNewMenu();
    void createFromTemplate (const juce::String& templateId, int cellsX, int cellsY);
    void importLibrary();
    void exportLibrary();
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> shareWindow;
    juce::Label gdtfStatus;
    void importGdtfFile();
    void openGdtfShare();

    // auto-scroll while dragging a row near the top / bottom of the list
    juce::TimedCallback autoScroll { [this] { autoScrollTick(); } };
    void autoScrollTick();

    juce::Label header, mfrLabel, nameLabel, infoLabel, errorLabel, spareLabel;
    juce::TextEditor mfrEdit, nameEdit, spareEdit;
    juce::TextButton addButton { "+ Add attribute" }, autoButton { "Auto-number" };
    juce::ToggleButton keepOrderToggle { "Keep channels in row order" };
    juce::Label cellsLabel, cellsBy;
    juce::TextEditor cellsXEdit, cellsYEdit;
    juce::TextButton repeatButton { "Repeat cell 1 for all cells" }, layoutButton { "Pixel layout..." },
                     pixelSetButton { "+ Pixel set..." };
    void addPixelSetDialog();
    juce::Component::SafePointer<juce::DialogWindow> layoutWindow;
    void openCellLayout();

    std::unique_ptr<ChannelMap> channelMap;
    juce::Viewport rowsViewport;
    std::unique_ptr<RowsHolder> rowsHolder;
    juce::OwnedArray<AttrRow> attrRows;

    int dragIndex = -1, dropIndex = -1;
    juce::Rectangle<int> leftPanel, rightPanel, columnHeaderArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixtureBuilderView)
};

} // namespace pl
