#pragma once
#include <juce_core/juce_core.h>

namespace pl
{

/** Feature groups, following the grandMA layout. */
enum class Family
{
    Dimmer,
    Position,
    Gobo,
    Color,
    Beam,
    Focus,
    Control,
    None
};

struct AttributeInfo
{
    const char* name;   // internal name, stored in show files ("Pan", "ColorR"...)
    const char* label;  // short label for the UI
    Family family;
};

/** Every attribute the fixture builder can assign to a channel. */
const std::vector<AttributeInfo>& allAttributes();

/** Cell attributes are keyed "ColorR@3" (cell 3). These strip / read the suffix. */
juce::String baseAttribute (const juce::String& key);
int cellOf (const juce::String& key);                    // 0 = whole fixture
juce::String cellKey (const juce::String& attribute, int cell);

const AttributeInfo* findAttribute (const juce::String& name);
Family familyOf (const juce::String& attributeName);
juce::String familyName (Family);
juce::String attributeLabel (const juce::String& attributeName);

/** Families shown as tabs in the programmer, in display order. */
const std::vector<Family>& programmerFamilies();

/** Highlight value (raw 0-255) used when a fixture type doesn't set one, or -1
    to leave the attribute alone: open white, shutter open, no gobo / prism / frost. */
int defaultHighlight (const juce::String& attributeName);

/** Attributes that are scaled by a virtual dimmer on fixtures without a real one. */
bool isVirtualDimmerTarget (const juce::String& attributeName);

inline constexpr const char* kNoAttribute = "None";

/** Selection targets: a whole fixture is its FID (> 0); one cell (pixel) of a
    multi-cell fixture is encoded as -(fid * 1000 + cell). Selections, groups and
    effects hold targets, so pixels work everywhere fixtures do. */
inline constexpr int kMaxCells = 999;
inline int cellTarget (int fid, int cell)  { return cell > 0 ? -(fid * 1000 + cell) : fid; }
inline int targetFid (int target)          { return target < 0 ? (-target) / 1000 : target; }
inline int targetCell (int target)         { return target < 0 ? (-target) % 1000 : 0; }
inline bool isCellTarget (int target)      { return target < 0; }
inline juce::String targetName (int target)
{
    return isCellTarget (target) ? juce::String (targetFid (target)) + "." + juce::String (targetCell (target))
                                 : juce::String (target);
}

} // namespace pl
