#pragma once
#include "Widgets.h"

namespace pl
{

/** Creates a fresh instance of a named view ("Layout", "Presets", ...). */
using ViewFactory = std::function<std::unique_ptr<juce::Component> (const juce::String&)>;

/** Content of an extra screen: a split layout of panes, each showing any view. */
class ScreenContent : public juce::Component
{
public:
    ScreenContent (int screenNumber, ViewFactory);

    static const juce::StringArray& viewNames();
    static const juce::StringArray& layoutNames();

    void setLayout (int layoutIndex);
    void setPaneView (int pane, const juce::String& view);

    juce::var toVar() const;
    void fromVar (const juce::var&);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onChanged;      // layout or a pane changed (save settings)

private:
    int paneCount() const;
    void rebuildBar();

    const int number;
    ViewFactory factory;
    int layout = 1;                       // index into layoutNames()
    juce::StringArray paneViews;
    std::vector<std::unique_ptr<juce::Component>> panes;

    juce::Label title;
    juce::ComboBox layoutCombo;
    juce::OwnedArray<juce::ComboBox> paneCombos;
    juce::Rectangle<int> bar;
};

/** A separate window for an extra screen (drag it to another display). */
class ScreenWindow : public juce::DocumentWindow
{
public:
    ScreenWindow (int screenNumber, ViewFactory);

    void closeButtonPressed() override;
    ScreenContent& content() { return *contentComp; }
    int getNumber() const { return number; }

    std::function<void (ScreenWindow&)> onClose;

private:
    const int number;
    ScreenContent* contentComp;
};

} // namespace pl
