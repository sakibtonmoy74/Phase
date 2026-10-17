#include "Presets.h"

namespace pl
{

const std::vector<PresetPool>& allPresetPools()
{
    static const std::vector<PresetPool> p { PresetPool::Group, PresetPool::Dimmer, PresetPool::Position,
                                             PresetPool::Gobo, PresetPool::Color, PresetPool::Beam,
                                             PresetPool::Focus, PresetPool::Effect, PresetPool::All };
    return p;
}

juce::uint32 colourOfValues (const std::map<juce::String, float>& v)
{
    // whole-fixture colour, or cell 1's for pixel fixtures
    const bool cellsOnly = ! v.count ("ColorR") && ! v.count ("ColorC") && (v.count ("ColorR@1") || v.count ("ColorC@1"));
    auto get = [&v, cellsOnly] (const char* k, float def)
    {
        auto i = v.find (cellsOnly ? juce::String (k) + "@1" : juce::String (k));
        return i != v.end() ? i->second : def;
    };
    float r = get ("ColorR", -1.0f), g = get ("ColorG", -1.0f), b = get ("ColorB", -1.0f);
    if (r < 0 && g < 0 && b < 0)
    {
        if (! (v.count ("ColorC") || v.count ("ColorM") || v.count ("ColorY"))) return 0;
        r = 1.0f - get ("ColorC", 0.0f); g = 1.0f - get ("ColorM", 0.0f); b = 1.0f - get ("ColorY", 0.0f);
    }
    const float w = get ("ColorW", 0.0f);
    auto c = [w] (float x) { return (juce::uint32) juce::roundToInt (juce::jlimit (0.0f, 1.0f, juce::jmax (0.0f, x) + w) * 255.0f); };
    return 0xff000000u | (c (r) << 16) | (c (g) << 8) | c (b);
}

juce::String poolName (PresetPool p)
{
    switch (p)
    {
        case PresetPool::Group:    return "Groups";
        case PresetPool::Dimmer:   return "Dimmer";
        case PresetPool::Position: return "Position";
        case PresetPool::Gobo:     return "Gobo";
        case PresetPool::Color:    return "Color";
        case PresetPool::Beam:     return "Beam";
        case PresetPool::Focus:    return "Focus";
        case PresetPool::Effect:   return "Effects";
        case PresetPool::All:      return "All";
    }
    return {};
}

juce::String poolKey (PresetPool p) { return poolName (p).toLowerCase(); }

std::optional<PresetPool> poolFromKey (const juce::String& k)
{
    for (auto p : allPresetPools())
        if (poolKey (p) == k) return p;
    return {};
}

Family poolFamily (PresetPool p)
{
    switch (p)
    {
        case PresetPool::Dimmer:   return Family::Dimmer;
        case PresetPool::Position: return Family::Position;
        case PresetPool::Gobo:     return Family::Gobo;
        case PresetPool::Color:    return Family::Color;
        case PresetPool::Beam:     return Family::Beam;
        case PresetPool::Focus:    return Family::Focus;
        case PresetPool::Group:
        case PresetPool::Effect:
        case PresetPool::All:      break;
    }
    return Family::None;
}

static bool poolTakes (PresetPool p, const juce::String& attr)
{
    if (p == PresetPool::All) return true;
    const auto f = poolFamily (p);
    return f != Family::None && familyOf (attr) == f;
}

static bool poolTakesEffects (PresetPool p) { return p == PresetPool::Effect || p == PresetPool::All; }

//==============================================================================
juce::var Preset::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("values", valueMapToVar (values));

    auto* bt = new juce::DynamicObject();
    for (auto& [type, attrs] : byType)
    {
        auto* a = new juce::DynamicObject();
        for (auto& [k, v] : attrs) a->setProperty (k, v);
        bt->setProperty (type, juce::var (a));
    }
    o->setProperty ("byType", juce::var (bt));

    juce::Array<juce::var> fx;
    for (auto& e : effects) fx.add (e.toVar());
    o->setProperty ("effects", fx);

    juce::Array<juce::var> fids;
    for (int f : fixtures) fids.add (f);
    o->setProperty ("fixtures", fids);
    o->setProperty ("swatch", (juce::int64) swatch);
    o->setProperty ("label", (juce::int64) label);
    juce::Array<juce::var> sw;
    for (auto c : swatches) sw.add ((juce::int64) c);
    o->setProperty ("swatches", sw);
    return juce::var (o);
}

Preset Preset::fromVar (const juce::var& v)
{
    Preset p;
    p.name = v["name"].toString();
    p.values = valueMapFromVar (v["values"]);
    if (auto* bt = v["byType"].getDynamicObject())
        for (auto& t : bt->getProperties())
            if (auto* a = t.value.getDynamicObject())
                for (auto& kv : a->getProperties())
                    p.byType[t.name.toString()][kv.name.toString()] = (float) (double) kv.value;
    if (auto* fx = v["effects"].getArray())
        for (auto& e : *fx) p.effects.push_back (EffectParams::fromVar (e));
    if (auto* fids = v["fixtures"].getArray())
        for (auto& f : *fids) p.fixtures.push_back ((int) f);
    p.swatch = (juce::uint32) (juce::int64) v["swatch"];
    p.label = (juce::uint32) (juce::int64) v["label"];
    if (auto* sw = v["swatches"].getArray())
        for (auto& c : *sw) p.swatches.push_back ((juce::uint32) (juce::int64) c);
    if (p.swatches.empty() && p.swatch != 0) p.swatches.push_back (p.swatch);
    return p;
}

//==============================================================================
const Preset* PresetLibrary::find (PresetPool pool, int number) const
{
    auto p = pools.find (pool);
    if (p == pools.end()) return nullptr;
    auto it = p->second.find (number);
    return it != p->second.end() ? &it->second : nullptr;
}

const std::map<int, Preset>& PresetLibrary::pool (PresetPool p) const
{
    static const std::map<int, Preset> empty;
    auto it = pools.find (p);
    return it != pools.end() ? it->second : empty;
}

juce::String PresetLibrary::store (PresetPool pool, int number, const Programmer& prog, const Patch& patch,
                                   const FixtureLibrary& lib, PresetStoreMode mode)
{
    Preset fresh;

    if (pool == PresetPool::Group)
    {
        if (prog.selection().empty()) return "Select fixtures to store a group";
        fresh.fixtures = prog.selection();
    }
    else
    {
        for (auto& [fid, attrs] : prog.allValues())
            for (auto& [a, v] : attrs)
                if (poolTakes (pool, a))
                    fresh.values[fid][a] = v;

        if (poolTakesEffects (pool))
            fresh.effects = prog.effects();

        if (fresh.values.empty() && fresh.effects.empty())
            return pool == PresetPool::Effect ? juce::String ("No effect in the programmer")
                                              : "No " + poolName (pool) + " values in the programmer";

        // fixture order: selection first, then any other fixture with values
        for (int t : prog.selection())
            if (const int fid = targetFid (t); fresh.values.count (fid)
                && std::find (fresh.fixtures.begin(), fresh.fixtures.end(), fid) == fresh.fixtures.end())
                fresh.fixtures.push_back (fid);
        for (auto& [fid, attrs] : fresh.values)
            if (std::find (fresh.fixtures.begin(), fresh.fixtures.end(), fid) == fresh.fixtures.end())
                fresh.fixtures.push_back (fid);

        // per-type fallback, from the first fixture of each type
        for (int fid : fresh.fixtures)
            if (auto* pf = patch.find (fid))
                if (! fresh.byType.count (pf->typeId))
                    fresh.byType[pf->typeId] = fresh.values[fid];

        // colours for the tile: one per fixture in order (shows gradients)
        for (int fid : fresh.fixtures)
            if (auto c = colourOfValues (fresh.values[fid]); c != 0 && fresh.swatches.size() < 32)
                fresh.swatches.push_back (c);
        if (! fresh.swatches.empty()) fresh.swatch = fresh.swatches.front();
    }

    auto& slot = pools[pool][number];
    const bool existed = ! slot.fixtures.empty() || ! slot.values.empty() || ! slot.effects.empty();

    if (mode == PresetStoreMode::Merge && existed)
    {
        for (auto& [fid, attrs] : fresh.values)
            for (auto& [a, v] : attrs) slot.values[fid][a] = v;
        for (auto& [t, attrs] : fresh.byType)
            for (auto& [a, v] : attrs) slot.byType[t][a] = v;
        for (int f : fresh.fixtures)
            if (std::find (slot.fixtures.begin(), slot.fixtures.end(), f) == slot.fixtures.end())
                slot.fixtures.push_back (f);
        for (auto& e : fresh.effects)
        {
            bool replaced = false;
            for (auto& x : slot.effects)
                if (x.sameSlot (e)) { x = e; replaced = true; }
            if (! replaced) slot.effects.push_back (e);
        }
        if (slot.swatch == 0) slot.swatch = fresh.swatch;   // keep the look of the first fixture
        for (auto c : fresh.swatches)
            if (slot.swatches.size() < 32) slot.swatches.push_back (c);
    }
    else
    {
        const auto keepName = slot.name;
        const auto keepLabel = slot.label;
        slot = fresh;
        slot.name = keepName.isNotEmpty() ? keepName : poolName (pool) + " " + juce::String (number);
        slot.label = keepLabel;
    }
    juce::ignoreUnused (lib);
    return {};
}

bool PresetLibrary::recall (PresetPool pool, int number, Programmer& prog, const Patch& patch,
                            const FixtureLibrary& lib, bool addToSelection) const
{
    auto* p = find (pool, number);
    if (p == nullptr) return false;

    if (pool == PresetPool::Group)
    {
        prog.lastGroup = addToSelection ? 0 : number;
        std::vector<int> sel = addToSelection ? prog.selection() : std::vector<int>();
        for (int f : p->fixtures)
            if (patch.find (f) != nullptr && std::find (sel.begin(), sel.end(), f) == sel.end())
                sel.push_back (f);
        prog.setSelection (sel);
        return true;
    }

    std::vector<int> targets = prog.selection();
    if (targets.empty())
    {
        for (int f : p->fixtures)
            if (patch.find (f) != nullptr) targets.push_back (f);
        prog.setSelection (targets);
    }

    for (int i = 0; i < (int) targets.size(); ++i)
        for (auto& [k, v] : valuesFor (pool, number, targets[(size_t) i], i, patch, lib))
            prog.set (targets[(size_t) i], k, v);

    for (auto e : p->effects)
    {
        if (! prog.selection().empty())
            e.fixtures = prog.selection();
        prog.setEffect (e);
    }

    // remember where the values came from (cues stored now reference the preset)
    for (int t : targets) prog.notePreset (t, poolKey (pool), number);
    return true;
}

std::map<juce::String, float> PresetLibrary::valuesFor (PresetPool pool, int number, int target, int index,
                                                       const Patch& patch, const FixtureLibrary& lib) const
{
    std::map<juce::String, float> out;
    auto* p = find (pool, number);
    auto* pf = patch.find (target);
    if (p == nullptr || pf == nullptr) return out;
    auto* type = lib.find (pf->typeId);
    if (type == nullptr) return out;
    const int fid = targetFid (target);

    // exact fixture first; otherwise by position (so a stored gradient lands as a
    // gradient on any selection), then by type
    const std::map<juce::String, float>* src = nullptr;
    if (auto it = p->values.find (fid); it != p->values.end()) src = &it->second;
    else
    {
        std::vector<const std::map<juce::String, float>*> ordered;
        for (int f : p->fixtures)
            if (auto it2 = p->values.find (f); it2 != p->values.end()) ordered.push_back (&it2->second);
        if (! ordered.empty())                                                  src = ordered[(size_t) index % ordered.size()];
        else if (auto bt = p->byType.find (pf->typeId); bt != p->byType.end()) src = &bt->second;
    }
    if (src == nullptr) return out;

    const auto slots = type->attributeSlots();
    auto hasSlot = [&slots] (const juce::String& k)
    {
        for (auto& s : slots) if (s.attribute == k) return true;
        return false;
    };
    if (! isCellTarget (target))
    {
        for (auto& [a, v] : *src)
            if (hasSlot (a)) out[a] = v;
        return out;
    }
    // a pixel takes the stored value of the same pixel, else the first one of that attribute
    const int cell = targetCell (target);
    std::map<juce::String, bool> exact;
    for (auto& [a, v] : *src)
    {
        const auto k = cellKey (baseAttribute (a), cell);
        if (! hasSlot (k)) continue;
        const bool isExact = cellOf (a) == cell;
        if (out.count (k) == 0 || (isExact && ! exact[k])) { out[k] = v; exact[k] = isExact; }
    }
    return out;
}

void PresetLibrary::rename (PresetPool pool, int number, const juce::String& name)
{
    auto p = pools.find (pool);
    if (p == pools.end()) return;
    if (auto it = p->second.find (number); it != p->second.end())
        it->second.name = name;
}

int PresetLibrary::setGroup (const juce::String& name, const std::vector<int>& fixtures)
{
    auto& groups = pools[PresetPool::Group];
    int number = 0;
    for (auto& [n, p] : groups)
        if (p.name == name) { number = n; break; }
    if (number == 0)
        for (number = 1; groups.count (number); ++number) {}
    auto& g = groups[number];
    g.name = name;
    g.fixtures = fixtures;
    return number;
}

void PresetLibrary::setLabel (PresetPool pool, int number, juce::uint32 argb)
{
    auto p = pools.find (pool);
    if (p == pools.end()) return;
    if (auto it = p->second.find (number); it != p->second.end())
        it->second.label = argb;
}

void PresetLibrary::remove (PresetPool pool, int number)
{
    auto p = pools.find (pool);
    if (p != pools.end()) p->second.erase (number);
}

bool PresetLibrary::copy (PresetPool pool, int from, int to, bool move)
{
    auto p = pools.find (pool);
    if (p == pools.end() || from == to || p->second.count (from) == 0) return false;
    auto& m = p->second;
    if (move)
    {
        auto src = m[from];
        if (m.count (to) > 0) m[from] = m[to]; else m.erase (from);
        m[to] = src;
    }
    else
    {
        auto c = m[from];
        for (auto& e : c.effects) e.id = EffectParams::nextId();
        m[to] = c;
    }
    return true;
}

juce::var PresetLibrary::toVar() const
{
    auto* root = new juce::DynamicObject();
    for (auto& [pool, presets] : pools)
    {
        juce::Array<juce::var> arr;
        for (auto& [num, preset] : presets)
        {
            auto v = preset.toVar();
            v.getDynamicObject()->setProperty ("number", num);
            arr.add (v);
        }
        root->setProperty (poolKey (pool), arr);
    }
    return juce::var (root);
}

void PresetLibrary::fromVar (const juce::var& v)
{
    pools.clear();
    if (auto* root = v.getDynamicObject())
        for (auto& prop : root->getProperties())
            if (auto pool = poolFromKey (prop.name.toString()))
                if (auto* arr = prop.value.getArray())
                    for (auto& p : *arr)
                        pools[*pool][juce::jmax (1, (int) p["number"])] = Preset::fromVar (p);
}

} // namespace pl
