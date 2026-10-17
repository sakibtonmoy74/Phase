#include "Playback.h"

namespace pl
{

//==============================================================================
const juce::StringArray& buttonFunctionNames()
{
    static const juce::StringArray n { "None", "Go", "Back", "Off", "Flash", "Swop", "Temp", "Toggle", "Pause",
                                       "Select", "Rate Up", "Rate Down" };
    return n;
}

juce::String buttonFunctionName (ButtonFunction f) { return buttonFunctionNames()[(int) f]; }

ButtonFunction buttonFunctionFromName (const juce::String& n)
{
    const int i = buttonFunctionNames().indexOf (n);
    return i < 0 ? ButtonFunction::None : (ButtonFunction) i;
}

const juce::StringArray& chaseModeNames()
{
    static const juce::StringArray n { "Loop", "Bounce", "Random" };
    return n;
}

//==============================================================================
juce::var valueMapToVar (const ValueMap& m)
{
    auto* root = new juce::DynamicObject();
    for (auto& [fid, attrs] : m)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [a, v] : attrs)
            o->setProperty (a, v);
        root->setProperty (juce::String (fid), juce::var (o));
    }
    return juce::var (root);
}

ValueMap valueMapFromVar (const juce::var& v)
{
    ValueMap m;
    if (auto* root = v.getDynamicObject())
        for (auto& p : root->getProperties())
            if (auto* attrs = p.value.getDynamicObject())
                for (auto& a : attrs->getProperties())
                    m[p.name.toString().getIntValue()][a.name.toString()]
                        = juce::jlimit (0.0f, 1.0f, (float) (double) a.value);
    return m;
}

juce::var CuePart::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    if (group > 0) o->setProperty ("group", group);
    juce::Array<juce::var> f;
    for (int t : fixtures) f.add (t);
    o->setProperty ("fixtures", f);
    auto* vals = new juce::DynamicObject();
    for (auto& [a, v] : values) vals->setProperty (a, v);
    o->setProperty ("values", juce::var (vals));
    auto* refs = new juce::DynamicObject();
    for (auto& [k, n] : presets) refs->setProperty (k, n);
    o->setProperty ("presets", juce::var (refs));
    juce::Array<juce::var> fx;
    for (auto& e : effects) fx.add (e.toVar());
    o->setProperty ("effects", fx);
    return juce::var (o);
}

CuePart CuePart::fromVar (const juce::var& v)
{
    CuePart p;
    p.name = v["name"].toString();
    p.group = juce::jmax (0, (int) v["group"]);
    if (auto* f = v["fixtures"].getArray()) for (auto& t : *f) p.fixtures.push_back ((int) t);
    if (auto* vals = v["values"].getDynamicObject())
        for (auto& prop : vals->getProperties()) p.values[prop.name.toString()] = juce::jlimit (0.0f, 1.0f, (float) (double) prop.value);
    if (auto* refs = v["presets"].getDynamicObject())
        for (auto& prop : refs->getProperties()) p.presets[prop.name.toString()] = juce::jmax (1, (int) prop.value);
    if (auto* fx = v["effects"].getArray()) for (auto& e : *fx) p.effects.push_back (EffectParams::fromVar (e));
    return p;
}

juce::var Cue::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("number", number);
    o->setProperty ("name", name);
    o->setProperty ("fade", fadeTime);
    o->setProperty ("fadeOut", fadeOut);
    o->setProperty ("values", valueMapToVar (values));
    juce::Array<juce::var> fx;
    for (auto& e : effects) fx.add (e.toVar());
    o->setProperty ("effects", fx);
    if (! parts.empty())
    {
        juce::Array<juce::var> ps;
        for (auto& p : parts) ps.add (p.toVar());
        o->setProperty ("parts", ps);
    }
    return juce::var (o);
}

Cue Cue::fromVar (const juce::var& v)
{
    Cue c;
    c.number   = (double) v.getProperty ("number", 1.0);
    c.name     = v["name"].toString();
    c.fadeTime = juce::jmax (0.0f, (float) (double) v["fade"]);
    c.fadeOut  = v.hasProperty ("fadeOut") ? juce::jmax (0.0f, (float) (double) v["fadeOut"]) : c.fadeTime;
    c.values   = valueMapFromVar (v["values"]);
    if (auto* fx = v["effects"].getArray())
        for (auto& e : *fx) c.effects.push_back (EffectParams::fromVar (e));
    if (auto* ps = v["parts"].getArray())
        for (auto& p : *ps) c.parts.push_back (CuePart::fromVar (p));
    return c;
}

juce::var Executor::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("tracking", tracking);
    juce::Array<juce::var> b;
    for (auto f : buttons) b.add (buttonFunctionName (f));
    o->setProperty ("buttons", b);
    o->setProperty ("faderFunction", faderFunction == FaderFunction::Speed ? "Speed" : "Master");
    if (chase)
    {
        auto* ch = new juce::DynamicObject();
        ch->setProperty ("rate", chaseRate);
        ch->setProperty ("fade", chaseFade);
        ch->setProperty ("mode", chaseModeNames()[(int) chaseMode]);
        o->setProperty ("chase", juce::var (ch));
    }
    juce::Array<juce::var> c;
    for (auto& cue : cues) c.add (cue.toVar());
    o->setProperty ("cues", c);
    return juce::var (o);
}

Executor Executor::fromVar (const juce::var& v)
{
    Executor e;
    e.name = v["name"].toString();
    e.tracking = v.getProperty ("tracking", true);
    if (auto* b = v["buttons"].getArray())
        for (int i = 0; i < juce::jmin (numButtons, b->size()); ++i)
            e.buttons[i] = buttonFunctionFromName ((*b)[i].toString());
    e.faderFunction = v["faderFunction"].toString() == "Speed" ? FaderFunction::Speed : FaderFunction::Master;
    if (v["chase"].isObject())
    {
        e.chase = true;
        e.chaseRate = juce::jlimit (0.0625f, 16.0f, (float) (double) v["chase"].getProperty ("rate", 1.0));
        e.chaseFade = juce::jlimit (0.0f, 1.0f, (float) (double) v["chase"].getProperty ("fade", 0.0));
        e.chaseMode = (ChaseMode) juce::jmax (0, chaseModeNames().indexOf (v["chase"]["mode"].toString()));
    }
    if (auto* c = v["cues"].getArray())
        for (auto& cue : *c) e.cues.push_back (Cue::fromVar (cue));
    return e;
}

//==============================================================================
const Executor* Playback::find (int page, int slot) const
{
    auto it = executors.find ({ page, slot });
    return it != executors.end() ? &it->second : nullptr;
}

Executor* Playback::findMutable (int page, int slot)
{
    auto it = executors.find ({ page, slot });
    return it != executors.end() ? &it->second : nullptr;
}

const ExecutorState& Playback::state (int page, int slot) const
{
    static const ExecutorState empty;
    auto it = states.find ({ page, slot });
    return it != states.end() ? it->second : empty;
}

int Playback::highestUsedPage() const
{
    int hi = 1;
    for (auto& [k, e] : executors) hi = juce::jmax (hi, k.first);
    return hi;
}

//==============================================================================
void Playback::startCue (const Key& k, int index, double now)
{
    auto* exec = findMutable (k.first, k.second);
    if (exec == nullptr || exec->cues.empty())
        return;
    index = juce::jlimit (0, (int) exec->cues.size() - 1, index);

    auto& st = stateFor (k);
    st.from = st.isOn() ? currentValues (st, now) : ValueMap();
    st.target = targetFor (*exec, index, &st.liveEffects);
    st.fadeStart = now;
    st.fadeIn = exec->cues[(size_t) index].fadeTime;
    st.fadeOut = exec->cues[(size_t) index].fadeOut;
    st.releasing = false;
    st.cue = index;
    st.order = ++orderCounter;
}

void Playback::go (int page, int slot, double now)
{
    auto* exec = find (page, slot);
    if (exec == nullptr || exec->cues.empty()) return;
    auto& st = stateFor ({ page, slot });
    int next = st.cue + 1;
    if (next >= (int) exec->cues.size()) next = 0;     // wrap around
    startCue ({ page, slot }, next, now);
}

void Playback::back (int page, int slot, double now)
{
    auto* exec = find (page, slot);
    if (exec == nullptr || exec->cues.empty()) return;
    auto& st = stateFor ({ page, slot });
    int prev = st.cue - 1;
    if (prev < 0) prev = (int) exec->cues.size() - 1;
    startCue ({ page, slot }, prev, now);
}

void Playback::gotoCue (int page, int slot, int cueIndex, double now)
{
    startCue ({ page, slot }, cueIndex, now);
}

void Playback::off (int page, int slot)
{
    auto& st = stateFor ({ page, slot });
    st.cue = -1;
    st.target.clear();
    st.from.clear();
    st.flashStarted = false;
    st.releasing = false;
    st.paused = false;
    st.chasePhase = 0.0;
}

void Playback::release (int page, int slot, double now)
{
    auto& st = stateFor ({ page, slot });
    if (! st.isOn()) return;
    auto* exec = find (page, slot);
    const float t = exec != nullptr && st.cue < (int) exec->cues.size() ? exec->cues[(size_t) st.cue].fadeOut : 0.0f;
    if (t <= 0.0f) { off (page, slot); return; }
    if (st.releasing) return;
    // freeze the current (possibly mid-fade) look, then fade its intensity out
    st.target = currentValues (st, now);
    st.from.clear();
    st.fadeIn = st.fadeOut = 0.0f;
    st.releasing = true;
    st.releaseStart = now;
    st.releaseTime = t;
}

void Playback::update (double now, float beatsPerSecond)
{
    for (auto& [k, st] : states)
    {
        if (st.releasing && now >= st.releaseStart + st.releaseTime)
            off (k.first, k.second);

        const double dt = st.lastUpdate < 0.0 ? 0.0 : juce::jlimit (0.0, 0.5, now - st.lastUpdate);
        st.lastUpdate = now;

        auto* exec = find (k.first, k.second);
        if (exec == nullptr || ! exec->chase || ! st.isOn() || st.releasing || st.paused || exec->cues.size() < 2)
            continue;

        float rate = exec->chaseRate * st.rateScale;
        if (exec->faderFunction == FaderFunction::Speed)
        {
            const float f = juce::jlimit (0.0f, 1.0f, st.speedFader);   // same curve as the speed masters
            rate *= f <= 0.5f ? f * 2.0f : 1.0f + (f - 0.5f) * 6.0f;
        }
        const double stepsPerSecond = (double) juce::jmax (0.0f, beatsPerSecond * rate);
        st.chasePhase += dt * stepsPerSecond;
        if (st.chasePhase >= 1.0 && stepsPerSecond > 0.0)
        {
            st.chasePhase = std::fmod (st.chasePhase, 1.0);
            const auto keep = st.order;
            startCue (k, nextChaseStep (*exec, st), now);
            st.order = keep;                               // stepping doesn't change LTP priority
            st.fadeIn = st.fadeOut = (float) (exec->chaseFade / stepsPerSecond);
        }
    }
}

int Playback::nextChaseStep (const Executor& exec, ExecutorState& st)
{
    const int n = (int) exec.cues.size();
    switch (exec.chaseMode)
    {
        case ChaseMode::Bounce:
        {
            int next = st.cue + st.bounceDirection;
            if (next >= n || next < 0) { st.bounceDirection = -st.bounceDirection; next = st.cue + st.bounceDirection; }
            return juce::jlimit (0, n - 1, next);
        }
        case ChaseMode::Random:
        {
            int next = juce::Random::getSystemRandom().nextInt (n - 1);
            return next >= st.cue ? next + 1 : next;      // never the same step twice
        }
        case ChaseMode::Loop: break;
    }
    return (st.cue + 1) % n;
}

void Playback::button (int page, int slot, int index, bool down, double now)
{
    auto* exec = find (page, slot);
    const auto fn = exec != nullptr && index >= 0 && index < Executor::numButtons ? exec->buttons[index]
                    : (index == 0 ? ButtonFunction::Go : index == 1 ? ButtonFunction::Back
                       : index == 2 ? ButtonFunction::Off : ButtonFunction::Flash);
    switch (fn)
    {
        case ButtonFunction::Go:       if (down) go (page, slot, now); break;
        case ButtonFunction::Back:     if (down) back (page, slot, now); break;
        case ButtonFunction::Off:      if (down) release (page, slot, now); break;
        case ButtonFunction::Flash:    setFlash (page, slot, down, now); break;
        case ButtonFunction::Swop:     setSwop (page, slot, down, now); break;
        case ButtonFunction::Temp:     setTemp (page, slot, down, now); break;
        case ButtonFunction::Toggle:   if (down) toggle (page, slot, now); break;
        case ButtonFunction::Pause:    if (down) togglePause (page, slot); break;
        case ButtonFunction::RateUp:   if (down) { auto& st = stateFor ({ page, slot }); st.rateScale = juce::jmin (16.0f, st.rateScale * 2.0f); } break;
        case ButtonFunction::RateDown: if (down) { auto& st = stateFor ({ page, slot }); st.rateScale = juce::jmax (1.0f / 16.0f, st.rateScale * 0.5f); } break;
        case ButtonFunction::Select:
        case ButtonFunction::None:     break;
    }
}

void Playback::setSwop (int page, int slot, bool down, double now)
{
    auto& st = stateFor ({ page, slot });
    if (down)
    {
        st.swop = true;
        if (! st.isOn() && find (page, slot) != nullptr) { startCue ({ page, slot }, 0, now); st.swopStarted = true; }
        st.order = ++orderCounter;
    }
    else
    {
        st.swop = false;
        if (st.swopStarted || (st.fader <= 0.0f && ! st.flash)) off (page, slot);
        st.swopStarted = false;
    }
}

void Playback::setTemp (int page, int slot, bool down, double now)
{
    auto& st = stateFor ({ page, slot });
    if (down)
    {
        st.temp = true;
        if (! st.isOn() && find (page, slot) != nullptr) { startCue ({ page, slot }, 0, now); st.tempStarted = true; }
    }
    else
    {
        st.temp = false;
        if (st.tempStarted) release (page, slot, now);
        st.tempStarted = false;
    }
}

void Playback::toggle (int page, int slot, double now)
{
    auto& st = stateFor ({ page, slot });
    if (st.isOn() && ! st.releasing) release (page, slot, now);
    else if (find (page, slot) != nullptr) startCue ({ page, slot }, st.isOn() ? st.cue : 0, now);
}

void Playback::togglePause (int page, int slot)
{
    auto& st = stateFor ({ page, slot });
    st.paused = ! st.paused;
}

bool Playback::anySwop() const
{
    for (auto& [k, st] : states)
        if (st.swop && st.isOn()) return true;
    return false;
}

float Playback::faderPosition (int page, int slot) const
{
    auto& st = state (page, slot);
    auto* e = find (page, slot);
    return e != nullptr && e->faderFunction == FaderFunction::Speed ? st.speedFader : st.fader;
}

void Playback::setButtonFunction (int page, int slot, int index, ButtonFunction f)
{
    if (auto* e = findMutable (page, slot))
        if (index >= 0 && index < Executor::numButtons) e->buttons[index] = f;
}

void Playback::setFaderFunction (int page, int slot, FaderFunction f)
{
    if (auto* e = findMutable (page, slot)) e->faderFunction = f;
    auto& st = stateFor ({ page, slot });
    if (f == FaderFunction::Speed) st.fader = 1.0f;       // level stays full; the fader sets speed
}

void Playback::setChase (int page, int slot, bool on, float rate, float fade, ChaseMode mode)
{
    if (auto* e = findMutable (page, slot))
    {
        e->chase = on;
        e->chaseRate = juce::jlimit (0.0625f, 16.0f, rate);
        e->chaseFade = juce::jlimit (0.0f, 1.0f, fade);
        e->chaseMode = mode;
    }
    auto& st = stateFor ({ page, slot });
    st.paused = false;
    st.rateScale = 1.0f;
}

void Playback::setTracking (int page, int slot, bool on)
{
    if (auto* e = findMutable (page, slot)) e->tracking = on;
    auto& st = stateFor ({ page, slot });
    if (st.isOn())
        if (auto* e = find (page, slot)) st.target = targetFor (*e, st.cue, &st.liveEffects);
}

void Playback::offAll()
{
    for (auto& [k, st] : states)
    {
        st.cue = -1;
        st.target.clear();
        st.from.clear();
        st.flashStarted = false;
    }
}

void Playback::setFader (int page, int slot, float value, double now)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    auto& st = stateFor ({ page, slot });
    if (auto* e = find (page, slot); e != nullptr && e->faderFunction == FaderFunction::Speed)
    {
        st.speedFader = value;                          // speed only: no start / stop
        return;
    }
    const float previous = st.fader;
    st.fader = value;

    if (find (page, slot) == nullptr) return;
    if (value > 0.0f && previous <= 0.0f && ! st.isOn())
        startCue ({ page, slot }, 0, now);              // auto start
    else if (value <= 0.0f && st.isOn() && ! st.flash)
        release (page, slot, now);                      // auto stop, with fade out
    else if (value > 0.0f && st.releasing)
        startCue ({ page, slot }, st.cue, now);         // fader back up during a release
}

void Playback::setFlash (int page, int slot, bool down, double now)
{
    auto& st = stateFor ({ page, slot });
    if (down)
    {
        st.flash = true;
        if (! st.isOn() && find (page, slot) != nullptr)
        {
            startCue ({ page, slot }, 0, now);
            st.flashStarted = true;
        }
        st.order = ++orderCounter;
    }
    else
    {
        st.flash = false;
        if (st.flashStarted || (st.fader <= 0.0f && ! st.swop && ! st.temp))
            off (page, slot);
        st.flashStarted = false;
    }
}

//==============================================================================
juce::String Playback::store (int page, int slot, const ValueMap& values,
                              const std::vector<EffectParams>& effects, StoreMode mode, double now,
                              const std::vector<CuePart>& parts, int cueIndex)
{
    if (values.empty() && effects.empty() && parts.empty())
        return "Programmer is empty, nothing to store";

    const Key k { page, slot };
    auto& exec = executors[k];
    const bool fresh = exec.cues.empty();
    if (exec.name.isEmpty())
        exec.name = "Exec " + juce::String (page) + "." + juce::String (slot);

    auto& st = stateFor (k);
    if (fresh && ! st.isOn() && st.fader <= 0.0f) st.fader = 1.0f;   // a new executor starts at full
    juce::String result;

    if (cueIndex >= (int) exec.cues.size()) cueIndex = -1;

    if (mode == StoreMode::Overwrite && cueIndex >= 0)
    {
        auto& c = exec.cues[(size_t) cueIndex];          // keep number, name and times
        c.values = values;
        c.effects = effects;
        c.parts = parts;
        result = "Overwrote " + c.name + " on " + exec.name;
    }
    else if (mode == StoreMode::Add && cueIndex >= 0 && cueIndex + 1 < (int) exec.cues.size())
    {
        // insert after the cue: number halfway to the next one (MA: 1 → 1.5)
        const double a = exec.cues[(size_t) cueIndex].number, b = exec.cues[(size_t) cueIndex + 1].number;
        double num = std::floor (a) + 1.0 < b ? std::floor (a) + 1.0 : a + (b - a) / 2.0;
        num = std::round (num * 1000.0) / 1000.0;
        Cue c;
        c.number = num;
        c.name = "Cue " + (juce::exactlyEqual (num, std::floor (num)) ? juce::String ((int) num) : juce::String (num, 3).trimCharactersAtEnd ("0"));
        c.values = values;
        c.effects = effects;
        c.parts = parts;
        exec.cues.insert (exec.cues.begin() + cueIndex + 1, c);
        if (st.isOn() && st.cue > cueIndex) ++st.cue;
        result = "Stored " + c.name + " on " + exec.name;
    }
    else if ((mode == StoreMode::Overwrite && cueIndex < 0) || exec.cues.empty())
    {
        exec.cues.clear();
        Cue c;
        c.number = 1.0;
        c.name = "Cue 1";
        c.values = values;
        c.effects = effects;
        c.parts = parts;
        exec.cues.push_back (c);
        if (st.isOn()) { st.cue = 0; }
        result = "Stored Cue 1 on " + exec.name;
    }
    else if (mode == StoreMode::Add)
    {
        Cue c;
        c.number = std::floor (exec.cues.back().number) + 1.0;
        c.name = "Cue " + juce::String ((int) c.number);
        c.values = values;
        c.effects = effects;
        c.parts = parts;
        exec.cues.push_back (c);
        result = "Stored " + c.name + " on " + exec.name;
    }
    else // Merge into the current cue (or the last one when off)
    {
        const int idx = cueIndex >= 0 ? cueIndex : st.isOn() ? st.cue : (int) exec.cues.size() - 1;
        auto& c = exec.cues[(size_t) idx];
        for (auto& [fid, attrs] : values)
            for (auto& [a, v] : attrs)
                c.values[fid][a] = v;
        for (auto& e : effects)
        {
            // the same effect is replaced; the same kind on other fixtures keeps the rest
            bool replaced = false;
            for (auto& existing : c.effects)
                if (existing.id == e.id) { existing = e; replaced = true; }
            if (replaced) continue;
            for (auto& existing : c.effects)
                if (existing.sameSlot (e))
                    existing.fixtures.erase (std::remove_if (existing.fixtures.begin(), existing.fixtures.end(),
                                                             [&e] (int t) { return e.contains (t); }), existing.fixtures.end());
            c.effects.erase (std::remove_if (c.effects.begin(), c.effects.end(),
                                             [] (const EffectParams& x) { return x.fixtures.empty(); }), c.effects.end());
            c.effects.push_back (e);
        }
        for (auto& p : parts)
        {
            // same group / same fixtures: merge into that folder
            bool merged = false;
            for (auto& existing : c.parts)
                if ((p.group > 0 && existing.group == p.group) || (p.group == 0 && existing.group == 0 && existing.fixtures == p.fixtures))
                {
                    for (auto& [a, v] : p.values) existing.values[a] = v;
                    for (auto& [pool, n] : p.presets) existing.presets[pool] = n;
                    for (auto& e : p.effects) existing.effects.push_back (e);
                    merged = true;
                }
            if (! merged) c.parts.push_back (p);
        }
        result = "Merged into " + c.name + " on " + exec.name;
    }

    // keep a running executor showing the updated cue, without a fade
    if (st.isOn())
    {
        st.target = targetFor (exec, st.cue, &st.liveEffects);
        st.from.clear();
        st.fadeIn = st.fadeOut = 0.0f;
        st.fadeStart = now;
    }
    return result;
}

void Playback::deleteCue (int page, int slot, int cueIndex)
{
    auto* exec = findMutable (page, slot);
    if (exec == nullptr || cueIndex < 0 || cueIndex >= (int) exec->cues.size()) return;
    exec->cues.erase (exec->cues.begin() + cueIndex);
    if (exec->cues.empty()) { deleteExecutor (page, slot); return; }

    auto& st = stateFor ({ page, slot });
    if (st.isOn())
    {
        st.cue = juce::jmin (st.cue, (int) exec->cues.size() - 1);
        st.target = targetFor (*exec, st.cue, &st.liveEffects);
        st.from.clear();
        st.fadeIn = st.fadeOut = 0.0f;
    }
}

void Playback::deleteExecutor (int page, int slot)
{
    off (page, slot);
    executors.erase ({ page, slot });
    states.erase ({ page, slot });                       // its fader goes back to 0
}

bool Playback::copyExecutor (int fromPage, int fromSlot, int toPage, int toSlot, bool move)
{
    const Key from { fromPage, fromSlot }, to { toPage, toSlot };
    if (from == to || executors.count (from) == 0) return false;
    if (move)
    {
        // swap the two slots, running state included
        std::swap (executors[from], executors[to]);
        std::swap (states[from], states[to]);
        if (executors[from].cues.empty()) { executors.erase (from); states.erase (from); }
        return true;
    }
    off (toPage, toSlot);
    auto copy = executors[from];
    if (copy.name == "Exec " + juce::String (fromPage) + "." + juce::String (fromSlot))
        copy.name = "Exec " + juce::String (toPage) + "." + juce::String (toSlot);
    for (auto& c : copy.cues)
        for (auto& e : c.effects) e.id = EffectParams::nextId();   // own effects, own phases
    executors[to] = std::move (copy);
    auto& ts = stateFor (to);
    if (! ts.isOn() && ts.fader <= 0.0f) ts.fader = 1.0f;
    return true;
}

bool Playback::copyCue (int page, int slot, int from, int after, bool move)
{
    auto* exec = findMutable (page, slot);
    if (exec == nullptr || from < 0 || from >= (int) exec->cues.size()) return false;
    after = juce::jlimit (-1, (int) exec->cues.size() - 1, after);
    if (move && (after == from || after == from - 1)) return true;  // already there

    auto cue = exec->cues[(size_t) from];
    if (move) exec->cues.erase (exec->cues.begin() + from);
    else
    {
        for (auto& e : cue.effects) e.id = EffectParams::nextId();
        cue.name = cue.name + " copy";
    }
    if (move && after > from) --after;
    exec->cues.insert (exec->cues.begin() + after + 1, cue);

    // renumber in order (1, 2, 3...) when the order no longer increases
    bool ordered = true;
    for (size_t i = 1; i < exec->cues.size(); ++i) ordered &= exec->cues[i].number > exec->cues[i - 1].number;
    if (! ordered)
        for (size_t i = 0; i < exec->cues.size(); ++i)
        {
            const bool defaultName = exec->cues[i].name == "Cue " + juce::String ((int) exec->cues[i].number)
                                  || exec->cues[i].name == "Cue " + juce::String (exec->cues[i].number, 3).trimCharactersAtEnd ("0");
            exec->cues[i].number = (double) (i + 1);
            if (defaultName) exec->cues[i].name = "Cue " + juce::String ((int) i + 1);
        }
    refreshExecutor (page, slot);
    return true;
}

void Playback::remapPresetReferences (const juce::String& poolKey, int from, int to, bool swap)
{
    auto remap = [&] (int n) { return n == from ? to : (swap && n == to ? from : n); };
    for (auto& [k, e] : executors)
        for (auto& c : e.cues)
            for (auto& part : c.parts)
            {
                if (poolKey == "group") part.group = remap (part.group);
                else if (auto it = part.presets.find (poolKey); it != part.presets.end()) it->second = remap (it->second);
            }
}

void Playback::renameExecutor (int page, int slot, const juce::String& name)
{
    if (auto* e = findMutable (page, slot)) e->name = name;
}

void Playback::setCueProperties (int page, int slot, int cueIndex, const juce::String& name, float fadeIn, float fadeOut)
{
    if (auto* e = findMutable (page, slot))
        if (cueIndex >= 0 && cueIndex < (int) e->cues.size())
        {
            e->cues[(size_t) cueIndex].name = name;
            e->cues[(size_t) cueIndex].fadeTime = juce::jmax (0.0f, fadeIn);
            e->cues[(size_t) cueIndex].fadeOut = juce::jmax (0.0f, fadeOut);
        }
}

//==============================================================================
std::vector<Playback::Active> Playback::activeInOrder() const
{
    std::vector<Active> out;
    for (auto& [k, st] : states)
        if (st.isOn())
            if (auto* e = find (k.first, k.second))
                if (st.cue < (int) e->cues.size())
                    out.push_back ({ k, e, &st });
    std::sort (out.begin(), out.end(),
               [] (const Active& a, const Active& b) { return a.state->order < b.state->order; });
    return out;
}

ValueMap Playback::currentValues (const ExecutorState& st, double now)
{
    ValueMap out = st.target;
    const double t = now - st.fadeStart;
    const float pIn  = st.fadeIn  <= 0.0f ? 1.0f : juce::jlimit (0.0f, 1.0f, (float) (t / st.fadeIn));
    const float pOut = st.fadeOut <= 0.0f ? 1.0f : juce::jlimit (0.0f, 1.0f, (float) (t / st.fadeOut));

    if (pIn < 1.0f || pOut < 1.0f)
    {
        for (auto& [fid, attrs] : out)
        {
            auto fromFix = st.from.find (fid);
            for (auto& [a, v] : attrs)
            {
                float from;
                if (fromFix != st.from.end() && fromFix->second.count (a))
                    from = fromFix->second.at (a);
                else if (a == "Dimmer")
                    from = 0.0f;                    // intensity fades in from 0
                else
                    continue;                       // nothing to fade from: snap
                const float p = (a == "Dimmer" && v < from) ? pOut : pIn;
                v = from + (v - from) * p;
            }
        }
        // intensity that is no longer in the cue (tracking off, Back) fades out
        if (pOut < 1.0f)
            for (auto& [fid, attrs] : st.from)
                if (auto d = attrs.find ("Dimmer"); d != attrs.end())
                    if (! out[fid].count ("Dimmer"))
                        out[fid]["Dimmer"] = d->second * (1.0f - pOut);
    }

    if (st.releasing)
    {
        const float r = st.releaseTime <= 0.0f ? 1.0f
                                               : juce::jlimit (0.0f, 1.0f, (float) ((now - st.releaseStart) / st.releaseTime));
        for (auto& [fid, attrs] : out)
            if (auto d = attrs.find ("Dimmer"); d != attrs.end())
                d->second *= 1.0f - r;
    }
    return out;
}

ValueMap Playback::targetFor (const Executor& exec, int index, std::vector<EffectParams>* liveEffects) const
{
    auto m = trackedState (exec, index);
    if (liveEffects != nullptr)
    {
        liveEffects->clear();
        for (auto& ce : effectsAt (exec, index)) liveEffects->push_back (ce.effect);
    }
    if (! partResolver || index < 0 || index >= (int) exec.cues.size()) return m;
    for (int i = exec.tracking ? 0 : index; i <= index; ++i)
        for (auto& part : exec.cues[(size_t) i].parts)
        {
            std::vector<EffectParams> fx;
            for (auto& [fid, attrs] : partResolver (part, fx))
                for (auto& [a, v] : attrs) m[fid][a] = v;
        }
    return m;
}

std::vector<Playback::CueEffect> Playback::effectsAt (const Executor& exec, int index) const
{
    std::vector<CueEffect> out;
    if (index < 0 || index >= (int) exec.cues.size()) return out;

    // a later effect of the same kind takes its fixtures over; a stop marker just frees them
    auto apply = [&out] (const EffectParams& e, int cue, bool folder)
    {
        for (auto& ce : out)
            if (ce.effect.sameSlot (e))
                ce.effect.fixtures.erase (std::remove_if (ce.effect.fixtures.begin(), ce.effect.fixtures.end(),
                                                          [&e] (int t) { return e.contains (t); }), ce.effect.fixtures.end());
        out.erase (std::remove_if (out.begin(), out.end(), [] (const CueEffect& ce) { return ce.effect.fixtures.empty(); }), out.end());
        if (! e.stop && ! e.fixtures.empty()) out.push_back ({ e, cue, folder });
    };

    for (int i = exec.tracking ? 0 : index; i <= index; ++i)
    {
        const auto& cue = exec.cues[(size_t) i];
        for (auto& e : cue.effects)
            apply (e, i, false);
        if (partResolver)
            for (auto& part : cue.parts)
            {
                std::vector<EffectParams> fx;
                partResolver (part, fx);
                for (auto& e : fx) apply (e, i, true);
            }
    }
    return out;
}

void Playback::stopEffect (int page, int slot, int cueIndex, const EffectParams& e)
{
    auto* exec = findMutable (page, slot);
    if (exec == nullptr || cueIndex < 0 || cueIndex >= (int) exec->cues.size()) return;
    auto& c = exec->cues[(size_t) cueIndex];
    const auto before = c.effects.size();
    c.effects.erase (std::remove_if (c.effects.begin(), c.effects.end(),
                                     [&e] (const EffectParams& x) { return x.id == e.id && ! x.stop; }), c.effects.end());
    if (c.effects.size() == before)                       // tracked in from an earlier cue: stop it here
    {
        auto marker = e;
        marker.stop = true;
        marker.id = EffectParams::nextId();
        c.effects.push_back (marker);
    }
    refreshExecutor (page, slot);
}

void Playback::removeCueEffect (int page, int slot, int cueIndex, int effectId)
{
    auto* exec = findMutable (page, slot);
    if (exec == nullptr || cueIndex < 0 || cueIndex >= (int) exec->cues.size()) return;
    auto& fx = exec->cues[(size_t) cueIndex].effects;
    fx.erase (std::remove_if (fx.begin(), fx.end(), [effectId] (const EffectParams& x) { return x.id == effectId; }), fx.end());
    refreshExecutor (page, slot);
}

void Playback::refreshExecutor (int page, int slot)
{
    auto* exec = find (page, slot);
    if (exec == nullptr) return;
    auto& st = stateFor ({ page, slot });
    if (! st.isOn()) return;
    st.cue = juce::jmin (st.cue, (int) exec->cues.size() - 1);
    st.target = targetFor (*exec, st.cue, &st.liveEffects);
}

void Playback::refreshParts()
{
    if (! partResolver) return;
    for (auto& [k, st] : states)
    {
        if (! st.isOn() || st.releasing) continue;
        auto* e = find (k.first, k.second);
        if (e == nullptr || st.cue >= (int) e->cues.size()) continue;
        bool hasParts = false;
        for (int i = e->tracking ? 0 : st.cue; i <= st.cue && ! hasParts; ++i) hasParts = ! e->cues[(size_t) i].parts.empty();
        if (! hasParts) continue;
        std::vector<EffectParams> fx;                       // ids are stable: phases carry on
        auto target = targetFor (*e, st.cue, &fx);
        if (target != st.target) st.target = std::move (target);
        st.liveEffects = std::move (fx);
    }
}

void Playback::setCueParts (int page, int slot, int cueIndex, const std::vector<CuePart>& parts)
{
    if (auto* e = findMutable (page, slot))
        if (cueIndex >= 0 && cueIndex < (int) e->cues.size())
            e->cues[(size_t) cueIndex].parts = parts;
}

ValueMap Playback::trackedState (const Executor& exec, int index)
{
    ValueMap m;
    for (int i = exec.tracking ? 0 : index; i <= index && i < (int) exec.cues.size(); ++i)
        for (auto& [fid, attrs] : exec.cues[(size_t) i].values)
            for (auto& [a, v] : attrs)
                m[fid][a] = v;
    return m;
}

void Playback::clear()
{
    executors.clear();
    states.clear();
}

juce::var Playback::toVar (bool withFaders) const
{
    juce::Array<juce::var> arr;
    for (auto& [k, e] : executors)
    {
        auto v = e.toVar();
        v.getDynamicObject()->setProperty ("page", k.first);
        v.getDynamicObject()->setProperty ("slot", k.second);
        if (withFaders) v.getDynamicObject()->setProperty ("fader", faderPosition (k.first, k.second));
        arr.add (v);
    }
    return arr;
}

void Playback::fromVar (const juce::var& v)
{
    clear();
    if (auto* arr = v.getArray())
        for (auto& e : *arr)
        {
            const Key k { juce::jmax (1, (int) e["page"]), juce::jlimit (1, executorsPerPage, (int) e["slot"]) };
            executors[k] = Executor::fromVar (e);
            const float pos = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("fader", 1.0));
            if (executors[k].faderFunction == FaderFunction::Speed) { states[k].speedFader = pos; states[k].fader = 1.0f; }
            else states[k].fader = pos;
        }
}

void Playback::restoreExecutors (const juce::var& v, double now)
{
    std::map<Key, Executor> restored;
    if (auto* arr = v.getArray())
        for (auto& e : *arr)
            restored[{ juce::jmax (1, (int) e["page"]), juce::jlimit (1, executorsPerPage, (int) e["slot"]) }] = Executor::fromVar (e);
    executors = std::move (restored);
    for (auto& [k, st] : states)
    {
        auto* e = find (k.first, k.second);
        if (e == nullptr || e->cues.empty()) { if (st.isOn()) off (k.first, k.second); continue; }
        if (! st.isOn()) continue;
        st.cue = juce::jmin (st.cue, (int) e->cues.size() - 1);
        st.target = targetFor (*e, st.cue, &st.liveEffects);
        st.from.clear();
        st.fadeIn = st.fadeOut = 0.0f;
        st.fadeStart = now;
    }
}

} // namespace pl
