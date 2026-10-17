#pragma once
#include <juce_core/juce_core.h>
#include "FixtureType.h"

namespace pl
{

/** GDTF (General Device Type Format) import: a .gdtf file is a zip with a
    description.xml. Each DMX mode becomes a Phase fixture type. GDTF attributes
    are mapped onto Phase attributes; repeated geometries (pixels) become cells. */
namespace gdtf
{
    struct ModeInfo
    {
        juce::String name;
        int footprint = 0;
        int cells = 0;
    };

    struct Description
    {
        juce::String manufacturer, name, fixtureTypeId;
        std::vector<ModeInfo> modes;
        std::shared_ptr<juce::XmlElement> xml;      // the parsed description.xml
    };

    /** Reads a .gdtf file (zip) or a bare description.xml. Empty string = OK. */
    juce::String read (const juce::MemoryBlock& data, Description& out);

    /** Builds a Phase fixture type from one DMX mode. Empty string = OK. */
    juce::String toFixtureType (const Description&, const juce::String& modeName, FixtureType& out);

    /** Phase attribute for a GDTF attribute name ("ColorAdd_R" → "ColorR"); empty if unknown. */
    juce::String mapAttribute (const juce::String& gdtfAttribute);
}

} // namespace pl
