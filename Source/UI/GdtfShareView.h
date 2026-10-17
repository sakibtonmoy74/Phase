#pragma once
#include "Widgets.h"
#include "Control/GdtfShare.h"

namespace pl
{

/** Turns GDTF file data into Phase fixture types: asks which DMX mode(s) to
    add (menu shown at `target`), then reports with `done` (message, isError). */
void importGdtfData (DmxEngine&, const juce::MemoryBlock& data, juce::Component* target,
                     std::function<void (const juce::String&, bool)> done);

/** Browse GDTF Share (gdtf-share.com) with a GDTF Share account: log in, search
    the fixture list, download and import fixture types. */
class GdtfShareView : public juce::Component,
                      private juce::TableListBoxModel
{
public:
    explicit GdtfShareView (DmxEngine&);
    ~GdtfShareView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int columnId, bool forwards) override;

    void doLogin();
    void refreshList();
    void applyFilter();
    void importSelected();
    void setStatus (const juce::String&, bool error = false);
    static juce::File cacheFile();

    DmxEngine& engine;
    std::shared_ptr<GdtfShare> client;           // shared with background jobs
    std::vector<GdtfShare::Entry> all, shown;
    bool busy = false;

    juce::Label title, userLabel, passLabel, status, searchLabel;
    juce::TextEditor userEdit, passEdit, searchEdit;
    juce::TextButton loginButton { "Log in" }, refreshButton { "Refresh list" }, importButton { "Import selected" },
                     signupButton { "Create account..." };
    juce::TableListBox table { "GDTF Share", this };

    JUCE_DECLARE_WEAK_REFERENCEABLE (GdtfShareView)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GdtfShareView)
};

} // namespace pl
