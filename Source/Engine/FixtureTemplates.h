#pragma once
#include "FixtureType.h"

namespace pl
{

/** Sensible default (raw DMX 0-255) for an attribute: Pan / Tilt centred,
    shutter open, zoom and focus mid, everything else 0. */
int suggestedDefault (const juce::String& attribute);

/** Cell layouts for pixel fixtures (positions in cell units, DMX order). */
namespace cellLayouts
{
    std::vector<std::pair<float, float>> grid (int cellsX, int cellsY);
    /** Rows that go back and forth (zig-zag wired matrices). */
    std::vector<std::pair<float, float>> snake (int cellsX, int cellsY);
    /** Column by column, top to bottom. */
    std::vector<std::pair<float, float>> columns (int cellsX, int cellsY);
    /** A ring, starting at the top, clockwise (or anticlockwise). Rings > 1 nest inside each other. */
    std::vector<std::pair<float, float>> ring (int count, bool clockwise = true, int rings = 1);
    /** Nested rings with a pixel count each, outer ring first (a ring of 1 is the centre).
        DMX order runs ring by ring, each from the top. */
    std::vector<std::pair<float, float>> rings (const std::vector<int>& perRing, bool clockwise = true);
}

/** Starting points for the Fixture Builder ("what kind of fixture?"). */
namespace templates
{
    struct Info
    {
        juce::String id, name, description;
        bool askCells = false;        // needs a pixel count (bars, panels)
        bool grid = false;            // cells across and down (panels)
    };

    const std::vector<Info>& all();

    /** Builds the template, numbered and with good defaults. `cellsX` / `cellsY`
        for bars and panels. The type gets a fresh user id. */
    FixtureType make (const juce::String& templateId, int cellsX = 8, int cellsY = 1);
}

} // namespace pl
