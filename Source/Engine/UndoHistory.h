#pragma once
#include "DmxEngine.h"

namespace pl
{

/** Undo / redo for the whole show and the programmer: patch, fixture types,
    executors and cues, presets, pixel maps, audio links, programmer values,
    effects and selection. Snapshots are taken when something changed (call
    capture() regularly, e.g. after edits or on a timer); undo restores the
    previous one without stopping running playback. */
class UndoHistory
{
public:
    explicit UndoHistory (DmxEngine& e) : engine (e) {}

    /** Adds a step if the show differs from the last snapshot. Returns true if added. */
    bool capture();

    bool undo();
    bool redo();
    bool canUndo() const { return past.size() > 1; }
    bool canRedo() const { return ! future.empty(); }
    int undoSteps() const { return juce::jmax (0, (int) past.size() - 1); }
    int redoSteps() const { return (int) future.size(); }

    /** Forget the history (new show / show loaded) and start from the current state. */
    void reset();

    static constexpr size_t maxSteps = 200;
    static constexpr size_t maxBytes = 96 * 1024 * 1024;

private:
    juce::String snapshot() const;
    void restore (const juce::String&);
    void trim();

    DmxEngine& engine;
    std::vector<juce::String> past;      // past.back() is the current state
    std::vector<juce::String> future;
};

} // namespace pl
