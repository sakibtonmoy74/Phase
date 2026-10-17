#include "Effects.h"

namespace pl
{

static float fracf (double v) { return (float) (v - std::floor (v)); }

juce::String effectKindName (EffectKind k)
{
    switch (k)
    {
        case EffectKind::Dimmer:    return "Dimmer";
        case EffectKind::Movement:  return "Movement";
        case EffectKind::Color:     return "Color";
        case EffectKind::Attribute: return "Attribute";
        case EffectKind::Fly:       return "Fly";
    }
    return {};
}

void EffectParams::updateFamily()
{
    switch (kind)
    {
        case EffectKind::Dimmer:    family = Family::Dimmer; break;
        case EffectKind::Movement:  family = Family::Position; break;
        case EffectKind::Color:     family = Family::Color; break;
        case EffectKind::Attribute: family = familyOf (attribute); break;
        case EffectKind::Fly:       family = Family::Dimmer; break;      // timed by the dimmer speed (tempo)
    }
}

bool EffectParams::contains (int fid) const
{
    return std::find (fixtures.begin(), fixtures.end(), fid) != fixtures.end();
}

float EffectParams::phaseOffsetFor (int index) const
{
    const int n = (int) fixtures.size();
    if (n <= 1 || index < 0 || index >= n)
        return phaseStart / 360.0f;

    int wingCount = mirror ? juce::jmax (2, wings) : wings;
    if (wingCount < 1) wingCount = 1;
    wingCount = juce::jmin (wingCount, n);

    const int wingSize = (n + wingCount - 1) / wingCount;
    const int wing = index / wingSize;
    int pos = index % wingSize;
    const int wingLen = juce::jmin (wingSize, n - wing * wingSize);
    if (wing % 2 == 1)
        pos = wingLen - 1 - pos;           // odd wings run backwards → symmetry

    int count = wingSize;
    if (blocks > 1) { pos /= blocks; count = (count + blocks - 1) / blocks; }
    if (groups > 1) { pos %= groups; count = juce::jmin (count, groups); }

    const float frac = count > 1 ? (float) pos / (float) count : 0.0f;
    return (phaseStart + (phaseEnd - phaseStart) * frac) / 360.0f;
}

bool EffectParams::mirroredSide (int index) const
{
    const int n = (int) fixtures.size();
    if (! mirror || n <= 1 || index < 0 || index >= n) return false;
    const int wingCount = juce::jmin (n, juce::jmax (2, wings));
    const int wingSize = (n + wingCount - 1) / wingCount;
    return (index / wingSize) % 2 == 1;
}

float EffectParams::spreadFor (int index) const
{
    const int n = (int) fixtures.size();
    if (n <= 1 || index < 0 || index >= n) return 0.0f;
    return 2.0f * (float) index / (float) (n - 1) - 1.0f;
}

juce::var EffectParams::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("family", family == Family::Position ? "Position" : "Dimmer");
    o->setProperty ("kind", effectKindName (kind));
    o->setProperty ("form", form);
    if (attribute.isNotEmpty()) o->setProperty ("attribute", attribute);
    if (kind == EffectKind::Color)
    {
        o->setProperty ("colourA", (juce::int64) colourA);
        o->setProperty ("colourB", (juce::int64) colourB);
    }
    o->setProperty ("bpm", bpm);
    o->setProperty ("low", low);
    o->setProperty ("high", high);
    o->setProperty ("size", size);
    if (tiltSize >= 0.0f) o->setProperty ("tiltSize", tiltSize);
    if (reverse) o->setProperty ("reverse", true);
    o->setProperty ("phaseStart", phaseStart);
    o->setProperty ("phaseEnd", phaseEnd);
    o->setProperty ("width", width);
    o->setProperty ("groups", groups);
    o->setProperty ("blocks", blocks);
    o->setProperty ("wings", wings);
    o->setProperty ("mirror", mirror);
    if (stop) o->setProperty ("stop", true);
    juce::Array<juce::var> f;
    for (int fid : fixtures) f.add (fid);
    o->setProperty ("fixtures", f);
    return juce::var (o);
}

EffectParams EffectParams::fromVar (const juce::var& v)
{
    EffectParams e;
    e.id         = (int) v["id"];
    e.family     = v["family"].toString() == "Position" ? Family::Position : Family::Dimmer;
    e.kind       = e.family == Family::Position ? EffectKind::Movement : EffectKind::Dimmer;   // v0.2-0.4 files
    for (auto k : { EffectKind::Dimmer, EffectKind::Movement, EffectKind::Color, EffectKind::Attribute, EffectKind::Fly })
        if (v["kind"].toString() == effectKindName (k)) e.kind = k;
    e.form       = v["form"].toString();
    e.attribute  = v["attribute"].toString();
    if (v.hasProperty ("colourA")) e.colourA = (juce::uint32) (juce::int64) v["colourA"];
    if (v.hasProperty ("colourB")) e.colourB = (juce::uint32) (juce::int64) v["colourB"];
    auto num = [&v] (const char* k, float def) { return v.hasProperty (k) ? (float) (double) v[k] : def; };
    e.bpm        = num ("bpm", 60.0f);
    e.low        = num ("low", 0.0f);
    e.high       = num ("high", 1.0f);
    e.size       = num ("size", 0.2f);
    e.tiltSize   = num ("tiltSize", -1.0f);
    e.reverse    = (bool) v.getProperty ("reverse", false);
    e.phaseStart = num ("phaseStart", 0.0f);
    e.phaseEnd   = num ("phaseEnd", 360.0f);
    e.width      = num ("width", 0.5f);
    e.groups     = (int) v["groups"];
    e.blocks     = (int) v["blocks"];
    e.wings      = (int) v["wings"];
    e.mirror     = (bool) v["mirror"];
    e.stop       = (bool) v.getProperty ("stop", false);
    if (auto* arr = v["fixtures"].getArray())
        for (auto& f : *arr) e.fixtures.push_back ((int) f);
    if (e.id == 0) e.id = nextId();
    e.updateFamily();
    return e;
}

int EffectParams::nextId()
{
    static std::atomic<int> counter { (int) (juce::Time::currentTimeMillis() % 100000) * 100 };
    return ++counter;
}

//==============================================================================
namespace effects
{
    const juce::StringArray& dimmerForms()
    {
        static const juce::StringArray f { "Sine", "Ramp Up", "Ramp Down", "Square", "Pulse", "Random" };
        return f;
    }

    const juce::StringArray& movementForms()
    {
        static const juce::StringArray f { "Circle", "Pan", "Tilt", "Figure 8", "Diagonal",
                                           "Square", "Triangle", "Diamond", "Swing", "Ballyhoo" };
        return f;
    }

    const juce::StringArray& flyForms()
    {
        static const juce::StringArray f { "Fly Out", "Fly In", "Fly Up", "Fly Fan" };
        return f;
    }

    const juce::StringArray& colourForms()
    {
        static const juce::StringArray f { "Rainbow", "A-B Fade", "A-B Chase", "A-B Ramp",
                                           "A-Black-B-Black", "A-B-Black", "Pulse A-B", "Pulse A", "Random Hue" };
        return f;
    }

    const juce::StringArray& formsFor (EffectKind k)
    {
        if (k == EffectKind::Movement) return movementForms();
        if (k == EffectKind::Color)    return colourForms();
        if (k == EffectKind::Fly)      return flyForms();
        return dimmerForms();
    }

    float dimmerWave (const juce::String& form, float p, float width)
    {
        width = juce::jlimit (0.01f, 1.0f, width);
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        if (form == "Ramp Up")   return p;
        if (form == "Ramp Down") return 1.0f - p;
        if (form == "Square")    return p < width ? 1.0f : 0.0f;
        if (form == "Pulse")     return p < width ? std::sin (juce::MathConstants<float>::pi * p / width) : 0.0f;
        return 0.5f - 0.5f * std::cos (twoPi * p);   // Sine (starts at Low)
    }

    /** Point on a closed path through the given corners (p = 0..1 around it). */
    static Vec2 polygon (std::initializer_list<Vec2> pts, float p)
    {
        const int n = (int) pts.size();
        const float f = p * (float) n;
        const int i = juce::jlimit (0, n - 1, (int) f);
        const float t = f - (float) i;
        const Vec2 a = *(pts.begin() + i), b = *(pts.begin() + (i + 1) % n);
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    }

    Vec2 movementWave (const juce::String& form, float p)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        const float s = std::sin (twoPi * p), c = std::cos (twoPi * p);
        if (form == "Pan")      return { s, 0.0f };
        if (form == "Tilt")     return { 0.0f, s };
        if (form == "Figure 8") return { s, std::sin (2.0f * twoPi * p) };
        if (form == "Diagonal") return { s, s };
        if (form == "Square")   return polygon ({ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }, p);
        if (form == "Triangle") return polygon ({ { 0, -1 }, { 0.866f, 0.5f }, { -0.866f, 0.5f } }, p);
        if (form == "Diamond")  return polygon ({ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }, p);
        if (form == "Swing")    return { s, std::abs (c) * 2.0f - 1.0f };           // pendulum: dips in the middle
        if (form == "Ballyhoo") return { s, std::sin (3.0f * twoPi * p) * 0.7f };   // wide, busy sweep
        return { s, c };                                // Circle
    }

    static float hashUnit (juce::int64 a, juce::uint32 b)
    {
        auto seed = (juce::uint32) (a * 2654435761u + b * 40503u);
        seed ^= seed >> 13; seed *= 0x5bd1e995; seed ^= seed >> 15;
        return (float) (seed % 1000) / 999.0f;
    }

    float dimmerAtPhase (const EffectParams& e, double t, float seedOffset, float sizeMaster)
    {
        float w;
        if (e.form == "Random")      // new random level each cycle, different per position
            w = hashUnit ((juce::int64) std::floor (t), (juce::uint32) (seedOffset * 997.0f));
        else
            w = dimmerWave (e.form, fracf (t), e.width);
        juce::ignoreUnused (sizeMaster);      // the FX size master is for movement only
        return juce::jlimit (0.0f, 1.0f, e.low + (e.high - e.low) * w);
    }

    float dimmerValue (const EffectParams& e, double cycles, int index, float sizeMaster)
    {
        const float off = e.phaseOffsetFor (index);
        return dimmerAtPhase (e, cycles + (double) off, off, sizeMaster);
    }

    static RGBf toRGB (juce::uint32 argb)
    {
        return { (float) ((argb >> 16) & 0xff) / 255.0f, (float) ((argb >> 8) & 0xff) / 255.0f, (float) (argb & 0xff) / 255.0f };
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

    RGBf colourAtPhase (const EffectParams& e, double t, float seedOffset, float sizeMaster)
    {
        const float p = fracf (t);
        juce::ignoreUnused (sizeMaster);      // the FX size master is for movement only
        const auto a = toRGB (e.colourA), b = toRGB (e.colourB);
        const RGBf black;
        auto mix = [] (RGBf x, RGBf y, float k) { return RGBf { x.r + (y.r - x.r) * k, x.g + (y.g - x.g) * k, x.b + (y.b - x.b) * k }; };
        auto scale = [] (RGBf x, float k) { return RGBf { x.r * k, x.g * k, x.b * k }; };
        const float width = juce::jlimit (0.01f, 1.0f, e.width);
        auto pulse = [width] (float q) { return q < width ? std::sin (juce::MathConstants<float>::pi * q / width) : 0.0f; };

        RGBf out;
        if (e.form == "Rainbow")              out = hsv (p, 1.0f, 1.0f);
        else if (e.form == "A-B Chase")       out = p < width ? a : b;
        else if (e.form == "A-B Ramp")        out = mix (a, b, p);
        else if (e.form == "A-Black-B-Black") { const int step = juce::jlimit (0, 3, (int) (p * 4.0f)); out = step == 0 ? a : step == 2 ? b : black; }
        else if (e.form == "A-B-Black")       { const int step = juce::jlimit (0, 2, (int) (p * 3.0f)); out = step == 0 ? a : step == 1 ? b : black; }
        else if (e.form == "Pulse A-B")       out = p < 0.5f ? scale (a, pulse (p * 2.0f)) : scale (b, pulse (p * 2.0f - 1.0f));
        else if (e.form == "Pulse A")         out = scale (a, pulse (p));
        else if (e.form == "Random Hue")      out = hsv (hashUnit ((juce::int64) std::floor (t), (juce::uint32) (seedOffset * 997.0f)), 1.0f, 1.0f);
        else                                  out = mix (a, b, 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p));   // A-B Fade
        return out;
    }

    RGBf colourValue (const EffectParams& e, double cycles, int index, float sizeMaster)
    {
        const float off = e.phaseOffsetFor (index);
        return colourAtPhase (e, cycles + (double) off, off, sizeMaster);
    }

    Vec2 movementOffset (const EffectParams& e, double cycles, int index, float sizeMaster)
    {
        const double t = cycles + (double) e.phaseOffsetFor (index);
        float p = fracf (t);
        if (e.reverse) p = 1.0f - p;
        auto w = movementWave (e.form, p);
        if (e.mirroredSide (index)) w.x = -w.x;        // symmetrical: the other side pans the other way
        const float m = 0.5f * juce::jlimit (0.0f, 1.0f, sizeMaster);
        return { w.x * e.size * m, w.y * e.effectiveTiltSize() * m };
    }

    FlyValue flyValue (const EffectParams& e, double cycles, int index, float sizeMaster)
    {
        // moves only while lit: travel and a dimmer pulse over the first `width` of the
        // cycle, then dark (and back to the start) until the next one
        const float q = fracf (cycles + (double) e.phaseOffsetFor (index));
        const float w = juce::jlimit (0.05f, 1.0f, e.width);
        const float u = juce::jlimit (0.0f, 1.0f, q / w);
        const bool lit = q < w;
        float travel = e.form == "Fly In" ? 1.0f - u : u;

        FlyValue out;
        const float env = lit ? std::sin (juce::MathConstants<float>::pi * u) : 0.0f;
        out.dimmer = juce::jlimit (0.0f, 1.0f, e.low + (e.high - e.low) * env);
        const float m = juce::jlimit (0.0f, 1.0f, sizeMaster);
        if (! lit) travel = e.form == "Fly In" ? 1.0f : 0.0f;
        float spread = e.spreadFor (index);
        if (e.reverse) spread = -spread;
        // a higher tilt value lifts the beam, so "up" adds tilt
        if (e.form == "Fly Up")       out.offset = { 0.0f, travel * e.effectiveTiltSize() * m };
        else if (e.form == "Fly Fan") out.offset = { spread * travel * e.size * m, 0.0f };
        else                          out.offset = { spread * travel * e.size * m, travel * e.effectiveTiltSize() * m };
        return out;
    }
}

} // namespace pl
