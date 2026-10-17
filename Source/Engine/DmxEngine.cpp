#include "DmxEngine.h"
#include <unordered_map>
#include <set>
#include "CommandLine.h"

namespace pl
{

DmxEngine::DmxEngine() : juce::Thread ("DMX Engine")
{
    playback.partResolver = [this] (const CuePart& part, std::vector<EffectParams>& fx) { return resolvePart (part, fx); };
    auto ips = localIPv4Addresses();
    if (! ips.isEmpty() && ! netSettings.outputs.empty())
        netSettings.outputs.front().localIp = ips[0];
}

DmxEngine::~DmxEngine() { stopEngine(); }

void DmxEngine::startEngine()
{
    if (! isThreadRunning())
        startRealtimeThread (juce::Thread::RealtimeOptions().withPriority (8));
}

void DmxEngine::stopEngine() { stopThread (2000); }

//==============================================================================
NetSettings DmxEngine::getNetSettings() const
{
    const juce::ScopedLock sl (netLock);
    return netSettings;
}

void DmxEngine::setNetSettings (const NetSettings& s)
{
    {
        const juce::ScopedLock sl (netLock);
        netSettings = s;
    }
    netDirty = true;
}

juce::String DmxEngine::getNetStatus() const
{
    const juce::ScopedLock sl (netLock);
    return netStatus;
}

bool DmxEngine::getUniverse (int universe, DmxUniverse& out) const
{
    const juce::ScopedLock sl (snapshotLock);
    auto it = snapshot.find (universe);
    if (it == snapshot.end())
        return false;
    out = it->second;
    return true;
}

std::vector<int> DmxEngine::outputUniverses() const
{
    const juce::ScopedLock sl (snapshotLock);
    std::vector<int> u;
    for (auto& [k, v] : snapshot)
        u.push_back (k);
    return u;
}

std::map<juce::String, float> DmxEngine::getFixtureOutput (int fid) const
{
    const juce::ScopedLock sl (snapshotLock);
    auto it = resolvedSnapshot.find (targetFid (fid));
    return it != resolvedSnapshot.end() ? it->second : std::map<juce::String, float>();
}

float DmxEngine::getAudioLinkLevel (int id) const
{
    const juce::ScopedLock sl (snapshotLock);
    auto it = audioLinkSnapshot.find (id);
    return it != audioLinkSnapshot.end() ? it->second : 0.0f;
}

void DmxEngine::updateAudioLinks (const std::vector<AudioLink>& links, const AudioLevels& audio, double dt,
                                  std::map<int, float>& levels)
{
    std::map<int, float> next;
    for (auto& l : links)
    {
        const float target = l.enabled ? l.shape (audio.get (l.source)) : 0.0f;
        auto it = levels.find (l.id);
        next[l.id] = l.smooth (it != levels.end() ? it->second : 0.0f, target, dt);
    }
    levels = std::move (next);
}

//==============================================================================
ValueMap DmxEngine::resolvePart (const CuePart& part, std::vector<EffectParams>& fx) const
{
    std::vector<int> targets;
    if (part.group > 0)
    {
        if (auto* g = presets.find (PresetPool::Group, part.group)) targets = g->fixtures;
    }
    else
        targets = part.fixtures;
    targets.erase (std::remove_if (targets.begin(), targets.end(), [this] (int t) { return patch.find (t) == nullptr; }), targets.end());

    ValueMap m;
    for (int i = 0; i < (int) targets.size(); ++i)
    {
        const int t = targets[(size_t) i], fid = targetFid (t);
        for (auto& [key, num] : part.presets)
        {
            auto pool = poolFromKey (key);
            if (! pool || *pool == PresetPool::Effect || *pool == PresetPool::Group) continue;
            for (auto& [k, v] : presets.valuesFor (*pool, num, t, i, patch, library)) m[fid][k] = v;
        }
        for (auto& [a, v] : part.values)
            for (auto& k : programmerKeys (patch, library, t, a)) m[fid][k] = v;
    }

    for (auto e : part.effects) { e.fixtures = targets; fx.push_back (e); }
    for (auto& [key, num] : part.presets)
        if (key == poolKey (PresetPool::Effect) || key == poolKey (PresetPool::All))
            if (auto* p = presets.find (key == "all" ? PresetPool::All : PresetPool::Effect, num))
                for (auto e : p->effects) { e.fixtures = targets; fx.push_back (e); }
    return m;
}

juce::String DmxEngine::storeFromProgrammer (int page, int slot, StoreMode mode, double when, int cueIndex)
{
    auto values = programmer.allValues();
    auto effectsList = programmer.effects();
    std::vector<CuePart> parts;

    // targets with the same preset references become one folder
    std::map<std::map<juce::String, int>, std::vector<int>> byRefs;
    for (int t : programmer.selection())
        if (auto it = programmer.presetReferences().find (t); it != programmer.presetReferences().end() && ! it->second.empty())
            byRefs[it->second].push_back (t);
    for (auto& [t, refs] : programmer.presetReferences())
        if (! refs.empty() && std::find (programmer.selection().begin(), programmer.selection().end(), t) == programmer.selection().end())
            byRefs[refs].push_back (t);

    std::set<int> groupFixtures;
    if (auto* g = presets.find (PresetPool::Group, programmer.lastGroup)) groupFixtures = { g->fixtures.begin(), g->fixtures.end() };

    for (auto& [refs, targets] : byRefs)
    {
        CuePart part;
        part.presets = refs;
        const std::set<int> mine (targets.begin(), targets.end());
        if (! groupFixtures.empty() && mine == groupFixtures) part.group = programmer.lastGroup;
        else part.fixtures = targets;

        juce::StringArray names;
        for (auto& [k, n] : refs) names.add (k + " " + juce::String (n));
        part.name = (part.group > 0 ? (presets.find (PresetPool::Group, part.group)->name) : juce::String ((int) targets.size()) + " fixtures")
                  + ": " + names.joinIntoString (", ");

        // the referenced values aren't stored as plain values any more
        for (int t : targets)
        {
            auto fv = values.find (targetFid (t));
            if (fv == values.end()) continue;
            for (auto it = fv->second.begin(); it != fv->second.end();)
            {
                const auto fam = familyName (familyOf (it->first)).toLowerCase();
                const bool coveredCell = ! isCellTarget (t) || cellOf (it->first) == targetCell (t);
                if (coveredCell && (refs.count (fam) || refs.count ("all"))) it = fv->second.erase (it);
                else ++it;
            }
            if (fv->second.empty()) values.erase (fv);
        }
        // effects that came from a referenced effect preset
        for (auto& [k, n] : refs)
            if (k == "effects" || k == "all")
                if (auto* p = presets.find (k == "all" ? PresetPool::All : PresetPool::Effect, n))
                    for (auto& pe : p->effects)
                        effectsList.erase (std::remove_if (effectsList.begin(), effectsList.end(),
                                                           [&pe] (const EffectParams& x) { return x.id == pe.id; }), effectsList.end());

        // the folder's dimmer: if every fixture in it has the same plain dimmer, keep it on the folder
        float dim = -1.0f; bool same = true;
        for (int t : targets)
            if (auto fv = values.find (targetFid (t)); fv != values.end())
                if (auto d = fv->second.find (isCellTarget (t) ? cellKey ("Dimmer", targetCell (t)) : juce::String ("Dimmer")); d != fv->second.end())
                {
                    if (dim < 0.0f) dim = d->second; else same &= std::abs (dim - d->second) < 0.0005f;
                    continue;
                }
                else same = false;
            else same = false;
        if (same && dim >= 0.0f)
        {
            part.values["Dimmer"] = dim;
            for (int t : targets)
                if (auto fv = values.find (targetFid (t)); fv != values.end())
                {
                    fv->second.erase (isCellTarget (t) ? cellKey ("Dimmer", targetCell (t)) : juce::String ("Dimmer"));
                    if (fv->second.empty()) values.erase (fv);
                }
        }
        parts.push_back (part);
    }

    return playback.store (page, slot, values, effectsList, mode, when, parts, cueIndex);
}

//==============================================================================
void DmxEngine::executorButton (int page, int slot, int index, bool down, double when)
{
    auto* exec = playback.find (page, slot);
    if (exec != nullptr && index >= 0 && index < Executor::numButtons
        && exec->buttons[index] == ButtonFunction::Select)
    {
        if (! down || exec->cues.empty()) return;
        const auto& st = playback.state (page, slot);
        const int cue = st.isOn() ? st.cue : 0;
        std::vector<int> sel;
        auto add = [&sel] (int t) { if (std::find (sel.begin(), sel.end(), t) == sel.end()) sel.push_back (t); };
        for (auto& ce : playback.effectsAt (*exec, cue))
            for (int t : ce.effect.fixtures) add (t);
        for (auto& [fid, attrs] : Playback::trackedState (*exec, cue)) add (fid);
        programmer.setSelection (sel);
        return;
    }
    playback.button (page, slot, index, down, when);
}

//==============================================================================
void DmxEngine::advanceEffects (const Programmer& prog, const Playback& playback, const Masters& m,
                                double dt, std::map<int, double>& phases, const std::vector<PixelMap>* maps)
{
    std::map<int, double> next;
    auto advance = [&] (const EffectParams& e)
    {
        if (next.count (e.id)) return;                     // shared id: advance once
        const float master = e.family == Family::Position ? m.movementSpeed.load() : m.dimmerSpeed.load();
        const double cycles = dt * (double) e.bpm / 60.0 * (double) Masters::speedMultiplier (master);
        auto it = phases.find (e.id);
        next[e.id] = (it != phases.end() ? it->second : 0.0) + cycles;
    };

    for (auto& e : prog.effects())
        advance (e);
    if (maps != nullptr)
        for (auto& pm : *maps)
            if (pm.enabled && pm.source == pixelmap::effectSource())
                advance (pm.effect);
    for (auto& a : playback.activeInOrder())
    {
        for (auto& e : a.state->liveEffects)
            advance (e);
    }

    for (auto& [id, v] : next)                             // keep numbers small
        if (v > 1.0e6) next[id] = v - std::floor (v);
    phases = std::move (next);                             // drops effects that stopped
}

//==============================================================================
namespace
{
    /** Writes an RGB colour into whatever colour system the fixture has
        (RGB, RGBW with white extracted, CMY). */
    void writeColour (std::map<juce::String, float>& vals, const FixtureType& type, RGBf c, int onlyCell = -1)
    {
        // per cell (cell 0 = whole fixture), so pixel fixtures take the colour on every cell
        std::map<int, int> flags;   // cell → bit 1 RGB, 2 W, 4 CMY
        for (auto& a : type.attributes)
        {
            if (a.attribute == "ColorR") flags[a.cell] |= 1;
            if (a.attribute == "ColorW") flags[a.cell] |= 2;
            if (a.attribute == "ColorC") flags[a.cell] |= 4;
        }
        for (auto& [cell, f] : flags)
        {
            if (onlyCell >= 0 && cell != onlyCell) continue;
            if (f & 4)
            {
                vals[cellKey ("ColorC", cell)] = 1.0f - c.r;
                vals[cellKey ("ColorM", cell)] = 1.0f - c.g;
                vals[cellKey ("ColorY", cell)] = 1.0f - c.b;
            }
            if (f == 2)                         // white-only cells (separate white pixels): the colour's white part
                vals[cellKey ("ColorW", cell)] = juce::jmin (c.r, c.g, c.b);
            if (f & 1)
            {
                float r = c.r, g = c.g, b = c.b;
                if (f & 2) { const float w = juce::jmin (r, g, b); r -= w; g -= w; b -= w; vals[cellKey ("ColorW", cell)] = w; }
                vals[cellKey ("ColorR", cell)] = r;
                vals[cellKey ("ColorG", cell)] = g;
                vals[cellKey ("ColorB", cell)] = b;
            }
        }
    }

    /** An effect with its targets indexed (fixtures and pixels). */
    struct IndexedEffect
    {
        const EffectParams* e;
        std::unordered_map<int, int> index;     // target → position in selection order
        bool hasCells = false;
    };

    struct Layer
    {
        ValueMap values;
        std::vector<IndexedEffect> effects;
        float level = 1.0f;                     // fader (or flash / swop)
        float dimmerLevel = 1.0f;               // 0 for executors held down by a Swop
        bool isProgrammer = false;
    };

    std::vector<IndexedEffect> indexEffects (const std::vector<EffectParams>& list)
    {
        std::vector<IndexedEffect> out;
        for (auto& e : list)
        {
            IndexedEffect ie { &e, {}, false };
            for (int i = 0; i < (int) e.fixtures.size(); ++i)
            {
                ie.index.emplace (e.fixtures[(size_t) i], i);
                ie.hasCells |= isCellTarget (e.fixtures[(size_t) i]);
            }
            out.push_back (std::move (ie));
        }
        return out;
    }
}

void DmxEngine::renderFrame (const RenderContext& ctx, UniverseMap& out,
                             std::map<int, std::map<juce::String, float>>* resolved)
{
    const float master = juce::jlimit (0.0f, 1.0f, ctx.grandMaster);

    // Layers in priority order: executors (oldest first), then the programmer
    std::vector<Layer> layers;
    const bool swopActive = ctx.playback.anySwop();
    for (auto& a : ctx.playback.activeInOrder())
    {
        auto fx = indexEffects (a.state->liveEffects);
        layers.push_back ({ Playback::currentValues (*a.state, ctx.now), std::move (fx), a.state->level(),
                            swopActive && ! a.state->swop ? 0.0f : 1.0f, false });
    }
    layers.push_back ({ ctx.programmer.allValues(), indexEffects (ctx.programmer.effects()), 1.0f, 1.0f, true });

    auto phaseOf = [&ctx] (int id)
    {
        auto it = ctx.effectPhases.find (id);
        return it != ctx.effectPhases.end() ? it->second : 0.0;
    };

    // Audio links: resolve their fixtures (group presets are live) and current value
    struct ActiveLink { const AudioLink* link; std::vector<int> fixtures; float value; };
    std::vector<ActiveLink> audioLinks;
    if (ctx.audioLinks != nullptr && ctx.audio != nullptr)
        for (auto& l : *ctx.audioLinks)
        {
            if (! l.enabled) continue;
            std::vector<int> fids = l.fixtures;
            if (l.groupNumber > 0 && ctx.presets != nullptr)
                if (auto* g = ctx.presets->find (PresetPool::Group, l.groupNumber))
                    fids = g->fixtures;
            for (auto& t : fids) t = targetFid (t);          // pixels: the link drives their fixture
            float level = l.shape (ctx.audio->get (l.source));
            if (ctx.audioLinkLevels != nullptr)
                if (auto it = ctx.audioLinkLevels->find (l.id); it != ctx.audioLinkLevels->end()) level = it->second;
            audioLinks.push_back ({ &l, fids, l.map (level) });
        }

    std::vector<const PixelMap*> pixelMaps;
    if (ctx.pixelMaps != nullptr)
        for (auto& m : *ctx.pixelMaps)
            if (m.enabled) pixelMaps.push_back (&m);

    for (auto& f : ctx.patch.fixtures())
    {
        auto* type = ctx.library.find (f.typeId);
        if (type == nullptr)
            continue;

        const int base = f.address - 1;
        const int footprint = type->footprint();
        if (base < 0 || base + footprint > 512)
            continue;
        auto& buf = out[f.universe];                     // value-initialised to zeros

        const auto slots = type->attributeSlots();

        // 1) raw defaults
        for (auto& s : slots)
            if (s.coarse >= 0)
                buf[(size_t) (base + s.coarse)] = (juce::uint8) juce::roundToInt (s.defaultNormalised * 255.0f);

        // 2) resolve layers
        std::map<juce::String, float> vals;              // LTP attributes
        float dimmerHtp = -1.0f, programmerDimmer = -1.0f;
        Vec2 posOffset;
        bool hasPosOffset = false;

        // audio for this fixture: "set" sits above executors and below the
        // programmer; "scale" multiplies the final value
        float audioDimmerSet = -1.0f, audioDimmerScale = 1.0f;
        std::map<juce::String, float> audioSet, audioScale;
        for (auto& al : audioLinks)
        {
            if (std::find (al.fixtures.begin(), al.fixtures.end(), f.fid) == al.fixtures.end()) continue;
            const auto& a = al.link->attribute;
            if (al.link->mode == AudioLinkMode::Set)
            {
                if (a == "Dimmer") audioDimmerSet = al.value;
                else               audioSet[a] = al.value;
            }
            else
            {
                if (a == "Dimmer") audioDimmerScale *= al.value;
                else
                {
                    auto it = audioScale.find (a);
                    audioScale[a] = (it != audioScale.end() ? it->second : 1.0f) * al.value;
                }
            }
        }

        // pixel maps: sample the video at the fixture (or each cell) position
        std::map<juce::String, float> pixelVals;
        float pixelDimmer = -1.0f;
        if (f.hasLayoutPosition && ! pixelMaps.empty())
        {
            const int cells = type->hasCells() ? type->cellCount() : 0;
            for (auto* m : pixelMaps)
                for (int c = cells > 0 ? 1 : 0; c <= cells; ++c)
                {
                    const auto [px, py] = cellLayoutPosition (f, *type, c);
                    if (! m->contains (px, py)) continue;
                    RGBf col;
                    if (! pixelmap::sampleMap (*m, ctx.video, (px - m->x) / m->w, (py - m->y) / m->h, ctx.now, col,
                                               phaseOf (m->effect.id), ctx.sizeMaster)) continue;
                    if (! m->driveDimmer) { col.r *= m->intensity; col.g *= m->intensity; col.b *= m->intensity; }
                    writeColour (pixelVals, *type, col, c);
                    if (m->driveDimmer) pixelDimmer = juce::jmax (pixelDimmer, m->intensity);
                }
        }

        const int cellCount = type->hasCells() ? type->cellCount() : 0;
        float cellDimmerLevel = -1.0f;            // a layer set pixel dimmers (see below)

        for (auto& layer : layers)
        {
            if (layer.isProgrammer)
            {
                // above executors, below the programmer: pixel maps, then audio "set" links
                for (auto& [a, v] : pixelVals) vals[a] = v;
                if (pixelDimmer >= 0.0f) dimmerHtp = pixelDimmer;
                for (auto& [a, v] : audioSet) vals[a] = v;
                if (audioDimmerSet >= 0.0f) dimmerHtp = audioDimmerSet;
            }

            float layerDimmer = -1.0f;
            const float cellLevel = layer.isProgrammer ? 1.0f : layer.level * layer.dimmerLevel;
            auto setCellDimmer = [&] (const juce::String& key, float v)
            {
                vals[key] = v * cellLevel;
                cellDimmerLevel = juce::jmax (cellDimmerLevel, cellLevel);
            };

            auto fv = layer.values.find (f.fid);
            if (fv != layer.values.end())
                for (auto& [attr, v] : fv->second)
                {
                    if (attr == "Dimmer")                                   layerDimmer = v;
                    else if (cellOf (attr) > 0 && baseAttribute (attr) == "Dimmer") setCellDimmer (attr, v);
                    else                                                    vals[attr] = v;
                }

            for (auto& ie : layer.effects)
            {
                const auto& e = *ie.e;
                const double ph = phaseOf (e.id);

                // whole fixture
                if (auto w = ie.index.find (f.fid); w != ie.index.end())
                {
                    const int idx = w->second;
                    switch (e.kind)
                    {
                        case EffectKind::Movement:
                            posOffset = effects::movementOffset (e, ph, idx, ctx.sizeMaster) * layer.level;
                            hasPosOffset = true;
                            break;
                        case EffectKind::Dimmer:
                            layerDimmer = effects::dimmerValue (e, ph, idx, ctx.sizeMaster);
                            break;
                        case EffectKind::Attribute:
                            if (e.attribute == "Dimmer") layerDimmer = effects::dimmerValue (e, ph, idx, ctx.sizeMaster);
                            else if (e.attribute.isNotEmpty())
                            {
                                const float v = effects::dimmerValue (e, ph, idx, ctx.sizeMaster);
                                for (auto& sl : slots)                      // every cell of pixel fixtures
                                    if (baseAttribute (sl.attribute) == e.attribute) vals[sl.attribute] = v;
                            }
                            break;
                        case EffectKind::Color:
                            writeColour (vals, *type, effects::colourValue (e, ph, idx, ctx.sizeMaster));
                            break;
                        case EffectKind::Fly:
                        {
                            const auto fly = effects::flyValue (e, ph, idx, ctx.sizeMaster);
                            posOffset = fly.offset * layer.level;
                            hasPosOffset = true;
                            layerDimmer = fly.dimmer;
                            break;
                        }
                    }
                }

                // single pixels
                if (! ie.hasCells || cellCount == 0) continue;
                for (int c = 1; c <= cellCount; ++c)
                {
                    auto ci = ie.index.find (cellTarget (f.fid, c));
                    if (ci == ie.index.end()) continue;
                    const int idx = ci->second;
                    switch (e.kind)
                    {
                        case EffectKind::Dimmer:
                            setCellDimmer (cellKey ("Dimmer", c), effects::dimmerValue (e, ph, idx, ctx.sizeMaster));
                            break;
                        case EffectKind::Attribute:
                        {
                            const float v = effects::dimmerValue (e, ph, idx, ctx.sizeMaster);
                            if (e.attribute == "Dimmer") setCellDimmer (cellKey ("Dimmer", c), v);
                            else vals[cellKey (e.attribute, c)] = v;
                            break;
                        }
                        case EffectKind::Color:
                            writeColour (vals, *type, effects::colourValue (e, ph, idx, ctx.sizeMaster), c);
                            break;
                        case EffectKind::Fly:
                            setCellDimmer (cellKey ("Dimmer", c), effects::flyValue (e, ph, idx, ctx.sizeMaster).dimmer);
                            break;
                        case EffectKind::Movement:
                            break;
                    }
                }
            }

            if (layerDimmer >= 0.0f)
            {
                if (layer.isProgrammer) programmerDimmer = layerDimmer;
                else dimmerHtp = juce::jmax (dimmerHtp, layerDimmer * layer.level * layer.dimmerLevel);
            }
        }

        // Lowlight: the step set (or selection) in the lowlight colour at the lowlight level
        if (ctx.programmer.isLowlight())
        {
            const auto& ll = ctx.programmer.lowlightTargets();
            const bool whole = std::find (ll.begin(), ll.end(), f.fid) != ll.end();
            const auto argb = ctx.programmer.lowlightColour;
            const RGBf rgb { (float) ((argb >> 16) & 0xff) / 255.0f, (float) ((argb >> 8) & 0xff) / 255.0f, (float) (argb & 0xff) / 255.0f };
            bool any = false;
            std::set<int> cells;
            for (auto& s : slots)
            {
                const int cell = cellOf (s.attribute);
                const bool hit = whole || (cell > 0 && std::find (ll.begin(), ll.end(), cellTarget (f.fid, cell)) != ll.end());
                if (! hit) continue;
                any = true;
                cells.insert (cell);
                // open the beam like highlight does (shutter open, wheels open...); the colour goes on top
                if (s.highlightNormalised >= 0.0f && s.attribute != "Dimmer") vals[s.attribute] = s.highlightNormalised;
            }
            if (any)
            {
                for (int c : cells) writeColour (vals, *type, rgb, c);
                if (whole) writeColour (vals, *type, rgb);
                // fixtures without colour mixing: the lowlight colour wheel position
                if (whole && ctx.programmer.lowlightWheel >= 0.0f)
                {
                    bool mixes = false, wheel = false;
                    for (auto& a : type->attributes)
                    {
                        mixes |= a.attribute == "ColorR" || a.attribute == "ColorC";
                        wheel |= a.attribute == "ColorWheel" && a.cell == 0;
                    }
                    if (wheel && ! mixes) vals["ColorWheel"] = ctx.programmer.lowlightWheel;
                }
                programmerDimmer = ctx.programmer.lowlightLevel;
            }
        }

        // Highlight: the selected fixtures (or pixels) at full, open white, above everything
        if (ctx.programmer.isHighlight())
        {
            const auto& hl = ctx.programmer.highlightTargets();
            const bool whole = std::find (hl.begin(), hl.end(), f.fid) != hl.end();
            bool any = whole;
            for (auto& s : slots)
            {
                if (s.highlightNormalised < 0.0f) continue;
                const int cell = cellOf (s.attribute);
                const bool hit = whole || (cell > 0 && std::find (hl.begin(), hl.end(), cellTarget (f.fid, cell)) != hl.end());
                if (! hit) continue;
                any = true;
                if (s.attribute != "Dimmer") vals[s.attribute] = s.highlightNormalised;
            }
            if (any) programmerDimmer = 1.0f;
        }

        float dimmer = 0.0f;
        if (programmerDimmer >= 0.0f)      dimmer = programmerDimmer;
        else if (dimmerHtp >= 0.0f)        dimmer = dimmerHtp;
        else if (cellDimmerLevel >= 0.0f)  dimmer = cellDimmerLevel;   // pixels set but no fixture dimmer: open it
        else
            for (auto& s : slots)
                if (s.attribute == "Dimmer") dimmer = s.defaultNormalised;
        dimmer *= audioDimmerScale;

        const bool virtualDim = type->usesVirtualDimmer();
        std::map<juce::String, float>* res = nullptr;
        if (resolved != nullptr)
        {
            res = &(*resolved)[f.fid];
            (*res)["Dimmer"] = dimmer;
            (*res)["Intensity"] = dimmer * master;
        }

        // 3) write DMX
        auto write = [&] (const AttributeSlots& s, float v)
        {
            v = juce::jlimit (0.0f, 1.0f, v);
            if (s.fine >= 0)
            {
                const int x = juce::roundToInt (v * 65535.0f);
                buf[(size_t) (base + s.coarse)] = (juce::uint8) (x >> 8);
                buf[(size_t) (base + s.fine)]   = (juce::uint8) (x & 0xff);
            }
            else
            {
                buf[(size_t) (base + s.coarse)] = (juce::uint8) juce::roundToInt (v * 255.0f);
            }
        };

        // per-pixel virtual dimmers (1 = full)
        std::map<int, float> cellDim;
        for (auto& s : slots)
            if (s.coarse < 0 && cellOf (s.attribute) > 0)
            {
                auto it = vals.find (s.attribute);
                const float v = juce::jlimit (0.0f, 1.0f, it != vals.end() ? it->second : s.defaultNormalised);
                cellDim[cellOf (s.attribute)] = v;
                if (res != nullptr) (*res)[s.attribute] = v;
            }

        for (auto& s : slots)
        {
            if (s.coarse < 0)
                continue;                                // virtual dimmers have no channel

            if (s.attribute == "Dimmer")
            {
                write (s, dimmer * master);
                continue;
            }

            auto it = vals.find (s.attribute);
            float v = it != vals.end() ? it->second : s.defaultNormalised;
            bool changed = it != vals.end();

            if (hasPosOffset && (s.attribute == "Pan" || s.attribute == "Tilt"))
            {
                v += s.attribute == "Pan" ? posOffset.x : posOffset.y;
                changed = true;
            }
            if (auto sc = audioScale.find (s.attribute); sc != audioScale.end())
            {
                v *= sc->second;
                changed = true;
            }
            // resolved values are reported before the virtual dimmer (colour stays visible)
            if (res != nullptr) (*res)[s.attribute] = juce::jlimit (0.0f, 1.0f, v);

            // fixtures hung the other way round: invert the output, not the programmed value
            if ((f.invertPan && baseAttribute (s.attribute) == "Pan") || (f.invertTilt && baseAttribute (s.attribute) == "Tilt"))
            {
                v = 1.0f - juce::jlimit (0.0f, 1.0f, v);
                changed = true;
            }

            const int cell = cellOf (s.attribute);
            if (cell > 0 && isVirtualDimmerTarget (s.attribute))
                if (auto cd = cellDim.find (cell); cd != cellDim.end())
                {
                    v *= cd->second;
                    changed = true;
                }
            if (virtualDim && (isVirtualDimmerTarget (s.attribute) || (cell > 0 && baseAttribute (s.attribute) == "Dimmer")))
            {
                v *= dimmer * master;
                changed = true;
            }
            if (changed)
                write (s, v);
        }
    }
}

//==============================================================================
void DmxEngine::applyDmxTest (const DmxTest& test, UniverseMap& frame, double t)
{
    auto universe = [&frame] (int u) -> DmxUniverse&
    {
        auto it = frame.find (u);
        if (it == frame.end()) { DmxUniverse z {}; z.fill (0); it = frame.emplace (u, z).first; }
        return it->second;
    };
    for (auto& [u, chans] : test.values)
    {
        if (u < 1) continue;
        auto& d = universe (u);
        for (auto& [c, v] : chans)
            if (c >= 1 && c <= 512) d[(size_t) c - 1] = (juce::uint8) juce::jlimit (0, 255, v);
    }
    if (test.chase && ! test.chaseChannels.empty() && test.chaseUniverse >= 1)
    {
        auto& d = universe (test.chaseUniverse);
        const auto n = (juce::int64) test.chaseChannels.size();
        const auto step = (juce::int64) std::floor (t * (double) juce::jmax (0.05f, test.chaseStepsPerSecond));
        const int on = test.chaseChannels[(size_t) (((step % n) + n) % n)];
        for (int c : test.chaseChannels)
            if (c >= 1 && c <= 512) d[(size_t) c - 1] = (juce::uint8) (c == on ? juce::jlimit (0, 255, test.chaseLevel) : 0);
    }
}

//==============================================================================
void DmxEngine::run()
{
    NetSettings current;
    int packetsThisSecond = 0;
    auto secondStart = juce::Time::getMillisecondCounter();
    double lastFrame = now();

    while (! threadShouldExit())
    {
        const auto frameStart = juce::Time::getMillisecondCounter();

        if (netDirty.exchange (false))
        {
            current = getNetSettings();
            auto status = current.enabled ? sender.configure (current)
                                          : juce::String ("Output disabled");
            const juce::ScopedLock sl (netLock);
            netStatus = status;
        }

        const double t = now();
        const double dt = juce::jlimit (0.0, 0.25, t - lastFrame);
        lastFrame = t;

        UniverseMap frame;
        std::map<int, std::map<juce::String, float>> resolved;
        {
            const juce::ScopedLock sl (dataLock);
            playback.update (t, Masters::speedMultiplier (masters.dimmerSpeed.load()) * Masters::referenceBpm / 60.0f);
            playback.refreshParts();
            advanceEffects (programmer, playback, masters, dt, effectPhases, &pixelMaps);
            updateAudioLinks (audioLinks, audio, dt, audioLinkLevels);
            RenderContext ctx { library, patch, programmer, playback, effectPhases, t,
                                masters.blackout.load() ? 0.0f : masters.grand.load(),
                                masters.size.load(), &audioLinks, &audio, &presets, &pixelMaps, &video, &audioLinkLevels };
            renderFrame (ctx, frame, &resolved);
            if (dmxTest.active()) applyDmxTest (dmxTest, frame, t);
        }

        if (current.enabled)
            packetsThisSecond += sender.send (frame);

        {
            const juce::ScopedLock sl (snapshotLock);
            snapshot = std::move (frame);
            resolvedSnapshot = std::move (resolved);
            audioLinkSnapshot = audioLinkLevels;
        }

        const auto nowMs = juce::Time::getMillisecondCounter();
        if (nowMs - secondStart >= 1000)
        {
            packetsPerSecond = packetsThisSecond;
            packetsThisSecond = 0;
            secondStart = nowMs;
        }

        const int interval = 1000 / juce::jlimit (1, 60, current.rateHz);
        const int elapsed = (int) (juce::Time::getMillisecondCounter() - frameStart);
        wait (juce::jmax (1, interval - elapsed));
    }
}

} // namespace pl
