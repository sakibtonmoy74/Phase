#include "ScreenWindow.h"

namespace pl
{

const juce::StringArray& ScreenContent::viewNames()
{
    static const juce::StringArray v { "Layout", "3D", "Presets", "Cues", "Patch", "Effects", "Executors",
                                       "Audio", "MIDI", "DMX", "Network", "DJ Link" };
    return v;
}

const juce::StringArray& ScreenContent::layoutNames()
{
    static const juce::StringArray l { "1 pane", "2 side by side", "2 stacked", "3 (1 + 2)", "4 grid" };
    return l;
}

ScreenContent::ScreenContent (int screenNumber, ViewFactory f) : number (screenNumber), factory (std::move (f))
{
    title.setText ("SCREEN " + juce::String (number), juce::dontSendNotification);
    title.setFont (theme::font (14.0f, true));
    title.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (title);

    for (int i = 0; i < layoutNames().size(); ++i) layoutCombo.addItem (layoutNames()[i], i + 1);
    layoutCombo.setTooltip ("How this screen is split");
    layoutCombo.onChange = [this] { setLayout (layoutCombo.getSelectedItemIndex()); };
    addAndMakeVisible (layoutCombo);

    paneViews = { "Presets", "Layout", "Cues", "Patch" };
    setLayout (1);
}

int ScreenContent::paneCount() const
{
    switch (layout)
    {
        case 0:  return 1;
        case 1:
        case 2:  return 2;
        case 3:  return 3;
        default: return 4;
    }
}

void ScreenContent::setLayout (int index)
{
    layout = juce::jlimit (0, layoutNames().size() - 1, index);
    layoutCombo.setSelectedItemIndex (layout, juce::dontSendNotification);
    while (paneViews.size() < 4) paneViews.add ("Layout");

    panes.resize ((size_t) paneCount());
    for (int i = 0; i < paneCount(); ++i)
        setPaneView (i, paneViews[i]);
    rebuildBar();
    resized();
    if (onChanged) onChanged();
}

void ScreenContent::setPaneView (int pane, const juce::String& view)
{
    if (pane < 0 || pane >= paneCount()) return;
    paneViews.set (pane, view);
    if (panes[(size_t) pane] != nullptr)
        removeChildComponent (panes[(size_t) pane].get());
    panes[(size_t) pane] = factory (view);
    if (panes[(size_t) pane] != nullptr)
        addAndMakeVisible (panes[(size_t) pane].get());
    resized();
    if (onChanged) onChanged();
}

void ScreenContent::rebuildBar()
{
    for (auto* c : paneCombos) removeChildComponent (c);
    paneCombos.clear();
    for (int i = 0; i < paneCount(); ++i)
    {
        auto* cb = paneCombos.add (new juce::ComboBox());
        for (int v = 0; v < viewNames().size(); ++v) cb->addItem (viewNames()[v], v + 1);
        cb->setSelectedItemIndex (viewNames().indexOf (paneViews[i]), juce::dontSendNotification);
        cb->setTooltip ("What pane " + juce::String (i + 1) + " shows");
        cb->onChange = [this, i, cb] { setPaneView (i, viewNames()[cb->getSelectedItemIndex()]); };
        addAndMakeVisible (cb);
    }
}

juce::var ScreenContent::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("layout", layout);
    juce::Array<juce::var> p;
    for (auto& v : paneViews) p.add (v);
    o->setProperty ("panes", p);
    return juce::var (o);
}

void ScreenContent::fromVar (const juce::var& v)
{
    if (auto* p = v["panes"].getArray())
    {
        paneViews.clear();
        for (auto& x : *p)
            paneViews.add (viewNames().contains (x.toString()) ? x.toString() : juce::String ("Layout"));
    }
    setLayout ((int) v.getProperty ("layout", 1));
}

void ScreenContent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::panel);
    g.fillRect (bar);
    g.setColour (theme::line);
    g.drawHorizontalLine (bar.getBottom() - 1, 0.0f, (float) getWidth());
}

void ScreenContent::resized()
{
    auto r = getLocalBounds();
    bar = r.removeFromTop (44);
    auto b = bar.reduced (theme::gap, 6);
    title.setBounds (b.removeFromLeft (100));
    layoutCombo.setBounds (b.removeFromLeft (170));
    b.removeFromLeft (16);
    for (int i = 0; i < paneCombos.size(); ++i)
    {
        paneCombos[i]->setBounds (b.removeFromLeft (140));
        b.removeFromLeft (6);
    }

    std::vector<juce::Rectangle<int>> areas;
    switch (layout)
    {
        case 0: areas = { r }; break;
        case 1: { auto a = r; areas = { a.removeFromLeft (r.getWidth() / 2), a }; break; }
        case 2: { auto a = r; areas = { a.removeFromTop (r.getHeight() / 2), a }; break; }
        case 3:
        {
            auto a = r;
            auto left = a.removeFromLeft (r.getWidth() * 3 / 5);
            areas = { left, a.removeFromTop (a.getHeight() / 2), a };
            break;
        }
        default:
        {
            auto top = r.withHeight (r.getHeight() / 2), bottom = r.withTrimmedTop (r.getHeight() / 2);
            areas = { top.withWidth (r.getWidth() / 2), top.withTrimmedLeft (r.getWidth() / 2),
                      bottom.withWidth (r.getWidth() / 2), bottom.withTrimmedLeft (r.getWidth() / 2) };
        }
    }
    for (size_t i = 0; i < panes.size() && i < areas.size(); ++i)
        if (panes[i] != nullptr) panes[i]->setBounds (areas[i]);
}

//==============================================================================
ScreenWindow::ScreenWindow (int screenNumber, ViewFactory f)
    : DocumentWindow ("Phase - Screen " + juce::String (screenNumber), theme::bg, DocumentWindow::allButtons),
      number (screenNumber)
{
    setUsingNativeTitleBar (true);
    contentComp = new ScreenContent (screenNumber, std::move (f));
    setContentOwned (contentComp, false);
    setResizable (true, true);
    setResizeLimits (640, 400, 10000, 10000);
    centreWithSize (1280, 800);
}

void ScreenWindow::closeButtonPressed()
{
    if (onClose) onClose (*this);
}

} // namespace pl
