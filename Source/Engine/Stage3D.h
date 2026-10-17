#pragma once
#include "Patch.h"

namespace pl
{

/** Small 3D maths for the visualizer (metres, y up). */
struct V3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    V3 operator+ (V3 o) const  { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator- (V3 o) const  { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator* (float k) const { return { x * k, y * k, z * k }; }
    float dot (V3 o) const     { return x * o.x + y * o.y + z * o.z; }
    V3 cross (V3 o) const      { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    float length() const       { return std::sqrt (dot (*this)); }
    V3 normalised() const      { const float l = length(); return l > 1e-6f ? *this * (1.0f / l) : V3 { 0, -1, 0 }; }
};

namespace stage3d
{
    V3 rotateX (V3, float degrees);
    V3 rotateY (V3, float degrees);
    V3 rotateZ (V3, float degrees);

    /** Fixture position in the 3D view: its own, or taken from the 2D layout
        (1 layout unit = 2 cm, hung at 4 m). */
    V3 position (const PatchedFixture&);

    /** A direction in the fixture's own frame turned by its mount (z, then x, then y). */
    V3 mount (const PatchedFixture&, V3 local);

    /** Beam direction for pan / tilt (0..1, 0.5 = straight out of the fixture: down when hung). */
    V3 beamDirection (const PatchedFixture&, const FixtureType&, float pan, float tilt);

    /** Beam angle in degrees for a zoom value (0..1), or the narrowest when there's no zoom. */
    float beamAngle (const FixtureType&, float zoom);

    /** Where a beam from `origin` along `dir` meets the floor (y = 0), or origin + dir * maxLength. */
    V3 floorHit (V3 origin, V3 dir, float maxLength);
}

} // namespace pl
