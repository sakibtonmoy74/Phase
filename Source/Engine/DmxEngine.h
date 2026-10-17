#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "FixtureType.h"
#include "Patch.h"
#include "Programmer.h"
#include "Playback.h"
#include "Presets.h"
#include "AudioReactive.h"
#include "PixelMap.h"
#include "ArtNet.h"

namespace pl
{

/** Global masters. Speed masters: 0 = stopped, 0.5 = 100 %, 1.0 = 400 %
    (linear 0-100 % on the lower half, 100-400 % on the upper half). */
struct Masters
{
    std::atomic<float> grand { 1.0f };
    std::atomic<float> dimmerSpeed { 0.5f };
    std::atomic<float> movementSpeed { 0.5f };
    std::atomic<float> size { 1.0f };
    std::atomic<bool> blackout { false };

    static float speedMultiplier (float master)
    {
        master = juce::jlimit (0.0f, 1.0f, master);
        return master <= 0.5f ? master * 2.0f : 1.0f + (master - 0.5f) * 6.0f;
    }

    /** Inverse of speedMultiplier: fader position for a multiplier (0..4). */
    static float speedFaderFor (float multiplier)
    {
        multiplier = juce::jlimit (0.0f, 4.0f, multiplier);
        return multiplier <= 1.0f ? multiplier * 0.5f : 0.5f + (multiplier - 1.0f) / 6.0f;
    }

    /** Effects are written at their own BPM; a tapped tempo of `bpm` sets the
        dimmer speed master so a 60 BPM effect runs exactly at the tapped tempo. */
    static constexpr float referenceBpm = 60.0f;
};

/** Everything a frame needs, so rendering is a pure function (testable). */
struct RenderContext
{
    const FixtureLibrary& library;
    const Patch& patch;
    const Programmer& programmer;
    const Playback& playback;
    const std::map<int, double>& effectPhases;   // effect id → cycles
    double now = 0.0;                            // seconds
    float grandMaster = 1.0f;                    // effective (0 when blacked out)
    float sizeMaster = 1.0f;
    const std::vector<AudioLink>* audioLinks = nullptr;
    const AudioLevels* audio = nullptr;
    const PresetLibrary* presets = nullptr;
    const std::vector<PixelMap>* pixelMaps = nullptr;
    const VideoHub* video = nullptr;
    const std::map<int, float>* audioLinkLevels = nullptr;   // smoothed 0..1 per link id (raw when missing)
};

/** The core of the console: owns the show data, renders DMX frames on its
    own thread at a fixed rate and sends them out over Art-Net.

    Threading: the UI changes show data on the message thread while holding
    `dataLock`; the engine thread takes the same lock briefly per frame. */
class DmxEngine : private juce::Thread,
                  public juce::ChangeBroadcaster
{
public:
    DmxEngine();
    ~DmxEngine() override;

    void startEngine();
    void stopEngine();

    // --- Show data (lock dataLock to access) ---------------------------------
    juce::CriticalSection dataLock;
    FixtureLibrary library;
    Patch patch;
    Programmer programmer;
    Playback playback;
    PresetLibrary presets;
    std::vector<AudioLink> audioLinks;
    std::vector<PixelMap> pixelMaps;

    /** Latest video frames (NDI receivers write here; pixel maps read). */
    VideoHub video;

    /** Live audio analysis (written by the audio input, read every frame). */
    AudioLevels audio;

    /** Press / release executor button `index` (0-based) with its configured
        function. Select puts the cue's fixtures in the selection. Hold dataLock. */
    void executorButton (int page, int slot, int index, bool down, double when = now());

    /** Stores the programmer onto an executor. Values that came from presets are
        stored as folders that reference the presets (on the group, when the
        selection is a group), the rest as plain values. Hold dataLock. */
    juce::String storeFromProgrammer (int page, int slot, StoreMode, double when = now(), int cueIndex = -1);

    /** Resolves a cue folder: its live fixtures, values and preset references. Hold dataLock. */
    ValueMap resolvePart (const CuePart&, std::vector<EffectParams>& effects) const;

    /** Call on the message thread after changing show data so views refresh. */
    void showChanged() { sendChangeMessage(); }

    Masters masters;

    /** DMX tester: raw channel values sent on top of everything (not saved with the show).
        Hold dataLock to change it. */
    struct DmxTest
    {
        std::map<int, std::map<int, int>> values;   // universe -> channel (1-512) -> 0..255
        bool chase = false;                         // step a level through `chaseChannels`
        int chaseUniverse = 1;
        std::vector<int> chaseChannels;
        float chaseStepsPerSecond = 2.0f;
        int chaseLevel = 255;
        bool active() const { return ! values.empty() || (chase && ! chaseChannels.empty()); }
    } dmxTest;

    /** Applies the tester to a rendered frame (time t drives the chase). */
    static void applyDmxTest (const DmxTest&, UniverseMap&, double t);

    /** Seconds on the engine clock (use for playback actions). */
    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    // --- Network ---------------------------------------------------------------
    NetSettings getNetSettings() const;
    void setNetSettings (const NetSettings&);
    juce::String getNetStatus() const;
    int getPacketsPerSecond() const { return packetsPerSecond.load(); }

    // --- Output snapshot (for monitors and the layout view) -------------------
    bool getUniverse (int universe, DmxUniverse& out) const;
    std::vector<int> outputUniverses() const;

    /** Live values per fixture from the last frame: every attribute (0..1, colours
        before the virtual dimmer), "Dimmer" before the grand master and
        "Intensity" after it. For encoders and the layout view. */
    std::map<juce::String, float> getFixtureOutput (int fid) const;

    /** Smoothed level 0..1 of an audio link (for its meter). */
    float getAudioLinkLevel (int linkId) const;

    /** Advances the audio link envelopes (attack / release) by dt. */
    static void updateAudioLinks (const std::vector<AudioLink>&, const AudioLevels&, double dt, std::map<int, float>& levels);

    static void renderFrame (const RenderContext&, UniverseMap& out,
                             std::map<int, std::map<juce::String, float>>* resolved = nullptr);

    /** Advances effect phases by dt seconds, using the speed masters. */
    static void advanceEffects (const Programmer&, const Playback&, const Masters&,
                                double dt, std::map<int, double>& phases,
                                const std::vector<PixelMap>* pixelMaps = nullptr);

private:
    void run() override;

    ArtNetSender sender;
    std::map<int, double> effectPhases;          // engine thread only (copied under dataLock)

    mutable juce::CriticalSection netLock;
    NetSettings netSettings;
    juce::String netStatus;
    std::atomic<bool> netDirty { true };

    mutable juce::CriticalSection snapshotLock;
    UniverseMap snapshot;
    std::map<int, std::map<juce::String, float>> resolvedSnapshot;
    std::map<int, float> audioLinkLevels, audioLinkSnapshot;

    std::atomic<int> packetsPerSecond { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxEngine)
};

} // namespace pl
