#include "Stage3D.h"

namespace pl::stage3d
{

V3 rotateX (V3 v, float deg)
{
    const float a = juce::degreesToRadians (deg), c = std::cos (a), s = std::sin (a);
    return { v.x, v.y * c - v.z * s, v.y * s + v.z * c };
}

V3 rotateY (V3 v, float deg)
{
    const float a = juce::degreesToRadians (deg), c = std::cos (a), s = std::sin (a);
    return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

V3 rotateZ (V3 v, float deg)
{
    const float a = juce::degreesToRadians (deg), c = std::cos (a), s = std::sin (a);
    return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
}

V3 position (const PatchedFixture& f)
{
    if (f.has3D) return { f.x3, f.y3, f.z3 };
    return { f.layoutX / 50.0f, 4.0f, f.layoutY / 50.0f };
}

V3 mount (const PatchedFixture& f, V3 v)
{
    return rotateY (rotateX (rotateZ (v, f.rz3), f.rx3), f.ry3);
}

V3 beamDirection (const PatchedFixture& f, const FixtureType& t, float pan, float tilt)
{
    V3 d { 0.0f, -1.0f, 0.0f };
    d = rotateX (d, -(tilt - 0.5f) * t.tiltRange);      // more tilt: towards the front (+z) and up
    d = rotateY (d, (pan - 0.5f) * t.panRange);
    return mount (f, d).normalised();
}

float beamAngle (const FixtureType& t, float zoom)
{
    if (zoom < 0.0f) return t.beamMin;
    return t.beamMin + (t.beamMax - t.beamMin) * juce::jlimit (0.0f, 1.0f, zoom);
}

V3 floorHit (V3 o, V3 d, float maxLength)
{
    if (d.y < -1e-4f)
    {
        const float t = -o.y / d.y;
        if (t > 0.0f && t < maxLength) return o + d * t;
    }
    return o + d * maxLength;
}

} // namespace pl::stage3d
