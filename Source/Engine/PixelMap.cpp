#include "PixelMap.h"

namespace pl
{

static RGBf unpack (juce::uint32 c)
{
    return { (float) ((c >> 16) & 0xff) / 255.0f, (float) ((c >> 8) & 0xff) / 255.0f, (float) (c & 0xff) / 255.0f };
}

RGBf VideoFrame::sample (float u, float v) const
{
    if (width <= 0 || height <= 0 || pixels.size() < (size_t) (width * height)) return {};
    const float fx = juce::jlimit (0.0f, (float) (width - 1), u * (float) width - 0.5f);
    const float fy = juce::jlimit (0.0f, (float) (height - 1), v * (float) height - 0.5f);
    const int x0 = (int) fx, y0 = (int) fy;
    const int x1 = juce::jmin (x0 + 1, width - 1), y1 = juce::jmin (y0 + 1, height - 1);
    const float tx = fx - (float) x0, ty = fy - (float) y0;
    auto px = [this] (int x, int y) { return unpack (pixels[(size_t) (y * width + x)]); };
    auto mix = [] (RGBf a, RGBf b, float t) { return RGBf { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; };
    return mix (mix (px (x0, y0), px (x1, y0), tx), mix (px (x0, y1), px (x1, y1), tx), ty);
}

//==============================================================================
void VideoHub::setFrame (const juce::String& source, std::shared_ptr<const VideoFrame> frame)
{
    const juce::ScopedLock sl (lock);
    frames[source] = std::move (frame);
}

std::shared_ptr<const VideoFrame> VideoHub::getFrame (const juce::String& source) const
{
    const juce::ScopedLock sl (lock);
    auto it = frames.find (source);
    return it != frames.end() ? it->second : nullptr;
}

juce::StringArray VideoHub::sources() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray s;
    for (auto& [k, v] : frames) s.add (k);
    return s;
}

void VideoHub::remove (const juce::String& source)
{
    const juce::ScopedLock sl (lock);
    frames.erase (source);
}

//==============================================================================
EffectParams PixelMap::defaultEffect()
{
    EffectParams e;
    e.id = EffectParams::nextId();
    e.kind = EffectKind::Color;
    e.form = "Rainbow";
    e.bpm = 30.0f;
    e.updateFamily();
    return e;
}

juce::var PixelMap::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("enabled", enabled);
    o->setProperty ("x", x); o->setProperty ("y", y); o->setProperty ("w", w); o->setProperty ("h", h);
    o->setProperty ("source", source);
    o->setProperty ("intensity", intensity);
    o->setProperty ("driveDimmer", driveDimmer);
    o->setProperty ("effect", effect.toVar());
    o->setProperty ("direction", direction);
    o->setProperty ("waves", waves);
    return juce::var (o);
}

PixelMap PixelMap::fromVar (const juce::var& v)
{
    PixelMap m;
    m.id = (int) v["id"];
    m.name = v["name"].toString();
    m.enabled = v.getProperty ("enabled", true);
    m.x = (float) (double) v["x"]; m.y = (float) (double) v["y"];
    m.w = juce::jmax (1.0f, (float) (double) v.getProperty ("w", 200.0));
    m.h = juce::jmax (1.0f, (float) (double) v.getProperty ("h", 100.0));
    m.source = v.getProperty ("source", "Test: Rainbow").toString();
    m.intensity = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("intensity", 1.0));
    m.driveDimmer = v.getProperty ("driveDimmer", true);
    if (v.hasProperty ("effect"))
    {
        m.effect = EffectParams::fromVar (v["effect"]);
        m.effect.fixtures.clear();
    }
    m.direction = v.getProperty ("direction", "Left to right").toString();
    m.waves = juce::jlimit (0.05f, 64.0f, (float) (double) v.getProperty ("waves", 1.0));
    return m;
}

//==============================================================================
namespace pixelmap
{
    const juce::StringArray& testSources()
    {
        static const juce::StringArray s { "Test: Rainbow", "Test: Gradient", "Test: Bars", "Test: Plasma" };
        return s;
    }

    const juce::StringArray& directions()
    {
        static const juce::StringArray d { "Left to right", "Right to left", "Top to bottom", "Bottom to top",
                                           "Centre out", "Outside in", "Diagonal", "Clockwise" };
        return d;
    }

    float along (const juce::String& d, float u, float v)
    {
        if (d == "Right to left") return 1.0f - u;
        if (d == "Top to bottom") return v;
        if (d == "Bottom to top") return 1.0f - v;
        if (d == "Diagonal")      return (u + v) * 0.5f;
        const float dx = u - 0.5f, dy = v - 0.5f;
        if (d == "Centre out")    return juce::jmin (1.0f, std::sqrt (dx * dx + dy * dy) * 1.41421356f);
        if (d == "Outside in")    return 1.0f - juce::jmin (1.0f, std::sqrt (dx * dx + dy * dy) * 1.41421356f);
        if (d == "Clockwise")     return (std::atan2 (dy, dx) + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi;
        return u;                                     // Left to right
    }

    static RGBf hsv (float h, float s, float v)
    {
        h = (h - std::floor (h)) * 6.0f;
        const int i = (int) h;
        const float f = h - (float) i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i % 6)
        {
            case 0:  return { v, t, p };
            case 1:  return { q, v, p };
            case 2:  return { p, v, t };
            case 3:  return { p, q, v };
            case 4:  return { t, p, v };
            default: return { v, p, q };
        }
    }

    RGBf testPattern (const juce::String& source, float u, float v, double t)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        if (source == "Test: Gradient")                  // red → blue, left to right (static)
            return { 1.0f - u, 0.0f, u };
        if (source == "Test: Bars")                      // 8 colour bars scrolling sideways
        {
            static const RGBf bars[] { { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 0, 1, 0 },
                                       { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, 0 } };
            float pos = u - (float) std::fmod (t * 0.1, 1.0);
            pos -= std::floor (pos);
            return bars[juce::jlimit (0, 7, (int) (pos * 8.0f))];
        }
        if (source == "Test: Plasma")
        {
            const float tt = (float) t;
            const float val = std::sin (u * 10.0f + tt) + std::sin (v * 8.0f - tt * 1.3f)
                            + std::sin ((u + v) * 6.0f + tt * 0.7f);
            return hsv (val / 6.0f + tt * 0.05f, 1.0f, 1.0f);
        }
        // Rainbow: hue sweep across, scrolling
        juce::ignoreUnused (v, twoPi);
        return hsv (u - (float) std::fmod (t * 0.2, 1.0), 1.0f, 1.0f);
    }

    bool sampleMap (const PixelMap& m, const VideoHub* hub, float u, float v, double t, RGBf& out,
                    double effectCycles, float sizeMaster)
    {
        if (m.source == effectSource())
        {
            // the wave travels along the direction: points further along run behind
            const float s = along (m.direction, u, v);
            const double cycles = effectCycles - (double) (s * m.waves);
            if (m.effect.kind == EffectKind::Color)
                out = effects::colourAtPhase (m.effect, cycles, s, sizeMaster);
            else
            {
                const float level = effects::dimmerAtPhase (m.effect, cycles, s, sizeMaster);
                const auto a = m.effect.colourA;
                out = { (float) ((a >> 16) & 0xff) / 255.0f * level, (float) ((a >> 8) & 0xff) / 255.0f * level,
                        (float) (a & 0xff) / 255.0f * level };
            }
            return true;
        }
        if (m.source.startsWith ("Test:"))
        {
            out = testPattern (m.source, u, v, t);
            return true;
        }
        if (hub == nullptr) return false;
        auto frame = hub->getFrame (m.source);
        if (frame == nullptr) return false;
        out = frame->sample (u, v);
        return true;
    }
}

} // namespace pl
