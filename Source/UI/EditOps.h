#pragma once
#include "Widgets.h"

namespace pl::editops
{
    using Item = UiState::EditItem;
    using Kind = Item::Kind;

    inline Item executor (int page, int slot)
    {
        return { Kind::Executor, page, slot, 0, "executor " + juce::String (page) + "." + juce::String (slot) };
    }
    inline Item preset (PresetPool pool, int number, const juce::String& name = {})
    {
        return { Kind::Preset, (int) pool, number, 0,
                 (name.isNotEmpty() ? name + " (" : juce::String()) + poolName (pool).trimCharactersAtEnd ("s") + " " + juce::String (number) + (name.isNotEmpty() ? ")" : "") };
    }
    inline Item cue (int page, int slot, int index, const juce::String& name)
    {
        return { Kind::Cue, page, slot, index, name.isNotEmpty() ? name : "cue " + juce::String (index + 1) };
    }

    /** Asks before something is deleted; `doIt` runs (on the message thread) after OK. */
    inline void confirmDelete (juce::Component* parent, const juce::String& what, std::function<void()> doIt, bool undoable = true)
    {
        auto* aw = new juce::AlertWindow ("Delete", "Delete " + what + "?" + (undoable ? "\nYou can undo with Cmd+Z." : ""),
                                          juce::MessageBoxIconType::WarningIcon, parent);
        aw->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([doIt] (int r) { if (r == 1 && doIt) doIt(); }), true);
    }

    /** A tap on something while Copy / Move / Delete is active. Returns true when the
        tap was used (the view should do nothing else). `exists`: the target has content. */
    inline bool tap (DmxEngine& engine, UiState& ui, const Item& target, bool exists, juce::Component* parent)
    {
        using M = UiState::EditMode;
        if (ui.editMode == M::None) return false;

        if (ui.editMode == M::Delete)
        {
            if (! exists) { ui.say ("Nothing to delete there", true); return true; }
            ui.setEditMode (M::None);
            confirmDelete (parent, target.label, [&engine, &ui, target]
            {
                {
                    const juce::ScopedLock sl (engine.dataLock);
                    if (target.kind == Kind::Executor) engine.playback.deleteExecutor (target.a, target.b);
                    if (target.kind == Kind::Preset)   engine.presets.remove ((PresetPool) target.a, target.b);
                    if (target.kind == Kind::Cue)      engine.playback.deleteCue (target.a, target.b, target.c);
                }
                engine.showChanged();
                ui.changed();
                ui.say ("Deleted " + target.label);
            });
            return true;
        }

        const bool move = ui.editMode == M::Move;
        const juce::String verb = move ? "Move" : "Copy";
        if (ui.editSource.kind == Kind::None)
        {
            if (! exists) { ui.say (verb + ": that one is empty - tap something to " + verb.toLowerCase(), true); return true; }
            ui.editSource = target;
            ui.say (verb + " " + target.label + " - now tap where to");
            ui.changed();
            return true;
        }

        const auto src = ui.editSource;
        if (src.kind != target.kind || (src.kind == Kind::Preset && src.a != target.a)
            || (src.kind == Kind::Cue && (src.a != target.a || src.b != target.b)))
        {
            ui.say (verb + ": tap a " + juce::String (src.kind == Kind::Executor ? "fader / executor"
                                                      : src.kind == Kind::Preset ? "tile in the same pool" : "cue in the same list"), true);
            return true;
        }

        bool ok = false;
        {
            const juce::ScopedLock sl (engine.dataLock);
            if (src.kind == Kind::Executor) ok = engine.playback.copyExecutor (src.a, src.b, target.a, target.b, move);
            if (src.kind == Kind::Preset)
            {
                ok = engine.presets.copy ((PresetPool) src.a, src.b, target.b, move);
                if (ok && move) engine.playback.remapPresetReferences (poolKey ((PresetPool) src.a), src.b, target.b, true);
            }
            if (src.kind == Kind::Cue) ok = engine.playback.copyCue (src.a, src.b, src.c, target.c, move);
        }
        ui.setEditMode (M::None);
        engine.showChanged();
        ui.say (ok ? (move ? "Moved " : "Copied ") + src.label + (src.kind == Kind::Cue ? " after " : " to ") + target.label
                   : verb + " didn't work", ! ok);
        return true;
    }
}
