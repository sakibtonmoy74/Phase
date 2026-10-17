#pragma once
#include <juce_core/juce_core.h>
#include "Attributes.h"

namespace pl
{

/** One attribute of a fixture and the DMX channels it uses.
    Channel numbers are 1-based inside the fixture's footprint;
    fine = 0 means 8-bit. */
struct AttributeDef
{
    juce::String attribute { "Dimmer" };
    int coarse = 1;
    int fine = 0;
    int defaultValue = 0;          // 0..255, raw DMX of the coarse channel
    int cell = 0;                  // 0 = whole fixture, 1..N = one cell of a multi-cell fixture
    int highlight = -1;            // 0..255 raw value for Highlight, -1 = automatic (see defaultHighlight)

    juce::String key() const { return cellKey (attribute, cell); }
};

/** Where an attribute lives inside a fixture's footprint (0-based offsets). */
struct AttributeSlots
{
    juce::String attribute;
    int coarse = -1;               // -1 for the virtual dimmer
    int fine = -1;                 // -1 when 8-bit
    float defaultNormalised = 0.0f;
    float highlightNormalised = -1.0f;   // < 0 = not touched by Highlight
};

struct FixtureType
{
    juce::String id;               // stable id, referenced by the patch
    juce::String manufacturer;
    juce::String name;
    bool builtIn = false;
    std::vector<AttributeDef> attributes;   // display order (drag to reorder)
    int extraChannels = 0;         // unused channels after the last assigned one
    float panRange = 540.0f, tiltRange = 270.0f;   // degrees, for the 3D view
    float beamMin = 12.0f, beamMax = 40.0f;         // beam angle (degrees) at zoom 0 / 1 (or fixed: min)
    int cellsX = 1, cellsY = 1;    // cell grid for pixel fixtures (cells numbered left→right, top→bottom)

    int cellCount() const { return juce::jmax (1, cellsX) * juce::jmax (1, cellsY); }
    bool hasCells() const { return cellCount() > 1; }

    /** Layout units between cell centres (the Layout view and pixel map use this). */
    static constexpr float cellSpacing = 16.0f;

    /** Custom cell positions (cell units, x right, y down, centred on the fixture),
        one per cell in DMX order. Empty = a plain grid, left to right, top to bottom. */
    std::vector<std::pair<float, float>> cellLayout;
    bool hasCustomLayout() const { return hasCells() && (int) cellLayout.size() == cellCount(); }

    /** Sets a custom layout (centred automatically). Pass {} to go back to the grid. */
    void setCellLayout (std::vector<std::pair<float, float>> positions);

    /** Offset of cell `cell` (1-based) from the fixture's layout position. */
    std::pair<float, float> cellOffset (int cell) const
    {
        if (hasCustomLayout())
        {
            const auto& p = cellLayout[(size_t) juce::jlimit (0, cellCount() - 1, cell - 1)];
            return { p.first * cellSpacing, p.second * cellSpacing };
        }
        const int cx = juce::jmax (1, cellsX), cy = juce::jmax (1, cellsY);
        const int i = juce::jlimit (0, cx * cy - 1, cell - 1);
        return { ((float) (i % cx) - (float) (cx - 1) * 0.5f) * cellSpacing,
                 ((float) (i / cx) - (float) (cy - 1) * 0.5f) * cellSpacing };
    }

    /** Makes cells 2..N copies of cell 1's attributes (then renumber channels). */
    void repeatCellOne();

    /** True when every cell has the same attributes as cell 1 (one pixel set). */
    bool cellsAreUniform() const;

    /** Adds a second (third...) set of pixels after the existing cells, e.g. separate
        white cells next to RGB ones: `count` cells with `attrs` each, their channels
        after the last used one. `sameSpots`: the new cells sit beside cells 1..count
        (offset a little), otherwise in a row under the fixture. */
    void addPixelSet (int count, const juce::StringArray& attrs, bool sameSpots);

    int highestChannel() const;
    int footprint() const { return highestChannel() + juce::jmax (0, extraChannels); }

    /** Attributes with 0-based slots, in display order. Adds a virtual
        "Dimmer" when the type has colour channels but no dimmer channel. */
    std::vector<AttributeSlots> attributeSlots() const;

    /** For each DMX channel (index 0 = channel 1): the attribute label using it,
        with " fine" for fine channels, or empty when unused. */
    juce::StringArray channelMap() const;

    /** Empty when valid, otherwise a description of the first problem. */
    juce::String validate() const;

    /** Renumbers channels in row order: each coarse, then its fine right after. */
    void autoNumber();

    bool hasRealDimmer() const;
    bool usesVirtualDimmer() const;
    /** A cell with colour channels but no dimmer channel gets a virtual "Dimmer@n"
        (full by default) so pixels can be dimmed one by one. */
    bool cellUsesVirtualDimmer (int cell) const;
    juce::String displayName() const;

    juce::var toVar() const;
    static FixtureType fromVar (const juce::var&);   // reads v1 and v2 formats
};

class FixtureLibrary
{
public:
    FixtureLibrary();

    const std::vector<FixtureType>& types() const { return list; }
    const FixtureType* find (const juce::String& id) const;
    FixtureType* findMutable (const juce::String& id);

    void addOrReplace (const FixtureType&);
    bool remove (const juce::String& id);
    juce::String duplicate (const juce::String& id);
    juce::String createEmpty();
    void clearUserTypes();

private:
    void addBuiltIns();
    std::vector<FixtureType> list;
};

} // namespace pl
