#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include "Effects.h"
#include "Playback.h"

namespace pl
{

/** Fan / align modes: spread a change across the selection in selection order. */
enum class FanMode { Off, Spread, RampUp, RampDown, V };

inline juce::String fanModeName (FanMode m)
{
    switch (m)
    {
        case FanMode::Spread:   return "Spread";
        case FanMode::RampUp:   return "Ramp >";
        case FanMode::RampDown: return "Ramp <";
        case FanMode::V:        return "V";
        case FanMode::Off:      break;
    }
    return "Off";
}

/** How much of a change fixture `i` of `n` receives.
    Spread: centre stays, ends move opposite ways (-1..1).
    Ramp >: first stays, last gets all (0..1).  Ramp <: the reverse.
    V: centre stays, both ends move the same way. */
inline float fanWeight (FanMode m, int i, int n)
{
    if (m == FanMode::Off || n < 2) return 1.0f;
    const float t = (float) i / (float) (n - 1);
    switch (m)
    {
        case FanMode::Spread:   return 2.0f * t - 1.0f;
        case FanMode::RampUp:   return t;
        case FanMode::RampDown: return 1.0f - t;
        case FanMode::V:        return std::abs (2.0f * t - 1.0f);
        case FanMode::Off:      break;
    }
    return 1.0f;
}

/** Position 0..1 of fixture `i` along a gradient (start → end colour). */
inline float fanGradientPosition (FanMode m, int i, int n)
{
    if (n < 2) return 1.0f;
    const float t = (float) i / (float) (n - 1);
    if (m == FanMode::RampDown) return 1.0f - t;
    if (m == FanMode::V)        return std::abs (2.0f * t - 1.0f);
    return t;
}

/** The programmer holds manually set attribute values (normalised 0..1)
    and the current fixture selection. Selection order is kept, because
    effects will later spread phase across fixtures in selection order. */
class Programmer
{
public:
    // --- values
    /** `fid` may be a cell target: values are stored per fixture under the full key
        ("ColorR@3"), so pass keys that already carry the cell. */
    void set (int fid, const juce::String& attr, float value)
    {
        values[targetFid (fid)][attr] = juce::jlimit (0.0f, 1.0f, value);
        forgetReference (fid, attr);
    }

    // --- preset references: which preset a target's values came from, so a cue
    //     stored from here plays the preset (and follows it when it's updated)
    void notePreset (int target, const juce::String& poolKey, int number) { presetRefs[target][poolKey] = number; }
    const std::map<int, std::map<juce::String, int>>& presetReferences() const { return presetRefs; }
    /** Last group recalled (stored cues use the group, live, when the selection still matches it). */
    int lastGroup = 0;

    std::optional<float> get (int fid, const juce::String& attr) const
    {
        auto f = values.find (targetFid (fid));
        if (f == values.end()) return {};
        auto a = f->second.find (attr);
        if (a == f->second.end()) return {};
        return a->second;
    }

    void clearValues()                     { values.clear(); effectList.clear(); presetRefs.clear(); }
    void clearAttribute (int fid, const juce::String& attr)
    {
        forgetReference (fid, attr);
        auto f = values.find (targetFid (fid));
        if (f != values.end())
        {
            f->second.erase (attr);
            if (f->second.empty()) values.erase (f);
        }
    }
    bool hasValues() const                 { return ! values.empty() || ! effectList.empty(); }
    const std::map<int, std::map<juce::String, float>>& allValues() const { return values; }

    // --- selection
    const std::vector<int>& selection() const { return selected; }
    bool isSelected (int fid) const
    {
        return std::find (selected.begin(), selected.end(), fid) != selected.end();
    }
    void select (int fid, bool addToSelection)
    {
        if (! addToSelection) selected.clear();
        if (! isSelected (fid)) selected.push_back (fid);
    }
    void toggle (int fid)
    {
        if (isSelected (fid)) selected.erase (std::find (selected.begin(), selected.end(), fid));
        else selected.push_back (fid);
    }
    void setSelection (std::vector<int> fids) { selected = std::move (fids); }
    void clearSelection()                     { selected.clear(); }

    /** MA-style "Clear": first press clears selection, second clears values. */
    void clearStep()
    {
        if (! selected.empty()) selected.clear();
        else clearValues();
    }

    // --- effects (one per kind; attribute effects one per attribute)
    const std::vector<EffectParams>& effects() const { return effectList; }
    const EffectParams* effect (EffectKind k, const juce::String& attribute = {}) const
    {
        for (auto& e : effectList)
            if (e.kind == k && (k != EffectKind::Attribute || e.attribute == attribute)) return &e;
        return nullptr;
    }
    void setEffect (const EffectParams& e)
    {
        for (auto& [t, refs] : presetRefs) refs.erase ("effects");
        for (auto& existing : effectList)
            if (existing.sameSlot (e)) { existing = e; return; }
        effectList.push_back (e);
    }
    void removeEffect (EffectKind k, const juce::String& attribute = {})
    {
        effectList.erase (std::remove_if (effectList.begin(), effectList.end(),
                                          [&] (const EffectParams& e)
                                          { return e.kind == k && (k != EffectKind::Attribute || e.attribute == attribute); }),
                          effectList.end());
    }

    // --- highlight: selected fixtures (or a given list) at full, open white
    bool isHighlight() const                       { return highlight; }
    void setHighlight (bool on)                    { highlight = on; highlightList.clear(); }
    /** Highlight these instead of the selection (the Patch view uses this). */
    void setHighlightList (std::vector<int> targets) { highlight = ! targets.empty(); highlightList = std::move (targets); }
    const std::vector<int>& highlightTargets() const { return highlightList.empty() ? selected : highlightList; }

    // --- Next / Prev (MA-style): step through the selection one fixture at a time.
    // The full selection is kept as the step set; All brings it back.
    bool isStepping() const
    {
        return stepIndex >= 0 && stepIndex < (int) stepSet.size() && selected.size() == 1 && selected[0] == stepSet[(size_t) stepIndex];
    }
    const std::vector<int>& stepTargets() const { return stepSet; }
    int stepPosition() const { return isStepping() ? stepIndex : -1; }

    /** Next (+1) / Prev (-1). Returns false when there is nothing to step through. */
    bool step (int direction)
    {
        if (! isStepping())
        {
            if (selected.size() < 2) return false;
            stepSet = selected;
            stepIndex = direction > 0 ? 0 : (int) stepSet.size() - 1;
        }
        else
        {
            const int n = (int) stepSet.size();
            stepIndex = ((stepIndex + direction) % n + n) % n;
        }
        selected = { stepSet[(size_t) stepIndex] };
        return true;
    }

    /** All: back to the whole step set. */
    bool stepAll()
    {
        if (stepSet.empty() || ! isStepping()) return false;
        selected = stepSet;
        stepIndex = -1;
        return true;
    }

    // --- lowlight: the step set (or the selection) in a chosen colour, dimmed.
    // With Highlight and Next / Prev, the current fixture is highlighted and the
    // others sit in the lowlight colour, so you see the whole group while fixing one.
    bool isLowlight() const                         { return lowlight; }
    void setLowlight (bool on)                      { lowlight = on; }
    juce::uint32 lowlightColour = 0xff2040ffu;      // ARGB
    float lowlightLevel = 0.4f;                     // dimmer 0..1
    float lowlightWheel = -1.0f;                    // colour wheel position 0..1 for fixtures without RGB / CMY (< 0 = open)
    const std::vector<int>& lowlightTargets() const { return isStepping() ? stepSet : selected; }

    // --- undo snapshots
    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("values", valueMapToVar (values));
        juce::Array<juce::var> fx, sel;
        for (auto& e : effectList) fx.add (e.toVar());
        for (int t : selected) sel.add (t);
        o->setProperty ("effects", fx);
        o->setProperty ("selection", sel);
        auto* refs = new juce::DynamicObject();
        for (auto& [t, r] : presetRefs)
        {
            auto* inner = new juce::DynamicObject();
            for (auto& [k, n] : r) inner->setProperty (k, n);
            refs->setProperty (juce::String (t), juce::var (inner));
        }
        o->setProperty ("presetRefs", juce::var (refs));
        o->setProperty ("lastGroup", lastGroup);
        return juce::var (o);
    }

    void fromVar (const juce::var& v)
    {
        values = valueMapFromVar (v["values"]);
        effectList.clear();
        selected.clear();
        if (auto* fx = v["effects"].getArray())
            for (auto& e : *fx) effectList.push_back (EffectParams::fromVar (e));
        if (auto* sel = v["selection"].getArray())
            for (auto& t : *sel) selected.push_back ((int) t);
        presetRefs.clear();
        if (auto* refs = v["presetRefs"].getDynamicObject())
            for (auto& prop : refs->getProperties())
                if (auto* inner = prop.value.getDynamicObject())
                    for (auto& r : inner->getProperties()) presetRefs[prop.name.toString().getIntValue()][r.name.toString()] = (int) r.value;
        lastGroup = (int) v["lastGroup"];
    }

private:
    void forgetReference (int target, const juce::String& attr)
    {
        auto drop = [&] (int t)
        {
            auto it = presetRefs.find (t);
            if (it == presetRefs.end()) return;
            it->second.erase (familyName (familyOf (attr)).toLowerCase());
            it->second.erase ("all");
            if (it->second.empty()) presetRefs.erase (it);
        };
        drop (target);
        if (! isCellTarget (target) && cellOf (attr) > 0) drop (cellTarget (target, cellOf (attr)));
    }

    std::map<int, std::map<juce::String, int>> presetRefs;   // target → pool key → preset number
    std::map<int, std::map<juce::String, float>> values;
    std::vector<EffectParams> effectList;
    std::vector<int> selected;
    bool highlight = false;
    std::vector<int> highlightList;
    std::vector<int> stepSet;
    int stepIndex = -1;
    bool lowlight = false;
};

} // namespace pl
