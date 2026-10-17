#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include "Effects.h"

namespace pl
{

using ValueMap = std::map<int, std::map<juce::String, float>>;   // fid → attribute → 0..1

juce::var valueMapToVar (const ValueMap&);
ValueMap valueMapFromVar (const juce::var&);

/** A "folder" in a cue: a group of fixtures (live: change the group and every cue
    follows) with values and preset references. Preset references are looked up
    when the cue plays, so updating a preset updates every cue that uses it. */
struct CuePart
{
    juce::String name;
    int group = 0;                              // > 0: the fixtures of this group, live
    std::vector<int> fixtures;                  // used when group == 0 (fixtures or pixels)
    std::map<juce::String, float> values;       // attribute → value on every fixture of the folder ("Dimmer" → 1)
    std::map<juce::String, int> presets;        // preset pool key ("position", "color"...) → preset number
    std::vector<EffectParams> effects;          // effects on the folder's fixtures

    bool operator== (const CuePart& o) const
    {
        return name == o.name && group == o.group && fixtures == o.fixtures && values == o.values && presets == o.presets
            && effects.size() == o.effects.size();
    }

    juce::var toVar() const;
    static CuePart fromVar (const juce::var&);
};

struct Cue
{
    double number = 1.0;
    juce::String name;
    float fadeTime = 0.0f;                  // fade in (seconds): rising intensity and all other attributes
    float fadeOut = 0.0f;                   // fade out (seconds): falling intensity, and releasing the cue list
    ValueMap values;                        // tracks into later cues
    std::vector<EffectParams> effects;      // track like values, until replaced (same kind on the same fixtures) or stopped
    std::vector<CuePart> parts;             // folders: groups + preset references (track like values)

    juce::var toVar() const;
    static Cue fromVar (const juce::var&);
};

/** What an executor button does. */
enum class ButtonFunction { None, Go, Back, Off, Flash, Swop, Temp, Toggle, Pause, Select, RateUp, RateDown };

/** What an executor fader does. */
enum class FaderFunction { Master, Speed };

const juce::StringArray& buttonFunctionNames();      // in enum order
juce::String buttonFunctionName (ButtonFunction);
ButtonFunction buttonFunctionFromName (const juce::String&);

enum class ChaseMode { Loop, Bounce, Random };
const juce::StringArray& chaseModeNames();

/** An executor holds one cue list (sequence), MA-style. */
struct Executor
{
    static constexpr int numButtons = 4;    // strip order: Go, Back, Off, Flash by default

    juce::String name;
    std::vector<Cue> cues;
    bool tracking = true;                   // values carry into later cues until changed

    ButtonFunction buttons[numButtons] { ButtonFunction::Go, ButtonFunction::Back, ButtonFunction::Off, ButtonFunction::Flash };
    FaderFunction faderFunction = FaderFunction::Master;

    // chase: steps through the cues on its own, timed by the Dimmer FX speed master
    bool chase = false;
    float chaseRate = 1.0f;                 // steps per beat
    float chaseFade = 0.0f;                 // crossfade, as a fraction of a step (0..1)
    ChaseMode chaseMode = ChaseMode::Loop;

    juce::var toVar() const;
    static Executor fromVar (const juce::var&);
};

enum class StoreMode { Add, Merge, Overwrite };

/** Runtime state of an executor (not saved). */
struct ExecutorState
{
    int cue = -1;                           // index into cues, -1 = off
    float fader = 0.0f;                     // empty executors sit at 0; storing a new one brings it to full
    bool flash = false;
    bool flashStarted = false;              // flash turned it on → turn off on release
    juce::uint64 order = 0;                 // activation order for LTP

    ValueMap target, from;
    std::vector<EffectParams> liveEffects;  // effects running in the current cue: tracked cue effects + folder effects
    double fadeStart = 0.0;
    float fadeIn = 0.0f, fadeOut = 0.0f;

    bool releasing = false;                 // fading out after Off / fader to zero
    double releaseStart = 0.0;
    float releaseTime = 0.0f;

    bool swop = false, swopStarted = false;  // Swop held: full, all other executors' intensity out
    bool temp = false, tempStarted = false;  // Temp held: on at fader level while held

    // chase
    float speedFader = 0.5f;                // Speed fader position (0.5 = 1x)
    double chasePhase = 0.0, lastUpdate = -1.0;
    bool paused = false;
    int bounceDirection = 1;
    float rateScale = 1.0f;                 // Rate Up / Rate Down buttons

    bool isOn() const   { return cue >= 0; }
    float level() const { return flash || swop ? 1.0f : fader; }
};

/** All executors on all pages (sparse, unlimited pages) and their playback state.
    Pages and slots are 1-based. Not thread-safe: guard with the engine's dataLock. */
class Playback
{
public:
    static constexpr int executorsPerPage = 10;

    using Key = std::pair<int, int>;        // page, slot

    const Executor* find (int page, int slot) const;
    Executor* findMutable (int page, int slot);
    const ExecutorState& state (int page, int slot) const;
    int highestUsedPage() const;

    // --- actions (now = seconds) -------------------------------------------
    void go (int page, int slot, double now);
    void back (int page, int slot, double now);
    void off (int page, int slot);              // immediate
    /** Off with the current cue's fade-out time (Off key, fader to zero). */
    void release (int page, int slot, double now);
    /** Finishes releases whose fade-out has completed and steps chases.
        Call once per frame; `beatsPerSecond` is the chase tempo (Dimmer FX speed). */
    void update (double now, float beatsPerSecond = 1.0f);

    /** Press / release one of an executor's buttons (0-based, strip order); does
        whatever function it is set to. Select is handled by the engine. */
    void button (int page, int slot, int index, bool down, double now);
    void setSwop (int page, int slot, bool down, double now);
    void setTemp (int page, int slot, bool down, double now);
    void toggle (int page, int slot, double now);
    void togglePause (int page, int slot);
    bool anySwop() const;

    /** Where the fader sits (level or speed, depending on its function). */
    float faderPosition (int page, int slot) const;

    void setButtonFunction (int page, int slot, int index, ButtonFunction);
    void setFaderFunction (int page, int slot, FaderFunction);
    void setChase (int page, int slot, bool on, float rate, float fade, ChaseMode);
    void setTracking (int page, int slot, bool on);
    void gotoCue (int page, int slot, int cueIndex, double now);
    void setFader (int page, int slot, float value, double now);   // auto start / stop
    void setFlash (int page, int slot, bool down, double now);
    void offAll();

    /** Stores values + effects (+ folders) onto an executor. Returns a message for the UI. */
    juce::String store (int page, int slot, const ValueMap&, const std::vector<EffectParams>&,
                        StoreMode, double now, const std::vector<CuePart>& parts = {},
                        int cueIndex = -1);

    /** Effects running in cue `index` (tracked from earlier cues when tracking is on),
        each tagged with the cue it comes from. */
    struct CueEffect { EffectParams effect; int fromCue = 0; bool fromFolder = false; };
    std::vector<CueEffect> effectsAt (const Executor&, int index) const;

    /** Stops a running effect from cue `cueIndex` on (adds a stop marker), or removes
        it when it is stored in that cue. */
    void stopEffect (int page, int slot, int cueIndex, const EffectParams&);
    /** Removes a stop marker / effect stored in a cue. */
    void removeCueEffect (int page, int slot, int cueIndex, int effectId);
    /** Re-targets a running executor after its cues were edited. */
    void refreshExecutor (int page, int slot);

    /** Resolves a folder to values (and its effects with their fixtures). Set by the
        engine; without it folders are ignored. */
    using PartResolver = std::function<ValueMap (const CuePart&, std::vector<EffectParams>& effects)>;
    PartResolver partResolver;

    /** Final values of cue `index`: tracked values plus resolved folders. */
    ValueMap targetFor (const Executor&, int index, std::vector<EffectParams>* liveEffects = nullptr) const;

    /** Re-resolves the folders of running cues (groups and presets may have changed).
        Call once per frame. */
    void refreshParts();

    void setCueParts (int page, int slot, int cueIndex, const std::vector<CuePart>&);

    void deleteCue (int page, int slot, int cueIndex);
    void deleteExecutor (int page, int slot);
    /** Copy an executor (cue list and settings) to another slot; Move swaps the two
        slots (running state goes along). Returns false when the source is empty. */
    bool copyExecutor (int fromPage, int fromSlot, int toPage, int toSlot, bool move);
    /** Copy / move cue `from` to just after cue `after` (-1 = first) in the same cue list. */
    bool copyCue (int page, int slot, int from, int after, bool move);
    /** Every folder in every cue: replace group / preset references (after a preset moved). */
    void remapPresetReferences (const juce::String& poolKey, int from, int to, bool swap);
    void renameExecutor (int page, int slot, const juce::String&);
    void setCueProperties (int page, int slot, int cueIndex, const juce::String& name, float fadeIn, float fadeOut);

    // --- rendering ---------------------------------------------------------
    struct Active { Key key; const Executor* exec; const ExecutorState* state; };
    std::vector<Active> activeInOrder() const;

    /** Values of a running executor at `now`, including the cue fade (rising
        intensity and other attributes use fade in, falling intensity fade out)
        and any release. */
    static ValueMap currentValues (const ExecutorState&, double now);

    /** Values of cue `index`: with tracking, all earlier cues merged in. */
    static ValueMap trackedState (const Executor&, int index);

    void clear();
    /** With `withFaders`, each executor also stores its fader position (show files). */
    juce::var toVar (bool withFaders = true) const;
    void fromVar (const juce::var&);

    /** Undo: replaces the executors' data but keeps what is running (faders,
        current cues) where those executors still exist. */
    void restoreExecutors (const juce::var&, double now);

    const std::map<Key, Executor>& all() const { return executors; }

private:
    void startCue (const Key&, int index, double now);
    int nextChaseStep (const Executor&, ExecutorState&);
    ExecutorState& stateFor (const Key& k) { return states[k]; }

    std::map<Key, Executor> executors;
    std::map<Key, ExecutorState> states;
    juce::uint64 orderCounter = 0;
};

} // namespace pl
