#pragma once
#include "Programmer.h"
#include "Patch.h"
#include "Presets.h"

namespace pl
{

/** Keys a programmer value for `base` (e.g. "Dimmer", "ColorR") uses on a target:
    the whole fixture's attribute, every cell's copy, or one pixel's. */
std::vector<juce::String> programmerKeys (const Patch&, const FixtureLibrary&, int target, const juce::String& base);

/** Spread values across `count` targets: one value for all, or anchors that are
    interpolated in selection order ("0 thru 100", "100 thru 0 thru 100"). */
std::vector<float> spreadValues (const std::vector<float>& anchors, int count);

/** MA-style command line.
      1 thru 10           select fixtures 1-10 (in that order; 10 thru 1 reverses)
      1 + 5 + 7 thru 9    add to the list;  - 3  removes
      301.2 thru 301.6    pixels of a pixel fixture
      group 3             select group 3 (g 3)
      at 50               dimmer of the selection to 50 %  (at full, at 0)
      at 0 thru 100       spread across the selection in selection order
      1 thru 8 at 100 thru 0
      pan at 25 thru 75   any attribute: pan, tilt, zoom, focus, red, green, blue, white, amber, uv, iris...
      preset 4.2          recall a preset (1 dimmer, 2 position, 3 gobo, 4 color, 5 beam, 6 focus, 7 effect, 8 all)
      highlight / clear   toggle highlight / clear (selection, then values)
      next / prev / all   step through the selection one fixture at a time; all = whole selection again
      lowlight            toggle lowlight */
namespace command
{
    struct Result
    {
        juce::String error;          // empty when it worked
        juce::String message;        // what happened, for the status line
        bool selectionChanged = false, valuesChanged = false, highlightChanged = false;
    };

    Result execute (const juce::String& line, Programmer&, const Patch&, const FixtureLibrary&, const PresetLibrary&);

    /** Attribute named in a command ("pan", "red", "zoom"...); empty if unknown. */
    juce::String attributeFromWord (const juce::String& word);
}

} // namespace pl
