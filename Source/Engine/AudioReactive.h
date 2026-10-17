#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

namespace pl
{

/** Audio analysis bands. */
enum class AudioSource { Level, Bass, Mid, High, Beat };

const std::vector<AudioSource>& allAudioSources();
juce::String audioSourceName (AudioSource);

/** Live audio analysis, written by the audio thread, read by the engine (0..1). */
struct AudioLevels
{
    std::atomic<float> level { 0.0f }, bass { 0.0f }, mid { 0.0f }, high { 0.0f }, beat { 0.0f };

    float get (AudioSource s) const
    {
        switch (s)
        {
            case AudioSource::Level: return level.load();
            case AudioSource::Bass:  return bass.load();
            case AudioSource::Mid:   return mid.load();
            case AudioSource::High:  return high.load();
            case AudioSource::Beat:  return beat.load();
        }
        return 0.0f;
    }
};

/** Set: the attribute follows the audio between Min and Max.
    Scale: the attribute's value is multiplied by Min..Max following the audio
    (e.g. an audio intensity master on a group). */
enum class AudioLinkMode { Set, Scale };

/** Links an audio band to an attribute on fixtures (or on a group preset). */
struct AudioLink
{
    int id = 0;
    juce::String name;
    bool enabled = true;
    AudioSource source = AudioSource::Bass;
    juce::String attribute { "Dimmer" };
    AudioLinkMode mode = AudioLinkMode::Scale;
    float minimum = 0.0f, maximum = 1.0f;
    int groupNumber = 0;                    // > 0: use the fixtures of this group preset
    std::vector<int> fixtures;              // used when groupNumber == 0

    // response: smoothing and shaping so lights don't flicker with every sample
    float attackMs = 15.0f;                 // how fast it rises
    float releaseMs = 250.0f;               // how fast it falls back
    float threshold = 0.0f;                 // below this the link reads 0 (noise gate)
    float curve = 1.0f;                     // < 1 lifts quiet parts, > 1 makes it punchier

    float map (float level) const { return minimum + (maximum - minimum) * juce::jlimit (0.0f, 1.0f, level); }

    /** Threshold and curve applied to a raw 0..1 level (before smoothing). */
    float shape (float raw) const
    {
        raw = juce::jlimit (0.0f, 1.0f, raw);
        if (raw <= threshold) return 0.0f;
        const float x = (raw - threshold) / juce::jmax (0.001f, 1.0f - threshold);
        return std::pow (x, juce::jlimit (0.2f, 5.0f, curve));
    }

    /** One step of the attack / release envelope from `current` towards `target`. */
    float smooth (float current, float target, double dt) const
    {
        const float ms = target > current ? attackMs : releaseMs;
        if (ms <= 0.5f) return target;
        const float k = 1.0f - (float) std::exp (-dt * 1000.0 / (double) ms);
        return current + (target - current) * k;
    }

    juce::var toVar() const;
    static AudioLink fromVar (const juce::var&);
};

/** Tap tempo: average of the last few intervals; a gap over 2 s starts again. */
class TapTempo
{
public:
    /** Registers a tap at `now` (seconds). Returns the tempo in BPM once known, else 0. */
    float tap (double now)
    {
        if (! taps.empty() && now - taps.back() > 2.0)
            taps.clear();
        taps.push_back (now);
        if (taps.size() > 5) taps.erase (taps.begin());
        if (taps.size() < 2) return 0.0f;
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        bpm = avg > 0.0 ? (float) (60.0 / avg) : 0.0f;
        return bpm;
    }
    float lastBpm() const { return bpm; }

private:
    std::vector<double> taps;
    float bpm = 0.0f;
};

} // namespace pl
