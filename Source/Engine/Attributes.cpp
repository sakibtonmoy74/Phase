#include "Attributes.h"

namespace pl
{

const std::vector<AttributeInfo>& allAttributes()
{
    static const std::vector<AttributeInfo> list {
        { "None",         "None",        Family::None },

        { "Dimmer",       "Dim",         Family::Dimmer },
        { "Shutter",      "Shutter",     Family::Dimmer },
        { "Strobe",       "Strobe",      Family::Dimmer },

        { "Pan",          "Pan",         Family::Position },
        { "Tilt",         "Tilt",        Family::Position },
        { "PanTiltSpeed", "P/T Speed",   Family::Position },

        { "Gobo1",        "Gobo 1",      Family::Gobo },
        { "Gobo1Rot",     "Gobo 1 Rot",  Family::Gobo },
        { "Gobo2",        "Gobo 2",      Family::Gobo },
        { "Gobo2Rot",     "Gobo 2 Rot",  Family::Gobo },
        { "Animation",    "Animation",   Family::Gobo },

        { "ColorR",       "Red",         Family::Color },
        { "ColorG",       "Green",       Family::Color },
        { "ColorB",       "Blue",        Family::Color },
        { "ColorW",       "White",       Family::Color },
        { "ColorA",       "Amber",       Family::Color },
        { "ColorUV",      "UV",          Family::Color },
        { "ColorLime",    "Lime",        Family::Color },
        { "ColorC",       "Cyan",        Family::Color },
        { "ColorM",       "Magenta",     Family::Color },
        { "ColorY",       "Yellow",      Family::Color },
        { "CTO",          "CTO",         Family::Color },
        { "ColorWheel",   "Color Wheel", Family::Color },
        { "ColorMacro",   "Color Macro", Family::Color },

        { "Prism",        "Prism",       Family::Beam },
        { "PrismRot",     "Prism Rot",   Family::Beam },
        { "Frost",        "Frost",       Family::Beam },
        { "Iris",         "Iris",        Family::Beam },
        { "Effect",       "FX Wheel",    Family::Beam },

        { "Zoom",         "Zoom",        Family::Focus },
        { "Focus",        "Focus",       Family::Focus },

        { "Control",      "Control",     Family::Control },
        { "Lamp",         "Lamp",        Family::Control },
        { "Reset",        "Reset",       Family::Control },
        { "Macro",        "Macro",       Family::Control },
        { "Raw",          "Raw",         Family::Control },
    };
    return list;
}

juce::String baseAttribute (const juce::String& key)
{
    const int at = key.indexOfChar ('@');
    return at < 0 ? key : key.substring (0, at);
}

int cellOf (const juce::String& key)
{
    const int at = key.indexOfChar ('@');
    return at < 0 ? 0 : key.substring (at + 1).getIntValue();
}

juce::String cellKey (const juce::String& attribute, int cell)
{
    return cell > 0 ? attribute + "@" + juce::String (cell) : attribute;
}

const AttributeInfo* findAttribute (const juce::String& name)
{
    const auto base = baseAttribute (name);
    for (auto& a : allAttributes())
        if (base == a.name)
            return &a;
    return nullptr;
}

Family familyOf (const juce::String& attributeName)
{
    if (auto* a = findAttribute (attributeName))
        return a->family;
    return Family::None;
}

juce::String familyName (Family f)
{
    switch (f)
    {
        case Family::Dimmer:   return "Dimmer";
        case Family::Position: return "Position";
        case Family::Gobo:     return "Gobo";
        case Family::Color:    return "Color";
        case Family::Beam:     return "Beam";
        case Family::Focus:    return "Focus";
        case Family::Control:  return "Control";
        case Family::None:     break;
    }
    return "None";
}

juce::String attributeLabel (const juce::String& attributeName)
{
    const int cell = cellOf (attributeName);
    const juce::String label = findAttribute (attributeName) != nullptr ? juce::String (findAttribute (attributeName)->label)
                                                                     : baseAttribute (attributeName);
    return cell > 0 ? label + " " + juce::String (cell) : label;
}

const std::vector<Family>& programmerFamilies()
{
    static const std::vector<Family> f { Family::Dimmer, Family::Position, Family::Gobo,
                                         Family::Color,  Family::Beam,     Family::Focus,
                                         Family::Control };
    return f;
}

int defaultHighlight (const juce::String& key)
{
    const auto a = baseAttribute (key);
    if (a == "Dimmer" || a == "Shutter" || a == "ColorR" || a == "ColorG" || a == "ColorB" || a == "ColorW") return 255;
    if (a == "ColorA" || a == "ColorUV" || a == "ColorLime" || a == "ColorC" || a == "ColorM" || a == "ColorY"
        || a == "CTO" || a == "ColorWheel" || a == "ColorMacro" || a == "Gobo1" || a == "Gobo2" || a == "Prism"
        || a == "Frost" || a == "Iris" || a == "Strobe" || a == "Effect" || a == "Animation")
        return 0;
    return -1;                     // pan, tilt, zoom, focus, control...: keep
}

bool isVirtualDimmerTarget (const juce::String& key)
{
    const auto n = baseAttribute (key);
    return n == "ColorR" || n == "ColorG" || n == "ColorB" || n == "ColorW"
        || n == "ColorA" || n == "ColorUV" || n == "ColorLime";
}

} // namespace pl
