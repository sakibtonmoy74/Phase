#include <map>
#include "FixtureType.h"

namespace pl
{

//==============================================================================
int FixtureType::highestChannel() const
{
    int hi = 0;
    for (auto& a : attributes)
        hi = juce::jmax (hi, a.coarse, a.fine);
    return hi;
}

std::vector<AttributeSlots> FixtureType::attributeSlots() const
{
    std::vector<AttributeSlots> out;
    if (usesVirtualDimmer())
        out.push_back ({ "Dimmer", -1, -1, 0.0f, 1.0f });
    for (int c = 1; c <= cellCount() && hasCells(); ++c)
        if (cellUsesVirtualDimmer (c))
            out.push_back ({ cellKey ("Dimmer", c), -1, -1, 1.0f, 1.0f });   // per pixel, full by default

    for (auto& a : attributes)
    {
        if (a.attribute == kNoAttribute || a.coarse < 1)
            continue;
        const int hl = a.highlight >= 0 ? a.highlight : defaultHighlight (a.attribute);
        out.push_back ({ a.key(), a.coarse - 1, a.fine > 0 ? a.fine - 1 : -1,
                         (float) juce::jlimit (0, 255, a.defaultValue) / 255.0f,
                         hl >= 0 ? (float) juce::jlimit (0, 255, hl) / 255.0f : -1.0f });
    }
    return out;
}

juce::StringArray FixtureType::channelMap() const
{
    juce::StringArray map;
    for (int i = 0; i < footprint(); ++i)
        map.add ({});
    for (auto& a : attributes)
    {
        if (a.coarse >= 1 && a.coarse <= map.size())
            map.set (a.coarse - 1, attributeLabel (a.key()));
        if (a.fine >= 1 && a.fine <= map.size())
            map.set (a.fine - 1, attributeLabel (a.key()) + " fine");
    }
    return map;
}

juce::String FixtureType::validate() const
{
    std::map<int, juce::String> used;
    for (auto& a : attributes)
    {
        if (a.coarse < 1 || a.coarse > 512)
            return attributeLabel (a.key()) + ": channel must be 1-512";
        if (a.fine < 0 || a.fine > 512)
            return attributeLabel (a.attribute) + ": fine channel must be 1-512";
        if (a.fine == a.coarse)
            return attributeLabel (a.attribute) + ": coarse and fine can't be the same channel";

        for (int ch : { a.coarse, a.fine })
        {
            if (ch == 0) continue;
            if (used.count (ch))
                return "Channel " + juce::String (ch) + " is used by both "
                     + used[ch] + " and " + attributeLabel (a.key());
            used[ch] = attributeLabel (a.key());
        }
    }
    for (size_t i = 0; i < attributes.size(); ++i)
        for (size_t j = i + 1; j < attributes.size(); ++j)
            if (attributes[i].key() == attributes[j].key())
                return attributeLabel (attributes[i].key()) + " is assigned twice";
    for (auto& a : attributes)
        if (a.cell > cellCount())
            return attributeLabel (a.key()) + ": the fixture only has " + juce::String (cellCount()) + " cell(s)";
    return {};
}

void FixtureType::autoNumber()
{
    int ch = 1;
    for (auto& a : attributes)
    {
        const bool sixteen = a.fine > 0;
        a.coarse = ch++;
        a.fine = sixteen ? ch++ : 0;
    }
    extraChannels = 0;
}

void FixtureType::setCellLayout (std::vector<std::pair<float, float>> positions)
{
    if (positions.empty()) { cellLayout.clear(); return; }
    float x0 = positions[0].first, x1 = x0, y0 = positions[0].second, y1 = y0;
    for (auto& [x, y] : positions)
    {
        x0 = juce::jmin (x0, x); x1 = juce::jmax (x1, x);
        y0 = juce::jmin (y0, y); y1 = juce::jmax (y1, y);
    }
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    for (auto& [x, y] : positions) { x -= cx; y -= cy; }
    cellLayout = std::move (positions);
}

void FixtureType::repeatCellOne()
{
    std::vector<AttributeDef> whole, cellOne;
    for (auto& a : attributes)
    {
        if (a.cell == 0) whole.push_back (a);
        else if (a.cell == 1) cellOne.push_back (a);
    }
    if (cellOne.empty()) return;
    attributes = whole;
    for (int c = 1; c <= cellCount(); ++c)
        for (auto a : cellOne)
        {
            a.cell = c;
            attributes.push_back (a);
        }
    autoNumber();
}

bool FixtureType::cellsAreUniform() const
{
    std::map<int, juce::StringArray> byCell;
    for (auto& a : attributes) if (a.cell > 0) byCell[a.cell].add (a.attribute);
    if (byCell.empty()) return true;
    const auto first = byCell.begin()->second;
    for (auto& [c, list] : byCell) if (list != first) return false;
    return (int) byCell.size() == cellCount();
}

void FixtureType::addPixelSet (int count, const juce::StringArray& attrs, bool sameSpots)
{
    count = juce::jlimit (1, 512, count);
    if (attrs.isEmpty()) return;
    const int base = hasCells() ? cellCount() : 0;

    // positions of the existing cells (cell units), then the new ones
    std::vector<std::pair<float, float>> pos;
    float maxY = 0.0f, minX = 0.0f;
    for (int c = 1; c <= base; ++c)
    {
        auto [x, y] = cellOffset (c);
        pos.push_back ({ x / cellSpacing, y / cellSpacing });
        maxY = juce::jmax (maxY, y / cellSpacing);
        minX = juce::jmin (minX, x / cellSpacing);
    }
    for (int i = 0; i < count; ++i)
    {
        if (sameSpots && base > 0)
        {
            auto p = pos[(size_t) (i % base)];
            pos.push_back ({ p.first + 0.35f, p.second + 0.35f });
        }
        else
            pos.push_back ({ (float) i - (float) (count - 1) * 0.5f, maxY + 1.5f });
    }

    int ch = highestChannel();
    for (int c = base + 1; c <= base + count; ++c)
        for (auto& a : attrs)
        {
            AttributeDef d;
            d.attribute = a;
            d.coarse = ++ch;
            d.cell = c;
            d.defaultValue = 0;
            attributes.push_back (d);
        }
    cellsX = base + count;
    cellsY = 1;
    setCellLayout (pos);
}

bool FixtureType::hasRealDimmer() const
{
    for (auto& a : attributes)
        if (a.attribute == "Dimmer" && a.cell == 0)
            return true;
    return false;
}

bool FixtureType::cellUsesVirtualDimmer (int cell) const
{
    if (cell <= 0) return false;
    bool target = false;
    for (auto& a : attributes)
        if (a.cell == cell)
        {
            if (a.attribute == "Dimmer") return false;     // the cell has its own dimmer channel
            target |= isVirtualDimmerTarget (a.attribute);
        }
    return target;
}

bool FixtureType::usesVirtualDimmer() const
{
    if (hasRealDimmer())
        return false;
    for (auto& a : attributes)
        if (isVirtualDimmerTarget (a.attribute))
            return true;
    return false;
}

juce::String FixtureType::displayName() const
{
    return manufacturer.isNotEmpty() ? manufacturer + " " + name : name;
}

juce::var FixtureType::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("manufacturer", manufacturer);
    o->setProperty ("name", name);
    o->setProperty ("extraChannels", extraChannels);
    if (hasCells()) { o->setProperty ("cellsX", cellsX); o->setProperty ("cellsY", cellsY); }
    o->setProperty ("panRange", panRange);
    o->setProperty ("tiltRange", tiltRange);
    o->setProperty ("beamMin", beamMin);
    o->setProperty ("beamMax", beamMax);
    if (hasCustomLayout())
    {
        juce::Array<juce::var> layout;
        for (auto& [x, y] : cellLayout) layout.add (juce::Array<juce::var> { x, y });
        o->setProperty ("cellLayout", layout);
    }

    juce::Array<juce::var> attrs;
    for (auto& a : attributes)
    {
        auto* c = new juce::DynamicObject();
        c->setProperty ("attribute", a.attribute);
        c->setProperty ("coarse", a.coarse);
        c->setProperty ("fine", a.fine);
        c->setProperty ("default", a.defaultValue);
        if (a.highlight >= 0) c->setProperty ("highlight", a.highlight);
        if (a.cell > 0) c->setProperty ("cell", a.cell);
        attrs.add (juce::var (c));
    }
    o->setProperty ("attributes", attrs);
    return juce::var (o);
}

FixtureType FixtureType::fromVar (const juce::var& v)
{
    FixtureType t;
    t.id            = v["id"].toString();
    t.manufacturer  = v["manufacturer"].toString();
    t.name          = v["name"].toString();
    t.extraChannels = juce::jmax (0, (int) v["extraChannels"]);
    t.cellsX = juce::jlimit (1, 256, (int) v.getProperty ("cellsX", 1));
    t.cellsY = juce::jlimit (1, 256, (int) v.getProperty ("cellsY", 1));
    t.panRange  = juce::jlimit (0.0f, 720.0f, (float) (double) v.getProperty ("panRange", 540.0));
    t.tiltRange = juce::jlimit (0.0f, 360.0f, (float) (double) v.getProperty ("tiltRange", 270.0));
    t.beamMin   = juce::jlimit (1.0f, 120.0f, (float) (double) v.getProperty ("beamMin", 12.0));
    t.beamMax   = juce::jlimit (1.0f, 120.0f, (float) (double) v.getProperty ("beamMax", 40.0));
    if (auto* layout = v["cellLayout"].getArray())
    {
        std::vector<std::pair<float, float>> pos;
        for (auto& p : *layout) pos.push_back ({ (float) (double) p[0], (float) (double) p[1] });
        if ((int) pos.size() == t.cellCount()) t.cellLayout = pos;
    }

    if (auto* arr = v["attributes"].getArray())
    {
        for (auto& c : *arr)
        {
            AttributeDef a;
            a.attribute    = c["attribute"].toString();
            a.coarse       = (int) c["coarse"];
            a.fine         = (int) c["fine"];
            a.defaultValue = juce::jlimit (0, 255, (int) c["default"]);
            a.highlight = juce::jlimit (-1, 255, (int) c.getProperty ("highlight", -1));
            a.cell         = juce::jmax (0, (int) c["cell"]);
            if (a.attribute.isNotEmpty() && a.attribute != kNoAttribute)
                t.attributes.push_back (a);
        }
    }
    else if (auto* chans = v["channels"].getArray())
    {
        // v0.1 format: one entry per DMX slot with a "fine" flag
        int channel = 0, lastUsed = 0;
        for (auto& c : *chans)
        {
            ++channel;
            auto attr = c["attribute"].toString();
            if (attr.isEmpty() || attr == kNoAttribute) continue;
            lastUsed = channel;

            AttributeDef* existing = nullptr;
            for (auto& a : t.attributes)
                if (a.attribute == attr) existing = &a;

            if ((bool) c["fine"])
            {
                if (existing != nullptr) existing->fine = channel;
                else t.attributes.push_back ({ attr, channel, 0, 0 });
            }
            else if (existing == nullptr)
            {
                t.attributes.push_back ({ attr, channel, 0, juce::jlimit (0, 255, (int) c["default"]) });
            }
        }
        t.extraChannels = juce::jmax (0, chans->size() - lastUsed);
    }
    return t;
}

//==============================================================================
FixtureLibrary::FixtureLibrary() { addBuiltIns(); }

void FixtureLibrary::addBuiltIns()
{
    struct Def { const char* attr; bool sixteen; int def; };

    auto make = [this] (const char* id, const char* mfr, const char* name, std::initializer_list<Def> defs)
    {
        FixtureType t;
        t.id = id; t.manufacturer = mfr; t.name = name; t.builtIn = true;
        for (auto& d : defs)
            t.attributes.push_back ({ d.attr, 0, d.sixteen ? 1 : 0, d.def });
        t.autoNumber();
        list.push_back (t);
    };

    make ("builtin.dimmer",   "Generic", "Dimmer",        { { "Dimmer", false, 0 } });
    make ("builtin.dimmer16", "Generic", "Dimmer 16-bit", { { "Dimmer", true, 0 } });
    make ("builtin.rgb",      "Generic", "RGB 3ch",
          { { "ColorR", false, 255 }, { "ColorG", false, 255 }, { "ColorB", false, 255 } });
    make ("builtin.rgbw8",    "Generic", "RGBW Par 8ch",
          { { "Dimmer", false, 0 }, { "ColorR", false, 255 }, { "ColorG", false, 255 },
            { "ColorB", false, 255 }, { "ColorW", false, 255 }, { "Strobe", false, 0 },
            { "ColorMacro", false, 0 }, { "Control", false, 0 } });
    make ("builtin.spot16",   "Generic", "Spot 16-bit 14ch",
          { { "Pan", true, 128 }, { "Tilt", true, 128 }, { "PanTiltSpeed", false, 0 },
            { "Dimmer", false, 0 }, { "Shutter", false, 255 }, { "ColorWheel", false, 0 },
            { "Gobo1", false, 0 }, { "Gobo1Rot", false, 0 }, { "Prism", false, 0 },
            { "Focus", false, 128 }, { "Zoom", false, 128 }, { "Control", false, 0 } });
    {
        FixtureType bar;
        bar.id = "builtin.bar8"; bar.manufacturer = "Generic"; bar.name = "LED Bar 8px RGB"; bar.builtIn = true;
        bar.cellsX = 8;
        bar.attributes.push_back ({ "Dimmer", 0, 0, 0, 0 });
        for (auto* c : { "ColorR", "ColorG", "ColorB" }) bar.attributes.push_back ({ c, 0, 0, 255, 1 });
        bar.repeatCellOne();
        list.push_back (bar);

        FixtureType panel;
        panel.id = "builtin.panel4x4"; panel.manufacturer = "Generic"; panel.name = "Pixel Panel 4x4 RGB"; panel.builtIn = true;
        panel.cellsX = 4; panel.cellsY = 4;
        for (auto* c : { "ColorR", "ColorG", "ColorB" }) panel.attributes.push_back ({ c, 0, 0, 255, 1 });
        panel.repeatCellOne();
        list.push_back (panel);
    }

    make ("builtin.wash16",   "Generic", "LED Wash 16-bit 13ch",
          { { "Pan", true, 128 }, { "Tilt", true, 128 }, { "Dimmer", true, 0 },
            { "Shutter", false, 255 }, { "ColorR", false, 255 }, { "ColorG", false, 255 },
            { "ColorB", false, 255 }, { "ColorW", false, 255 }, { "Zoom", false, 128 },
            { "Control", false, 0 } });
}

const FixtureType* FixtureLibrary::find (const juce::String& id) const
{
    for (auto& t : list)
        if (t.id == id)
            return &t;
    return nullptr;
}

FixtureType* FixtureLibrary::findMutable (const juce::String& id)
{
    for (auto& t : list)
        if (t.id == id)
            return t.builtIn ? nullptr : &t;
    return nullptr;
}

void FixtureLibrary::addOrReplace (const FixtureType& type)
{
    auto t = type;
    t.builtIn = false;
    if (t.id.isEmpty())
        t.id = "user." + juce::Uuid().toDashedString();

    for (auto& existing : list)
        if (existing.id == t.id)
        {
            if (! existing.builtIn)
                existing = t;
            return;
        }
    list.push_back (t);
}

bool FixtureLibrary::remove (const juce::String& id)
{
    for (auto it = list.begin(); it != list.end(); ++it)
        if (it->id == id && ! it->builtIn)
        {
            list.erase (it);
            return true;
        }
    return false;
}

juce::String FixtureLibrary::duplicate (const juce::String& id)
{
    auto* src = find (id);
    if (src == nullptr)
        return {};
    auto copy = *src;
    copy.id = "user." + juce::Uuid().toDashedString();
    copy.name = src->name + " (copy)";
    copy.builtIn = false;
    list.push_back (copy);
    return copy.id;
}

juce::String FixtureLibrary::createEmpty()
{
    FixtureType t;
    t.id = "user." + juce::Uuid().toDashedString();
    t.manufacturer = "Custom";
    t.name = "New Fixture";
    t.attributes.push_back ({ "Dimmer", 1, 0, 0 });
    list.push_back (t);
    return t.id;
}

void FixtureLibrary::clearUserTypes()
{
    list.erase (std::remove_if (list.begin(), list.end(),
                                [] (const FixtureType& t) { return ! t.builtIn; }),
                list.end());
}

} // namespace pl
