#pragma once
#include "Widgets.h"

namespace pl
{

/** Edits the folders of one cue: each folder is a group (or fixture list) with a
    dimmer and preset references - the cue plays whatever the presets hold now,
    on whatever fixtures the group has now. */
class CueFoldersEditor : public juce::Component,
                         private juce::Timer
{
public:
    CueFoldersEditor (DmxEngine& e, int page, int slot, int cue) : engine (e), page_ (page), slot_ (slot), cueIndex (cue)
    {
        ui::styleHeader (title, "FOLDERS");
        addAndMakeVisible (title);
        hint.setText ("A folder plays its presets on its group. Update a preset or a group and every cue that uses it follows. "
                      "Storing a cue after recalling a group and presets makes folders automatically.",
                      juce::dontSendNotification);
        hint.setFont (theme::font (12.5f));
        hint.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (hint);
        addSelection.onClick = [this] { addFolder (false); };
        addGroup.onClick = [this] { addFolder (true); };
        addSelection.setTooltip ("New folder with the fixtures selected now");
        addGroup.setTooltip ("New folder on a group (live: change the group, the cue follows)");
        addAndMakeVisible (addSelection);
        addAndMakeVisible (addGroup);
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        load();
        startTimerHz (2);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (theme::gap * 2);
        auto top = r.removeFromTop (theme::buttonH - 4);
        title.setBounds (top.removeFromLeft (120));
        addGroup.setBounds (top.removeFromRight (170));
        top.removeFromRight (8);
        addSelection.setBounds (top.removeFromRight (200));
        r.removeFromTop (6);
        hint.setBounds (r.removeFromTop (36));
        r.removeFromTop (6);
        viewport.setBounds (r);
        layoutRows();
    }

private:
    static const juce::StringArray& poolKeys()
    {
        static const juce::StringArray k { "dimmer", "position", "gobo", "color", "beam", "focus", "effects", "all" };
        return k;
    }

    struct Row : public juce::Component
    {
        Row (CueFoldersEditor& o, int i) : owner (o), index (i)
        {
            ui::styleEditor (name, false);
            name.onReturnKey = [this] { push(); };
            name.onFocusLost = [this] { push(); };
            addAndMakeVisible (name);
            group.onChange = [this] { push(); };
            group.setTooltip ("The folder's fixtures: a group (live) or the fixtures it was made with");
            addAndMakeVisible (group);
            useSel.setTooltip ("Set the folder's fixtures to the current selection");
            useSel.onClick = [this] { useSelection(); };
            addAndMakeVisible (useSel);
            dimOn.setTooltip ("The folder sets the dimmer");
            dimOn.onClick = [this] { push(); };
            addAndMakeVisible (dimOn);
            dim.setSliderStyle (juce::Slider::LinearBar);
            dim.setRange (0.0, 100.0, 1.0);
            dim.setTextValueSuffix ("%");
            dim.onValueChange = [this] { push(); };
            addAndMakeVisible (dim);
            for (int k = 0; k < poolKeys().size(); ++k)
            {
                auto* cb = refs.add (new juce::ComboBox());
                cb->setTooltip (poolKeys()[k] + " preset this folder plays");
                cb->onChange = [this] { push(); };
                addAndMakeVisible (cb);
                auto* l = refLabels.add (new juce::Label ({}, poolKeys()[k].substring (0, 1).toUpperCase() + poolKeys()[k].substring (1)));
                l->setFont (theme::font (11.0f));
                l->setColour (juce::Label::textColourId, theme::textDim);
                addAndMakeVisible (l);
            }
            remove.setButtonText (juce::CharPointer_UTF8 ("\xe2\x9c\x95"));
            remove.onClick = [this] { owner.removeFolder (index); };
            addAndMakeVisible (remove);
        }

        void fill (const CuePart& p, const PresetLibrary& presets, int fixtureCount)
        {
            loading = true;
            if (! name.hasKeyboardFocus (true)) name.setText (p.name, false);
            group.clear (juce::dontSendNotification);
            groupNumbers.clear();
            group.addItem (juce::String (fixtureCount) + " fixtures (fixed list)", 1);
            groupNumbers.push_back (0);
            for (auto& [n, g] : presets.pool (PresetPool::Group))
            {
                group.addItem ("Group " + juce::String (n) + "  " + g.name, (int) groupNumbers.size() + 1);
                groupNumbers.push_back (n);
            }
            for (size_t i = 0; i < groupNumbers.size(); ++i)
                if (groupNumbers[i] == p.group) group.setSelectedItemIndex ((int) i, juce::dontSendNotification);

            auto it = p.values.find ("Dimmer");
            dimOn.setToggleState (it != p.values.end(), juce::dontSendNotification);
            dim.setValue (it != p.values.end() ? it->second * 100.0 : 100.0, juce::dontSendNotification);
            dim.setEnabled (dimOn.getToggleState());

            for (int k = 0; k < poolKeys().size(); ++k)
            {
                auto* cb = refs[k];
                cb->clear (juce::dontSendNotification);
                cb->addItem ("-", 1);
                std::vector<int> nums { 0 };
                if (auto pool = poolFromKey (poolKeys()[k]))
                    for (auto& [n, pr] : presets.pool (*pool))
                    {
                        cb->addItem (juce::String (n) + " " + pr.name, (int) nums.size() + 1);
                        nums.push_back (n);
                    }
                refNumbers[k] = nums;
                const auto ref = p.presets.find (poolKeys()[k]);
                int sel = 0;
                if (ref != p.presets.end())
                    for (size_t i = 0; i < nums.size(); ++i) if (nums[i] == ref->second) sel = (int) i;
                cb->setSelectedItemIndex (sel, juce::dontSendNotification);
            }
            loading = false;
        }

        void push()
        {
            if (loading) return;
            dim.setEnabled (dimOn.getToggleState());
            owner.modify (index, [this] (CuePart& p)
            {
                p.name = name.getText().trim();
                const int g = group.getSelectedItemIndex();
                p.group = g >= 0 && g < (int) groupNumbers.size() ? groupNumbers[(size_t) g] : 0;
                if (dimOn.getToggleState()) p.values["Dimmer"] = (float) dim.getValue() / 100.0f;
                else p.values.erase ("Dimmer");
                for (int k = 0; k < poolKeys().size(); ++k)
                {
                    const int i = refs[k]->getSelectedItemIndex();
                    const auto& nums = refNumbers[k];
                    if (i > 0 && i < (int) nums.size()) p.presets[poolKeys()[k]] = nums[(size_t) i];
                    else p.presets.erase (poolKeys()[k]);
                }
            });
        }

        void useSelection()
        {
            std::vector<int> sel;
            { const juce::ScopedLock sl (owner.engine.dataLock); sel = owner.engine.programmer.selection(); }
            if (sel.empty()) return;
            owner.modify (index, [sel] (CuePart& p) { p.group = 0; p.fixtures = sel; });
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::panel2.withAlpha (0.5f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0, 3), 6.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 8);
            auto top = r.removeFromTop (28);
            remove.setBounds (top.removeFromRight (34));
            top.removeFromRight (6);
            name.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth() / 4)));
            top.removeFromLeft (6);
            useSel.setBounds (top.removeFromRight (110));
            top.removeFromRight (6);
            dim.setBounds (top.removeFromRight (110));
            dimOn.setBounds (top.removeFromRight (70));
            top.removeFromRight (6);
            group.setBounds (top);
            r.removeFromTop (6);
            const int w = r.getWidth() / juce::jmax (1, refs.size());
            for (int k = 0; k < refs.size(); ++k)
            {
                auto cell = r.removeFromLeft (w).reduced (2, 0);
                refLabels[k]->setBounds (cell.removeFromTop (14));
                refs[k]->setBounds (cell.removeFromTop (26));
            }
        }

        CueFoldersEditor& owner;
        const int index;
        bool loading = false;
        juce::TextEditor name;
        juce::ComboBox group;
        std::vector<int> groupNumbers;
        juce::TextButton useSel { "Use selection" }, remove;
        juce::ToggleButton dimOn { "Dim" };
        juce::Slider dim;
        juce::OwnedArray<juce::ComboBox> refs;
        juce::OwnedArray<juce::Label> refLabels;
        std::map<int, std::vector<int>> refNumbers;
    };

    void timerCallback() override
    {
        // keep up with preset / group changes made elsewhere (unless the user is in a field)
        for (auto* r : rows) if (r->name.hasKeyboardFocus (true)) return;
        auto* top = juce::Component::getCurrentlyModalComponent();
        if (top != nullptr) return;
        std::vector<CuePart> now;
        { const juce::ScopedLock sl (engine.dataLock); now = currentParts(); }
        if (now != shown) load();
    }

    std::vector<CuePart> currentParts() const
    {
        auto* e = engine.playback.find (page_, slot_);
        if (e == nullptr || cueIndex >= (int) e->cues.size()) return {};
        return e->cues[(size_t) cueIndex].parts;
    }

    void load()
    {
        std::vector<CuePart> parts;
        PresetLibrary presets;
        {
            const juce::ScopedLock sl (engine.dataLock);
            parts = currentParts();
            presets = engine.presets;
        }
        shown = parts;
        if ((int) parts.size() != rows.size())
        {
            rows.clear();
            for (int i = 0; i < (int) parts.size(); ++i) holder.addAndMakeVisible (rows.add (new Row (*this, i)));
            layoutRows();
        }
        for (int i = 0; i < rows.size(); ++i)
        {
            int count = (int) parts[(size_t) i].fixtures.size();
            if (parts[(size_t) i].group > 0)
                if (auto* g = presets.find (PresetPool::Group, parts[(size_t) i].group)) count = (int) g->fixtures.size();
            rows[i]->fill (parts[(size_t) i], presets, count);
        }
    }

    void layoutRows()
    {
        const int h = 90, w = viewport.getMaximumVisibleWidth();
        holder.setSize (w, juce::jmax (1, rows.size() * h));
        for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * h, w, h);
    }

    void modify (int index, std::function<void (CuePart&)> fn)
    {
        {
            const juce::ScopedLock sl (engine.dataLock);
            auto parts = currentParts();
            if (index < 0 || index >= (int) parts.size()) return;
            fn (parts[(size_t) index]);
            engine.playback.setCueParts (page_, slot_, cueIndex, parts);
            shown = parts;
        }
        engine.showChanged();
    }

    void removeFolder (int index)
    {
        {
            const juce::ScopedLock sl (engine.dataLock);
            auto parts = currentParts();
            if (index < 0 || index >= (int) parts.size()) return;
            parts.erase (parts.begin() + index);
            engine.playback.setCueParts (page_, slot_, cueIndex, parts);
        }
        load();
        engine.showChanged();
    }

    void addFolder (bool onGroup)
    {
        CuePart p;
        {
            const juce::ScopedLock sl (engine.dataLock);
            if (onGroup)
            {
                const auto& groups = engine.presets.pool (PresetPool::Group);
                if (groups.empty()) return;
                p.group = groups.begin()->first;
                p.name = groups.begin()->second.name;
            }
            else
            {
                p.fixtures = engine.programmer.selection();
                if (p.fixtures.empty()) return;
                p.name = juce::String ((int) p.fixtures.size()) + " fixtures";
            }
            p.values["Dimmer"] = 1.0f;
            auto parts = currentParts();
            parts.push_back (p);
            engine.playback.setCueParts (page_, slot_, cueIndex, parts);
        }
        load();
        engine.showChanged();
    }

    friend struct Row;
    DmxEngine& engine;
    const int page_, slot_, cueIndex;
    std::vector<CuePart> shown;
    juce::Label title, hint;
    juce::TextButton addSelection { "+ Folder from selection" }, addGroup { "+ Folder on a group" };
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Row> rows;
};

} // namespace pl
