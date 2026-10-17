#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include "Playback.h"
#include "Programmer.h"
#include "Patch.h"

namespace pl
{

/** Preset pools, grandMA-style. */
enum class PresetPool { Group, Dimmer, Position, Gobo, Color, Beam, Focus, Effect, All };

const std::vector<PresetPool>& allPresetPools();

/** Colour of a set of values (RGB, RGBW or CMY), or 0 when it has no colour. */
juce::uint32 colourOfValues (const std::map<juce::String, float>&);
juce::String poolName (PresetPool);
juce::String poolKey (PresetPool);                 // stable id for show files
std::optional<PresetPool> poolFromKey (const juce::String&);
/** Feature group stored by a pool (None for Group / Effect / All). */
Family poolFamily (PresetPool);

struct Preset
{
    juce::String name;
    ValueMap values;                                          // per fixture
    std::map<juce::String, std::map<juce::String, float>> byType;   // fixture type → values (applies to any fixture of that type)
    std::vector<EffectParams> effects;
    std::vector<int> fixtures;                                // fixtures stored, in selection order
    juce::uint32 swatch = 0;                                  // ARGB of the first fixture (0 = none)
    std::vector<juce::uint32> swatches;                       // colour of each fixture in order (gradients)
    juce::uint32 label = 0;                                   // user colour label for organising (0 = none)

    juce::var toVar() const;
    static Preset fromVar (const juce::var&);
};

enum class PresetStoreMode { Overwrite, Merge };

/** All preset pools. Not thread-safe: guard with the engine's dataLock. */
class PresetLibrary
{
public:
    const Preset* find (PresetPool, int number) const;
    const std::map<int, Preset>& pool (PresetPool) const;

    /** Stores from the programmer. Returns an empty string or an error. */
    juce::String store (PresetPool, int number, const Programmer&, const Patch&,
                        const FixtureLibrary&, PresetStoreMode);

    /** Applies a preset to the selection (or, with nothing selected, to the
        fixtures it was stored with, selecting them). Fixtures that weren't
        stored get values by their position in the selection, so a gradient
        stays a gradient on another group. Returns false if empty. */
    bool recall (PresetPool, int number, Programmer&, const Patch&, const FixtureLibrary&,
                 bool addToSelection = false) const;

    /** The values a preset gives one target (fixture or pixel) that is `index`-th of
        `count` targets: its own stored values, else by position, else by type. */
    std::map<juce::String, float> valuesFor (PresetPool, int number, int target, int index,
                                             const Patch&, const FixtureLibrary&) const;

    void rename (PresetPool, int number, const juce::String&);
    void setLabel (PresetPool, int number, juce::uint32 argb);

    /** Creates or updates a group by name (same name: same group, else the next
        free number). Returns the group number. */
    int setGroup (const juce::String& name, const std::vector<int>& fixtures);
    void remove (PresetPool, int number);
    /** Copy a preset to another number (overwrites); Move swaps the two. */
    bool copy (PresetPool, int from, int to, bool move);
    void clear() { pools.clear(); }

    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    std::map<PresetPool, std::map<int, Preset>> pools;
};

} // namespace pl
