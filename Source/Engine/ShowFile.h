#pragma once
#include "DmxEngine.h"

namespace pl::showfile
{
    constexpr int formatVersion = 6;
    inline constexpr const char* extension = ".phase";

    juce::var toVar (DmxEngine&);

    /** Replaces the engine's show data. Returns an empty string on success. */
    juce::String fromVar (DmxEngine&, const juce::var&);

    juce::String save (DmxEngine&, const juce::File&);
    juce::String load (DmxEngine&, const juce::File&);

    /** Clears patch, user fixture types, executors and programmer. */
    void newShow (DmxEngine&);

    // --- fixture library ------------------------------------------------------
    /** Machine fixture library: every custom / imported type is kept here too, so
        it's available in every show. */
    juce::File machineLibraryFile();
    void saveMachineLibrary (DmxEngine&);
    /** Adds machine library types the show doesn't have yet. Returns how many. */
    int mergeMachineLibrary (DmxEngine&);

    /** Library file (.phaselib) with the given types. */
    juce::String exportLibrary (const std::vector<FixtureType>&, const juce::File&);

    /** Imports fixture types from a .phaselib, a .phase show, a .gdtf (all modes)
        or a folder of those (searched recursively). Adds the count to `added`. */
    juce::String importLibrary (DmxEngine&, const juce::File&, int& added);
}
