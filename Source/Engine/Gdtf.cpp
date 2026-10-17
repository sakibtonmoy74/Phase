#include "Gdtf.h"
#include <set>

namespace pl
{
namespace gdtf
{

juce::String mapAttribute (const juce::String& g)
{
    struct Rule { const char* prefix; const char* phase; bool exact; };
    static const Rule rules[] {
        { "Dimmer", "Dimmer", true },
        { "Pan", "Pan", true }, { "Tilt", "Tilt", true },
        { "PanTiltMSpeed", "PanTiltSpeed", true }, { "PositionMSpeed", "PanTiltSpeed", true },
        { "PanTiltMTime", "PanTiltSpeed", true }, { "PositionMTime", "PanTiltSpeed", true },
        { "Shutter1", "Shutter", false }, { "StrobeFrequency", "Strobe", false }, { "StrobeRate", "Strobe", false },
        { "StrobeDuration", "Strobe", false },
        { "Gobo1Pos", "Gobo1Rot", false }, { "Gobo1WheelSpin", "Gobo1", false }, { "Gobo1", "Gobo1", true },
        { "Gobo1SelectSpin", "Gobo1", false }, { "Gobo1SelectShake", "Gobo1", false }, { "Gobo1SelectEffects", "Gobo1", false },
        { "Gobo2Pos", "Gobo2Rot", false }, { "Gobo2", "Gobo2", true }, { "Gobo2Select", "Gobo2", false },
        { "AnimationWheel1", "Animation", false }, { "AnimationIndexRotate1", "Animation", false },
        { "ColorAdd_R", "ColorR", true }, { "ColorRGB_Red", "ColorR", true },
        { "ColorAdd_G", "ColorG", true }, { "ColorRGB_Green", "ColorG", true },
        { "ColorAdd_B", "ColorB", true }, { "ColorRGB_Blue", "ColorB", true },
        { "ColorAdd_W", "ColorW", true }, { "ColorAdd_WW", "ColorW", true }, { "ColorAdd_CW", "ColorW", true },
        { "ColorRGB_White", "ColorW", true },
        { "ColorAdd_A", "ColorA", true }, { "ColorAdd_RY", "ColorA", true }, { "ColorRGB_Amber", "ColorA", true },
        { "ColorAdd_UV", "ColorUV", true }, { "ColorAdd_GY", "ColorLime", true }, { "ColorRGB_Lime", "ColorLime", true },
        { "ColorSub_C", "ColorC", true }, { "ColorSub_M", "ColorM", true }, { "ColorSub_Y", "ColorY", true },
        { "CTO", "CTO", true }, { "CTC", "CTO", true }, { "CTB", "CTO", true },
        { "ColorMacro", "ColorMacro", false }, { "ColorMixMode", "Control", false },
        { "Color1", "ColorWheel", false }, { "Color2", "ColorWheel", false },
        { "Prism1Pos", "PrismRot", false }, { "Prism1", "Prism", true }, { "Prism1Select", "Prism", false },
        { "Frost1", "Frost", false }, { "Iris", "Iris", true }, { "IrisStrobe", "Iris", false }, { "IrisPulse", "Iris", false },
        { "Effects1", "Effect", false },
        { "Zoom", "Zoom", true }, { "ZoomModeSpot", "Zoom", false }, { "Focus1", "Focus", false },
        { "LampControl", "Lamp", false }, { "LampPowerMode", "Lamp", false },
        { "FixtureGlobalReset", "Reset", false }, { "GlobalReset", "Reset", false },
        { "Control", "Control", false }, { "DimmerMode", "Control", false }, { "DimmerCurve", "Control", false },
        { "Function", "Control", false }, { "Fans", "Control", false },
    };
    for (auto& r : rules)
        if (r.exact ? g == r.prefix : g.startsWith (r.prefix))
            return r.phase;
    if (g.endsWith ("Mode")) return "Control";
    return {};
}

//==============================================================================
static juce::String readZip (const juce::MemoryBlock& data, juce::String& xmlText)
{
    juce::MemoryInputStream in (data, false);
    juce::ZipFile zip (in);
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (auto* e = zip.getEntry (i); e != nullptr && e->filename.equalsIgnoreCase ("description.xml"))
        {
            std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
            if (s == nullptr) break;
            xmlText = s->readEntireStreamAsString();
            return {};
        }
    return "No description.xml inside the GDTF file";
}

juce::String read (const juce::MemoryBlock& data, Description& out)
{
    juce::String xmlText;
    const auto* bytes = static_cast<const char*> (data.getData());
    if (data.getSize() > 4 && bytes[0] == 'P' && bytes[1] == 'K')
    {
        if (auto err = readZip (data, xmlText); err.isNotEmpty()) return err;
    }
    else
        xmlText = data.toString();

    std::shared_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (xmlText).release());
    if (xml == nullptr || ! xml->hasTagName ("GDTF")) return "Not a GDTF description";
    auto* ft = xml->getChildByName ("FixtureType");
    if (ft == nullptr) return "GDTF has no FixtureType";

    out = {};
    out.xml = xml;
    out.manufacturer = ft->getStringAttribute ("Manufacturer");
    out.name = ft->getStringAttribute ("LongName", ft->getStringAttribute ("Name"));
    if (out.name.isEmpty()) out.name = ft->getStringAttribute ("Name");
    out.fixtureTypeId = ft->getStringAttribute ("FixtureTypeID");

    if (auto* modes = ft->getChildByName ("DMXModes"))
        for (auto* m : modes->getChildWithTagNameIterator ("DMXMode"))
        {
            FixtureType t;
            ModeInfo info { m->getStringAttribute ("Name"), 0, 0 };
            Description tmp = out;
            if (toFixtureType (tmp, info.name, t).isEmpty())
            {
                info.footprint = t.footprint();
                info.cells = t.hasCells() ? t.cellCount() : 0;
            }
            out.modes.push_back (info);
        }
    if (out.modes.empty()) return "GDTF has no DMX modes";
    return {};
}

//==============================================================================
namespace
{
    /** "32768/2" → 128 (8-bit), "255/1" → 255, "127" → 127. */
    int parseDefault (const juce::String& s)
    {
        if (s.isEmpty()) return 0;
        const auto value = s.upToFirstOccurrenceOf ("/", false, false).getLargeIntValue();
        const int bytes = s.containsChar ('/') ? juce::jlimit (1, 4, s.fromFirstOccurrenceOf ("/", false, false).getIntValue()) : 1;
        return juce::jlimit (0, 255, (int) (value >> (8 * (bytes - 1))));
    }

    juce::XmlElement* findGeometry (juce::XmlElement* parent, const juce::String& name)
    {
        if (parent == nullptr) return nullptr;
        for (auto* c : parent->getChildIterator())
        {
            if (c->getStringAttribute ("Name") == name) return c;
            if (auto* f = findGeometry (c, name)) return f;
        }
        return nullptr;
    }

    struct Reference { juce::String name, geometry; int offset = 1; };

    void collectReferences (juce::XmlElement* g, std::vector<Reference>& out)
    {
        if (g == nullptr) return;
        for (auto* c : g->getChildIterator())
        {
            if (c->hasTagName ("GeometryReference"))
            {
                Reference r { c->getStringAttribute ("Name"), c->getStringAttribute ("Geometry"), 1 };
                if (auto* b = c->getChildByName ("Break")) r.offset = juce::jmax (1, b->getIntAttribute ("DMXOffset", 1));
                out.push_back (r);
            }
            collectReferences (c, out);
        }
    }
}

juce::String toFixtureType (const Description& d, const juce::String& modeName, FixtureType& out)
{
    if (d.xml == nullptr) return "No GDTF loaded";
    auto* ft = d.xml->getChildByName ("FixtureType");
    auto* modes = ft != nullptr ? ft->getChildByName ("DMXModes") : nullptr;
    juce::XmlElement* mode = nullptr;
    if (modes != nullptr)
        for (auto* m : modes->getChildWithTagNameIterator ("DMXMode"))
            if (m->getStringAttribute ("Name") == modeName) mode = m;
    if (mode == nullptr) return "Mode not found: " + modeName;

    // repeated geometries (pixels): each reference to a template geometry is a cell
    std::vector<Reference> refs;
    collectReferences (findGeometry (ft->getChildByName ("Geometries"), mode->getStringAttribute ("Geometry")), refs);
    std::map<juce::String, std::vector<int>> templateOffsets;      // template geometry → offsets per cell
    for (auto& r : refs) templateOffsets[r.geometry].push_back (r.offset);
    int cells = 0;
    for (auto& [g, offs] : templateOffsets) cells = juce::jmax (cells, (int) offs.size());

    out = {};
    out.manufacturer = d.manufacturer;
    out.name = d.name + " (" + modeName + ")";
    out.id = "gdtf." + (d.fixtureTypeId.isNotEmpty() ? d.fixtureTypeId : (d.manufacturer + "." + d.name).removeCharacters (" "))
           + "." + modeName.removeCharacters (" /\\");
    if (cells > 1) { out.cellsX = cells; out.cellsY = 1; }

    std::set<juce::String> used;
    int highest = 0;
    auto addChannel = [&] (const juce::String& attr, int coarse, int fine, int def, int cell)
    {
        highest = juce::jmax (highest, coarse, fine);
        // one attribute per key: a duplicate becomes Raw, then a spare channel
        juce::String a;
        for (auto& candidate : { attr.isNotEmpty() ? attr : juce::String ("Raw"), juce::String ("Raw") })
            if (! used.count (cellKey (candidate, cell))) { a = candidate; break; }
        if (a.isEmpty()) return;                  // stays a spare channel (footprint kept below)
        used.insert (cellKey (a, cell));
        out.attributes.push_back ({ a, coarse, fine, def, cell });
    };

    auto* channels = mode->getChildByName ("DMXChannels");
    if (channels == nullptr) return "Mode has no channels";
    for (auto* ch : channels->getChildWithTagNameIterator ("DMXChannel"))
    {
        if (ch->getIntAttribute ("DMXBreak", 1) != 1) continue;        // only the first DMX break
        auto offsets = juce::StringArray::fromTokens (ch->getStringAttribute ("Offset"), ",", "");
        offsets.removeEmptyStrings();
        if (offsets.isEmpty() || offsets[0].getIntValue() < 1) continue;   // virtual channel
        const int coarseRel = offsets[0].getIntValue();
        const int fineRel = offsets.size() > 1 ? offsets[1].getIntValue() : 0;
        const int extraRel = offsets.size() > 2 ? offsets[offsets.size() - 1].getIntValue() : 0;

        juce::String gdtfAttr;
        juce::String def = ch->getStringAttribute ("Default");
        if (auto* logical = ch->getChildByName ("LogicalChannel"))
        {
            gdtfAttr = logical->getStringAttribute ("Attribute");
            // default: the channel's own (GDTF 1.0), else its initial function's, else the first function's
            const auto initial = ch->getStringAttribute ("InitialFunction");
            juce::XmlElement* first = nullptr;
            for (auto* fn : logical->getChildWithTagNameIterator ("ChannelFunction"))
            {
                if (first == nullptr) first = fn;
                if (def.isEmpty() && initial.isNotEmpty() && initial.endsWith ("." + fn->getStringAttribute ("Name")))
                    def = fn->getStringAttribute ("Default");
            }
            if (first != nullptr)
            {
                if (gdtfAttr.isEmpty()) gdtfAttr = first->getStringAttribute ("Attribute");
                if (def.isEmpty()) def = first->getStringAttribute ("Default");
            }
        }
        const auto phaseAttr = mapAttribute (gdtfAttr);
        const int defaultValue = parseDefault (def);

        const auto geometry = ch->getStringAttribute ("Geometry");
        auto tmpl = templateOffsets.find (geometry);
        if (tmpl != templateOffsets.end() && cells > 1)
        {
            for (int c = 0; c < (int) tmpl->second.size(); ++c)
            {
                const int base = tmpl->second[(size_t) c] - 1;
                addChannel (phaseAttr, base + coarseRel, fineRel > 0 ? base + fineRel : 0, defaultValue, c + 1);
                if (extraRel > 0) highest = juce::jmax (highest, base + extraRel);
            }
        }
        else
        {
            addChannel (phaseAttr, coarseRel, fineRel, defaultValue, 0);
            highest = juce::jmax (highest, extraRel);
        }
    }

    if (out.attributes.empty()) return "No usable channels in mode " + modeName;
    // keep channel order, and the real footprint (spare and 24-bit channels)
    std::stable_sort (out.attributes.begin(), out.attributes.end(),
                      [] (const AttributeDef& a, const AttributeDef& b) { return a.coarse < b.coarse; });
    out.extraChannels = juce::jmax (0, highest - out.highestChannel());
    if (highest > 512) return "Mode " + modeName + " is larger than one universe";
    return out.validate();
}

} // namespace gdtf
} // namespace pl
