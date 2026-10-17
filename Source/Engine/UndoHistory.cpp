#include "UndoHistory.h"

namespace pl
{

juce::String UndoHistory::snapshot() const
{
    auto* root = new juce::DynamicObject();
    const juce::ScopedLock sl (engine.dataLock);
    juce::Array<juce::var> types;
    for (auto& t : engine.library.types())
        if (! t.builtIn) types.add (t.toVar());
    root->setProperty ("fixtureTypes", types);
    root->setProperty ("patch", engine.patch.toVar());
    root->setProperty ("executors", engine.playback.toVar (false));
    root->setProperty ("presets", engine.presets.toVar());
    juce::Array<juce::var> links, maps;
    for (auto& l : engine.audioLinks) links.add (l.toVar());
    for (auto& m : engine.pixelMaps) maps.add (m.toVar());
    root->setProperty ("audioLinks", links);
    root->setProperty ("pixelMaps", maps);
    root->setProperty ("programmer", engine.programmer.toVar());
    return juce::JSON::toString (juce::var (root), true);
}

void UndoHistory::restore (const juce::String& text)
{
    const auto v = juce::JSON::parse (text);
    if (! v.isObject()) return;
    const juce::ScopedLock sl (engine.dataLock);
    engine.library.clearUserTypes();
    if (auto* types = v["fixtureTypes"].getArray())
        for (auto& t : *types) engine.library.addOrReplace (FixtureType::fromVar (t));
    engine.patch.fromVar (v["patch"]);
    engine.playback.restoreExecutors (v["executors"], DmxEngine::now());
    engine.presets.fromVar (v["presets"]);
    engine.audioLinks.clear();
    if (auto* links = v["audioLinks"].getArray())
        for (auto& l : *links) engine.audioLinks.push_back (AudioLink::fromVar (l));
    engine.pixelMaps.clear();
    if (auto* maps = v["pixelMaps"].getArray())
        for (auto& m : *maps) engine.pixelMaps.push_back (PixelMap::fromVar (m));
    engine.programmer.fromVar (v["programmer"]);
}

bool UndoHistory::capture()
{
    auto now = snapshot();
    if (! past.empty() && past.back() == now) return false;
    past.push_back (std::move (now));
    future.clear();
    trim();
    return past.size() > 1;
}

bool UndoHistory::undo()
{
    capture();                                   // keep anything not yet recorded
    if (past.size() < 2) return false;
    future.push_back (past.back());
    past.pop_back();
    restore (past.back());
    return true;
}

bool UndoHistory::redo()
{
    if (future.empty()) return false;
    past.push_back (future.back());
    future.pop_back();
    restore (past.back());
    return true;
}

void UndoHistory::reset()
{
    past.clear();
    future.clear();
    past.push_back (snapshot());
}

void UndoHistory::trim()
{
    size_t bytes = 0;
    for (auto& s : past) bytes += (size_t) s.getNumBytesAsUTF8();
    while (past.size() > 2 && (past.size() > maxSteps || bytes > maxBytes))
    {
        bytes -= (size_t) past.front().getNumBytesAsUTF8();
        past.erase (past.begin());
    }
}

} // namespace pl
