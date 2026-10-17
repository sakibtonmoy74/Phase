#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include "Effects.h"

namespace pl
{

/** A small RGB frame (pixel mapping only needs low resolution). */
struct VideoFrame
{
    int width = 0, height = 0;
    std::vector<juce::uint32> pixels;           // 0xAARRGGBB, row-major

    /** Bilinear sample at u, v in 0..1. */
    RGBf sample (float u, float v) const;
};

/** Latest frame per video source, shared between the receiver threads and
    the engine. Thread-safe. */
class VideoHub
{
public:
    void setFrame (const juce::String& source, std::shared_ptr<const VideoFrame> frame);
    std::shared_ptr<const VideoFrame> getFrame (const juce::String& source) const;
    juce::StringArray sources() const;
    void remove (const juce::String& source);

private:
    mutable juce::CriticalSection lock;
    std::map<juce::String, std::shared_ptr<const VideoFrame>> frames;
};

/** A pixel map region drawn on the Layout view. Fixtures (and the cells of
    pixel fixtures) inside it take their colour from the video source. */
struct PixelMap
{
    int id = 0;
    juce::String name;
    bool enabled = true;
    float x = 0.0f, y = 0.0f, w = 200.0f, h = 100.0f;   // layout units
    juce::String source { "Test: Rainbow" };          // "Test: ..." or "NDI: <source name>"
    float intensity = 1.0f;                           // master for the mapped fixtures
    bool driveDimmer = true;                          // also set fixture intensity (else colour only)

    // source "Effect": a Phase colour or dimmer effect drawn across the map as a wave
    EffectParams effect = defaultEffect();
    juce::String direction { "Left to right" };       // see pixelmap::directions()
    float waves = 1.0f;                               // how many effect cycles fit across the map

    static EffectParams defaultEffect();

    bool contains (float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }

    juce::var toVar() const;
    static PixelMap fromVar (const juce::var&);
};

namespace pixelmap
{
    /** Built-in test sources (work without any video input). */
    const juce::StringArray& testSources();

    /** The source name for the built-in effect generator. */
    inline const char* effectSource() { return "Effect"; }

    /** Directions an effect travels across a map. */
    const juce::StringArray& directions();

    /** Position 0..1 of u, v along a direction (the wave travels towards 1). */
    float along (const juce::String& direction, float u, float v);

    /** Colour of a test source at u, v (0..1) and time t (seconds). */
    RGBf testPattern (const juce::String& source, float u, float v, double t);

    /** Colour for a point of a map: test pattern, effect, or the hub's frame for NDI
        sources. `effectCycles` is the map effect's running phase. Returns false
        when the source has no frame yet. */
    bool sampleMap (const PixelMap&, const VideoHub*, float u, float v, double t, RGBf& out,
                    double effectCycles = 0.0, float sizeMaster = 1.0f);
}

} // namespace pl
