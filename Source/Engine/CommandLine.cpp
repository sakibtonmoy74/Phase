#include "CommandLine.h"

namespace pl
{

std::vector<juce::String> programmerKeys (const Patch& patch, const FixtureLibrary& lib, int target, const juce::String& base)
{
    std::vector<juce::String> keys;
    const int cell = targetCell (target);
    if (auto* pf = patch.find (target))
        if (auto* type = lib.find (pf->typeId))
            for (auto& s : type->attributeSlots())
                if (baseAttribute (s.attribute) == base && (cell == 0 || cellOf (s.attribute) == cell))
                    keys.push_back (s.attribute);
    // the whole fixture's Dimmer is its master dimmer, not every pixel dimmer
    if (cell == 0 && base == "Dimmer" && std::find (keys.begin(), keys.end(), juce::String ("Dimmer")) != keys.end())
        keys = { "Dimmer" };
    return keys;
}

std::vector<float> spreadValues (const std::vector<float>& anchors, int count)
{
    std::vector<float> out ((size_t) juce::jmax (0, count), anchors.empty() ? 0.0f : anchors.front());
    if (anchors.size() < 2 || count < 2) return out;
    const int segments = (int) anchors.size() - 1;
    for (int i = 0; i < count; ++i)
    {
        const float t = (float) i / (float) (count - 1) * (float) segments;
        const int seg = juce::jmin (segments - 1, (int) t);
        const float f = t - (float) seg;
        out[(size_t) i] = anchors[(size_t) seg] + (anchors[(size_t) seg + 1] - anchors[(size_t) seg]) * f;
    }
    return out;
}

namespace command
{
    juce::String attributeFromWord (const juce::String& word)
    {
        const auto w = word.toLowerCase();
        static const std::vector<std::pair<const char*, const char*>> names {
            { "dim", "Dimmer" }, { "dimmer", "Dimmer" }, { "intensity", "Dimmer" },
            { "pan", "Pan" }, { "tilt", "Tilt" }, { "zoom", "Zoom" }, { "focus", "Focus" }, { "iris", "Iris" },
            { "frost", "Frost" }, { "shutter", "Shutter" }, { "strobe", "Strobe" }, { "prism", "Prism" },
            { "red", "ColorR" }, { "green", "ColorG" }, { "blue", "ColorB" }, { "white", "ColorW" },
            { "amber", "ColorA" }, { "uv", "ColorUV" }, { "lime", "ColorLime" }, { "cyan", "ColorC" },
            { "magenta", "ColorM" }, { "yellow", "ColorY" }, { "cto", "CTO" }, { "gobo", "Gobo1" }, { "gobo1", "Gobo1" },
            { "gobo2", "Gobo2" }, { "wheel", "ColorWheel" }, { "colorwheel", "ColorWheel" },
        };
        for (auto& [k, v] : names) if (w == k) return v;
        for (auto& a : allAttributes())
            if (w == juce::String (a.name).toLowerCase() || w == juce::String (a.label).toLowerCase().removeCharacters (" "))
                return a.name;
        return {};
    }

    namespace
    {
        bool isNumber (const juce::String& t)
        {
            return t.isNotEmpty() && t.containsOnly ("0123456789.");
        }

        /** "12" → 12, "301.4" → pixel 4 of 301. */
        int targetFromToken (const juce::String& t)
        {
            if (t.containsChar ('.'))
                return cellTarget (t.upToFirstOccurrenceOf (".", false, false).getIntValue(),
                                   t.fromFirstOccurrenceOf (".", false, false).getIntValue());
            return t.getIntValue();
        }

        float valueFromToken (const juce::String& t, bool& ok)
        {
            ok = true;
            if (t == "full" || t == "f") return 1.0f;
            if (t == "zero" || t == "z") return 0.0f;
            if (isNumber (t)) return juce::jlimit (0.0f, 100.0f, t.getFloatValue()) / 100.0f;
            ok = false;
            return 0.0f;
        }
    }

    Result execute (const juce::String& line, Programmer& prog, const Patch& patch, const FixtureLibrary& lib, const PresetLibrary& presets)
    {
        Result r;
        auto text = line.toLowerCase().replace ("+", " + ").replace ("-", " - ").replace (">", " thru ");
        auto tokens = juce::StringArray::fromTokens (text, " ,", "");
        tokens.removeEmptyStrings();
        if (tokens.isEmpty()) return r;

        // single words
        if (tokens.size() == 1 && (tokens[0] == "clear" || tokens[0] == "c"))
        {
            prog.clearStep();
            r.selectionChanged = r.valuesChanged = true;
            r.message = "Clear";
            return r;
        }
        if (tokens.size() == 1 && (tokens[0] == "highlight" || tokens[0] == "hi"))
        {
            prog.setHighlight (! prog.isHighlight());
            r.highlightChanged = true;
            r.message = prog.isHighlight() ? "Highlight on" : "Highlight off";
            return r;
        }

        if (tokens.size() == 1 && (tokens[0] == "next" || tokens[0] == "prev" || tokens[0] == "previous"))
        {
            if (! prog.step (tokens[0] == "next" ? 1 : -1)) { r.error = "Select 2 or more fixtures to step through"; return r; }
            r.selectionChanged = true;
            r.message = juce::String (prog.stepPosition() + 1) + " of " + juce::String ((int) prog.stepTargets().size());
            return r;
        }
        if (tokens.size() == 1 && tokens[0] == "all")
        {
            r.selectionChanged = prog.stepAll();
            r.message = "All";
            return r;
        }
        if (tokens.size() == 1 && (tokens[0] == "lowlight" || tokens[0] == "lo"))
        {
            prog.setLowlight (! prog.isLowlight());
            r.highlightChanged = true;
            r.message = prog.isLowlight() ? "Lowlight on" : "Lowlight off";
            return r;
        }

        int i = 0;
        auto next = [&] { return i < tokens.size() ? tokens[i] : juce::String(); };

        // preset T.N
        if (next() == "preset" || next() == "p")
        {
            ++i;
            const auto ref = next();
            static const PresetPool pools[] { PresetPool::Dimmer, PresetPool::Position, PresetPool::Gobo, PresetPool::Color,
                                              PresetPool::Beam, PresetPool::Focus, PresetPool::Effect, PresetPool::All };
            const int type = ref.upToFirstOccurrenceOf (".", false, false).getIntValue();
            const int num = ref.fromFirstOccurrenceOf (".", false, false).getIntValue();
            if (type < 1 || type > 8 || num < 1 || ! ref.containsChar ('.'))
                { r.error = "Preset needs type.number, e.g. preset 4.2"; return r; }
            if (! presets.recall (pools[type - 1], num, prog, patch, lib, false))
                { r.error = poolName (pools[type - 1]) + " " + juce::String (num) + " is empty"; return r; }
            r.valuesChanged = r.selectionChanged = true;
            r.message = "Preset " + ref;
            return r;
        }

        // --- selection part ---------------------------------------------------
        std::vector<int> list;
        bool hasSelection = false, removing = false;
        auto addTargets = [&] (const std::vector<int>& targets)
        {
            for (int t : targets)
            {
                auto it = std::find (list.begin(), list.end(), t);
                if (removing) { if (it != list.end()) list.erase (it); }
                else if (it == list.end() && patch.find (t) != nullptr) list.push_back (t);
            }
        };

        while (i < tokens.size() && next() != "at")
        {
            const auto t = next();
            if (t == "+") { removing = false; ++i; continue; }
            if (t == "-") { removing = true; ++i; continue; }
            if (t == "fixture" || t == "fix") { ++i; continue; }
            if (t == "group" || t == "g")
            {
                ++i;
                const int n = next().getIntValue();
                auto* g = presets.find (PresetPool::Group, n);
                if (g == nullptr) { r.error = "Group " + next() + " doesn't exist"; return r; }
                addTargets (g->fixtures);
                hasSelection = true;
                ++i;
                continue;
            }
            if (! attributeFromWord (t).isEmpty() && (i + 1 < tokens.size() && tokens[i + 1] == "at")) break;
            if (! isNumber (t)) { r.error = "Don't understand \"" + t + "\""; return r; }

            const int first = targetFromToken (t);
            ++i;
            if (next() == "thru" || next() == "t")
            {
                ++i;
                if (! isNumber (next())) { r.error = "thru needs a number after it"; return r; }
                const int last = targetFromToken (next());
                ++i;
                std::vector<int> range;
                if (isCellTarget (first) && isCellTarget (last) && targetFid (first) == targetFid (last))
                {
                    const int fid = targetFid (first), a = targetCell (first), b = targetCell (last);
                    for (int c = a; a <= b ? c <= b : c >= b; c += a <= b ? 1 : -1) range.push_back (cellTarget (fid, c));
                }
                else
                {
                    // every patched fixture between the two FIDs, in the typed direction
                    const int a = targetFid (first), b = targetFid (last);
                    std::vector<int> fids;
                    for (auto& f : patch.fixtures())
                        if (f.fid >= juce::jmin (a, b) && f.fid <= juce::jmax (a, b)) fids.push_back (f.fid);
                    if (a > b) std::reverse (fids.begin(), fids.end());
                    range = fids;
                }
                addTargets (range);
            }
            else
                addTargets ({ first });
            hasSelection = true;
        }

        if (hasSelection)
        {
            if (list.empty()) { r.error = "No patched fixtures in that selection"; return r; }
            prog.setSelection (list);
            r.selectionChanged = true;
            r.message = juce::String ((int) list.size()) + " selected";
        }

        // --- value part: [attribute] at v [thru v ...] -------------------------
        juce::String attribute = "Dimmer";
        if (i < tokens.size() && next() != "at")
        {
            attribute = attributeFromWord (next());
            ++i;
        }
        if (i >= tokens.size()) return r;
        if (next() != "at") { r.error = "Expected \"at\""; return r; }
        ++i;

        std::vector<float> anchors;
        while (i < tokens.size())
        {
            bool ok = false;
            anchors.push_back (valueFromToken (next(), ok));
            if (! ok) { r.error = "Not a value: \"" + next() + "\""; return r; }
            ++i;
            if (i < tokens.size() && (next() == "thru" || next() == "t")) ++i;
            else break;
        }
        if (anchors.empty()) { r.error = "at needs a value (0-100, full)"; return r; }
        if (i < tokens.size()) { r.error = "Don't understand \"" + next() + "\""; return r; }

        std::vector<int> targets;
        for (int t : prog.selection())
            if (! programmerKeys (patch, lib, t, attribute).empty()) targets.push_back (t);
        if (targets.empty())
        {
            r.error = prog.selection().empty() ? juce::String ("Select fixtures first")
                                               : "No " + attributeLabel (attribute) + " on the selection";
            return r;
        }
        const auto values = spreadValues (anchors, (int) targets.size());
        for (size_t k = 0; k < targets.size(); ++k)
            for (auto& key : programmerKeys (patch, lib, targets[k], attribute))
                prog.set (targets[k], key, values[k]);
        r.valuesChanged = true;
        r.message = (r.message.isNotEmpty() ? r.message + ", " : juce::String()) + attributeLabel (attribute) + " at "
                  + juce::String (juce::roundToInt (anchors.front() * 100.0f))
                  + (anchors.size() > 1 ? " thru " + juce::String (juce::roundToInt (anchors.back() * 100.0f)) : juce::String());
        return r;
    }
}

} // namespace pl
