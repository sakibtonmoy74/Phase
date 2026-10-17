#pragma once
#include <juce_core/juce_core.h>
#include "Attributes.h"

namespace pl
{

/** Small 2D value (pan, tilt) so the engine doesn't depend on juce_graphics. */
struct Vec2
{
    float x = 0.0f, y = 0.0f;
    Vec2 operator* (float k) const { return { x * k, y * k }; }
};

/** Plain RGB colour (0..1) so the engine doesn't depend on juce_graphics. */
struct RGBf { float r = 0.0f, g = 0.0f, b = 0.0f; };

/** What an effect drives. */
enum class EffectKind
{
    Dimmer,      // intensity waveforms
    Movement,    // pan/tilt shapes around the current position
    Color,       // rainbow, or between colour A and B
    Attribute,   // a waveform on any attribute (zoom, focus, gobo rotation...)
    Fly          // movement + dimmer together: fly out / in / up / fan
};

juce::String effectKindName (EffectKind);

/** MA2-style effect. Phase is spread across `fixtures` in selection order,
    shaped by groups / blocks / wings (and Mirror, which forces 2 wings). */
struct EffectParams
{
    int id = 0;                         // unique, keeps the phase running across edits
    EffectKind kind = EffectKind::Dimmer;
    Family family = Family::Dimmer;     // feature group: picks the speed master (Position = movement)
    juce::String form;                  // see the forms lists below
    juce::String attribute;             // Attribute effects: which attribute
    juce::uint32 colourA = 0xffff0000u; // Color effects (ARGB)
    juce::uint32 colourB = 0xff0000ffu;

    float bpm = 60.0f;                  // speed
    float low = 0.0f, high = 1.0f;      // dimmer range (0..1)
    float size = 0.2f;                  // movement amplitude (fraction of full range, peak to peak): pan
    float tiltSize = -1.0f;             // tilt amplitude (< 0 = same as size)
    bool reverse = false;               // run the shape the other way round
    float phaseStart = 0.0f;            // degrees
    float phaseEnd = 360.0f;            // degrees
    float width = 0.5f;                 // duty cycle for Square / Pulse (0..1)
    int groups = 0, blocks = 0, wings = 0;
    bool mirror = false;
    bool stop = false;                  // cue lists: stops this effect (on these fixtures) from tracking on

    std::vector<int> fixtures;          // selection order matters

    bool contains (int fid) const;

    /** Two effects occupy the same programmer slot (same kind, and same attribute
        for attribute effects); storing one replaces the other. */
    bool sameSlot (const EffectParams& other) const
    {
        return kind == other.kind && (kind != EffectKind::Attribute || attribute == other.attribute);
    }

    /** Sets `family` from kind/attribute (call after changing either). */
    void updateFamily();

    /** Phase offset (0..1) of the fixture at `index` in `fixtures`. */
    float phaseOffsetFor (int index) const;

    /** True for fixtures on the mirrored side (odd wings with Mirror on): their
        pan runs the other way, so movement is symmetrical. */
    bool mirroredSide (int index) const;

    /** Position -1..1 of the fixture across the selection (fly / fan spreads). */
    float spreadFor (int index) const;

    float effectiveTiltSize() const { return tiltSize < 0.0f ? size : tiltSize; }

    juce::var toVar() const;
    static EffectParams fromVar (const juce::var&);

    static int nextId();
};

namespace effects
{
    const juce::StringArray& dimmerForms();     // Sine, Ramp Up, Ramp Down, Square, Pulse, Random
    const juce::StringArray& movementForms();   // Circle, Pan, Tilt, Figure 8, Diagonal, Square, Triangle, Diamond, Swing, Ballyhoo
    const juce::StringArray& flyForms();        // Fly Out, Fly In, Fly Up, Fly Fan
    const juce::StringArray& colourForms();     // Rainbow, A-B Fade, A-B Chase, A-B Ramp, A-Black-B-Black, A-B-Black, Pulse A-B, Pulse A, Random Hue
    const juce::StringArray& formsFor (EffectKind);

    /** Dimmer waveform at phase p (0..1) → 0..1 */
    float dimmerWave (const juce::String& form, float p, float width);

    /** Movement offset at phase p → x (pan), y (tilt), each -1..1 */
    Vec2 movementWave (const juce::String& form, float p);

    /** Value for a fixture: dimmer → absolute 0..1 (low..high; the size master only scales movement);
        position → offset to add to pan/tilt (already scaled by size). */
    float dimmerValue (const EffectParams&, double phaseCycles, int index, float sizeMaster);
    Vec2 movementOffset (const EffectParams&, double phaseCycles, int index, float sizeMaster);

    /** Fly effects: pan / tilt offset and dimmer (0..1) together. */
    struct FlyValue { Vec2 offset; float dimmer = 0.0f; };
    FlyValue flyValue (const EffectParams&, double phaseCycles, int index, float sizeMaster);

    /** Colour effect output for a fixture (the size master is ignored: it scales movement only). */
    RGBf colourValue (const EffectParams&, double phaseCycles, int index, float sizeMaster);

    /** The same at an absolute phase (cycles); `seedOffset` varies Random forms by position. */
    RGBf colourAtPhase (const EffectParams&, double cycles, float seedOffset, float sizeMaster);
    float dimmerAtPhase (const EffectParams&, double cycles, float seedOffset, float sizeMaster);
}

} // namespace pl
