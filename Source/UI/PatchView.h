#pragma once
#include "Widgets.h"
#include <set>

namespace pl
{

/** Patch window: fixtures grouped in collapsible folders, plus "add fixtures"
    and "edit selected" panels. The Fixture Builder opens in its own window. */
class PatchView : public juce::Component,
                  private juce::TableListBoxModel,
                  private juce::ChangeListener
{
public:
    PatchView (DmxEngine&, UiState&);
    ~PatchView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void openFixtureBuilder();

private:
    struct Row
    {
        bool isFolder = false;
        juce::String folder;
        int fid = 0;
        int count = 0;
    };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void refreshFromEngine();
    void rebuildRows();
    void refreshTypeCombo();
    void suggestNextAddress();
    void addFixtures();
    void applyEdit();
    void deleteSelected();
    std::vector<int> selectedFids() const;
    void moveSelectedToFolder (const juce::String& folder);
    void showMoveMenu();
    void renameSelectedFolder();
    void showMessage (const juce::String&, bool isError);

    DmxEngine& engine;
    UiState& ui;
    std::vector<PatchedFixture> fixtures;   // copy for painting
    std::vector<Row> rows;
    std::set<juce::String> collapsed;
    std::vector<juce::String> typeIds;

    juce::TableListBox table { "Patch", this };
    juce::TextButton builderButton { "Fixture Builder..." }, newFolderButton { "New folder..." },
                     moveButton { "Move to folder" }, renameFolderButton { "Rename folder..." },
                     groupsButton { "Groups from folders" }, invertPanButton { "Invert Pan" }, invertTiltButton { "Invert Tilt" },
                     highlightButton { "Highlight" }, repatchButton { "Re-patch..." };
    void repatchSelected();
    void updatePatchHighlight();
    void toggleInvert (bool pan);

    // Add panel
    juce::Label addHeader, typeLabel, qtyLabel, fidLabel, uniLabel, addrLabel, nameLabel, folderLabel;
    juce::ComboBox typeCombo;
    juce::TextEditor qtyEdit, fidEdit, uniEdit, addrEdit, nameEdit, folderEdit;
    juce::TextButton nextAddrButton { "Next free" }, addButton { "Add fixtures" }, folderPick { "..." };

    // Edit panel
    juce::Label editHeader, eFidLabel, eNameLabel, eUniLabel, eAddrLabel;
    juce::TextEditor eFid, eName, eUni, eAddr;
    juce::TextButton applyButton { "Apply" }, deleteButton { "Delete" };
    juce::ToggleButton invertPanToggle { "Invert Pan" }, invertTiltToggle { "Invert Tilt" };
    void setInvert (bool pan, bool on);

    // type search
    juce::Label searchLabel;
    juce::TextEditor typeSearch;

    juce::Label message;
    juce::Rectangle<int> sidePanel;
    juce::Viewport sideViewport;
    juce::Component sideHolder;           // add + edit panels (scrolls on small screens)
    int editingFid = -1;

    juce::Component::SafePointer<juce::DialogWindow> builderWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchView)
};

} // namespace pl
