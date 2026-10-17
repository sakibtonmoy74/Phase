#include <set>
#include "Patch.h"

namespace pl
{

std::pair<float, float> cellLayoutPosition (const PatchedFixture& f, const FixtureType& type, int cell)
{
    if (cell <= 0 || ! type.hasCells()) return { f.layoutX, f.layoutY };
    auto [dx, dy] = type.cellOffset (cell);
    if (f.layoutMirror) dx = -dx;
    const float a = juce::degreesToRadians (f.layoutRotation);
    const float c = std::cos (a), s = std::sin (a);
    return { f.layoutX + dx * c - dy * s, f.layoutY + dx * s + dy * c };
}

const PatchedFixture* Patch::find (int fid) const
{
    fid = targetFid (fid);
    for (auto& f : list)
        if (f.fid == fid)
            return &f;
    return nullptr;
}

juce::String Patch::checkConflicts (const PatchedFixture& f, const FixtureLibrary& lib,
                                    int ignoreFid) const
{
    auto* type = lib.find (f.typeId);
    if (type == nullptr)
        return "Unknown fixture type";
    if (f.fid <= 0)
        return "Fixture ID must be 1 or higher";
    if (f.universe < 1)
        return "Universe must be 1 or higher";
    if (f.address < 1 || f.address + type->footprint() - 1 > 512)
        return "Fixture doesn't fit in the universe at address " + juce::String (f.address);

    const int start = f.address, end = f.address + type->footprint() - 1;

    for (auto& other : list)
    {
        if (other.fid == ignoreFid)
            continue;
        if (other.fid == f.fid)
            return "Fixture ID " + juce::String (f.fid) + " is already used";
        if (other.universe != f.universe)
            continue;
        auto* ot = lib.find (other.typeId);
        if (ot == nullptr)
            continue;
        const int os = other.address, oe = other.address + ot->footprint() - 1;
        if (start <= oe && os <= end)
            return "Address overlaps fixture " + juce::String (other.fid)
                 + " (" + juce::String (other.universe) + "." + juce::String (other.address) + ")";
    }
    return {};
}

juce::String Patch::add (const PatchedFixture& f, const FixtureLibrary& lib)
{
    auto err = checkConflicts (f, lib);
    if (err.isNotEmpty())
        return err;
    list.push_back (f);
    sortByFid();
    return {};
}

juce::String Patch::update (int originalFid, const PatchedFixture& f, const FixtureLibrary& lib)
{
    auto err = checkConflicts (f, lib, originalFid);
    if (err.isNotEmpty())
        return err;
    for (auto& existing : list)
        if (existing.fid == originalFid)
            existing = f;
    sortByFid();
    return {};
}

juce::String Patch::repatch (const std::vector<int>& fids, int universe, int address, int gap, const FixtureLibrary& lib)
{
    if (fids.empty()) return "Select fixtures first";
    universe = juce::jmax (1, universe);
    address = juce::jlimit (1, 512, address);
    gap = juce::jlimit (0, 511, gap);

    auto moved = list;
    std::set<int> moving (fids.begin(), fids.end());
    int u = universe, a = address;
    for (int fid : fids)
    {
        auto it = std::find_if (moved.begin(), moved.end(), [fid] (const PatchedFixture& f) { return f.fid == fid; });
        if (it == moved.end()) continue;
        auto* t = lib.find (it->typeId);
        if (t == nullptr) return "Unknown fixture type for " + juce::String (fid);
        const int size = juce::jmax (1, t->footprint());
        if (size > 512) return "Fixture " + juce::String (fid) + " doesn't fit in a universe";
        if (a + size - 1 > 512) { ++u; a = 1; }
        it->universe = u;
        it->address = a;
        a += size + gap;
    }

    // overlaps with fixtures that stay where they are (or with each other)
    for (auto& f : moved)
    {
        if (moving.count (f.fid) == 0) continue;
        auto* t = lib.find (f.typeId);
        const int s0 = f.address, e0 = f.address + t->footprint() - 1;
        for (auto& o : moved)
        {
            if (o.fid == f.fid || o.universe != f.universe) continue;
            auto* ot = lib.find (o.typeId);
            if (ot == nullptr) continue;
            const int s1 = o.address, e1 = o.address + ot->footprint() - 1;
            if (s0 <= e1 && s1 <= e0)
                return "Fixture " + juce::String (f.fid) + " would overlap fixture " + juce::String (o.fid)
                     + " at " + juce::String (f.universe) + "." + juce::String (f.address)
                     + " - pick another start, or move that one too";
        }
    }
    list = std::move (moved);
    return {};
}

void Patch::setLayoutPosition (int fid, float x, float y)
{
    for (auto& f : list)
        if (f.fid == fid)
        {
            f.hasLayoutPosition = true;
            f.layoutX = x;
            f.layoutY = y;
        }
}

void Patch::setLayoutOrientation (int fid, float rotation, bool mirror)
{
    rotation = std::fmod (rotation, 360.0f);
    if (rotation < 0.0f) rotation += 360.0f;
    for (auto& f : list)
        if (f.fid == fid) { f.layoutRotation = rotation; f.layoutMirror = mirror; }
}

void Patch::setInvert (int fid, bool pan, bool tilt)
{
    for (auto& f : list)
        if (f.fid == fid) { f.invertPan = pan; f.invertTilt = tilt; }
}

void Patch::setHiddenInLayout (int fid, bool hidden)
{
    for (auto& f : list)
        if (f.fid == fid) f.hiddenInLayout = hidden;
}

void Patch::set3D (int fid, float x, float y, float z, float rx, float ry, float rz)
{
    for (auto& f : list)
        if (f.fid == fid)
        {
            f.has3D = true;
            f.x3 = x; f.y3 = y; f.z3 = z;
            f.rx3 = rx; f.ry3 = ry; f.rz3 = rz;
        }
}

void Patch::setFolder (int fid, const juce::String& folder)
{
    for (auto& f : list)
        if (f.fid == fid) f.folder = folder.trim();
}

void Patch::renameFolder (const juce::String& from, const juce::String& to)
{
    for (auto& f : list)
        if (f.folder == from) f.folder = to.trim();
}

juce::StringArray Patch::folders() const
{
    juce::StringArray out;
    for (auto& f : list)
        if (f.folder.isNotEmpty()) out.addIfNotAlreadyThere (f.folder);
    return out;
}

void Patch::remove (int fid)
{
    list.erase (std::remove_if (list.begin(), list.end(),
                                [fid] (const PatchedFixture& f) { return f.fid == fid; }),
                list.end());
}

juce::String Patch::addRange (const juce::String& typeId, int count, int firstFid,
                              int universe, int address, const juce::String& baseName,
                              const FixtureLibrary& lib, const juce::String& folder)
{
    auto* type = lib.find (typeId);
    if (type == nullptr)
        return "Unknown fixture type";
    if (type->footprint() == 0)
        return "Fixture type has no channels";

    // Validate everything first so a failed range adds nothing
    std::vector<PatchedFixture> toAdd;
    Patch scratch = *this;
    int fid = firstFid, uni = universe, addr = address;

    for (int i = 0; i < count; ++i)
    {
        if (addr + type->footprint() - 1 > 512) { ++uni; addr = 1; }

        PatchedFixture f;
        f.fid = fid;
        f.typeId = typeId;
        f.universe = uni;
        f.address = addr;
        f.name = (baseName.isNotEmpty() ? baseName : type->name) + " " + juce::String (i + 1);
        f.folder = folder.trim();

        auto err = scratch.add (f, lib);
        if (err.isNotEmpty())
            return "Fixture " + juce::String (fid) + ": " + err;

        ++fid;
        addr += type->footprint();
    }

    *this = scratch;
    return {};
}

int Patch::nextFreeFid() const
{
    int maxFid = 0;
    for (auto& f : list)
        maxFid = juce::jmax (maxFid, f.fid);
    return maxFid + 1;
}

std::vector<int> Patch::usedUniverses() const
{
    std::vector<int> u;
    for (auto& f : list)
        if (std::find (u.begin(), u.end(), f.universe) == u.end())
            u.push_back (f.universe);
    std::sort (u.begin(), u.end());
    return u;
}

int Patch::nextFreeAddress (int universe, const FixtureLibrary& lib) const
{
    int next = 1;
    for (auto& f : list)
        if (f.universe == universe)
            if (auto* t = lib.find (f.typeId))
                next = juce::jmax (next, f.address + t->footprint());
    return next;
}

void Patch::sortByFid()
{
    std::sort (list.begin(), list.end(),
               [] (const PatchedFixture& a, const PatchedFixture& b) { return a.fid < b.fid; });
}

juce::var Patch::toVar() const
{
    juce::Array<juce::var> arr;
    for (auto& f : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("fid", f.fid);
        o->setProperty ("name", f.name);
        o->setProperty ("type", f.typeId);
        o->setProperty ("universe", f.universe);
        o->setProperty ("address", f.address);
        if (f.folder.isNotEmpty()) o->setProperty ("folder", f.folder);
        if (f.hasLayoutPosition)
        {
            o->setProperty ("x", f.layoutX);
            o->setProperty ("y", f.layoutY);
        }
        if (std::abs (f.layoutRotation) > 0.001f) o->setProperty ("rotation", f.layoutRotation);
        if (f.layoutMirror) o->setProperty ("mirror", true);
        if (f.invertPan) o->setProperty ("invertPan", true);
        if (f.invertTilt) o->setProperty ("invertTilt", true);
        if (f.hiddenInLayout) o->setProperty ("hiddenInLayout", true);
        if (f.has3D)
        {
            juce::Array<juce::var> p3 { f.x3, f.y3, f.z3, f.rx3, f.ry3, f.rz3 };
            o->setProperty ("pos3d", p3);
        }
        arr.add (juce::var (o));
    }
    return arr;
}

void Patch::fromVar (const juce::var& v)
{
    list.clear();
    if (auto* arr = v.getArray())
        for (auto& o : *arr)
        {
            PatchedFixture f;
            f.fid      = (int) o["fid"];
            f.name     = o["name"].toString();
            f.typeId   = o["type"].toString();
            f.universe = juce::jmax (1, (int) o["universe"]);
            f.address  = juce::jlimit (1, 512, (int) o["address"]);
            f.folder   = o["folder"].toString();
            if (o.hasProperty ("x"))
            {
                f.hasLayoutPosition = true;
                f.layoutX = (float) (double) o["x"];
                f.layoutY = (float) (double) o["y"];
            }
            f.layoutRotation = (float) (double) o.getProperty ("rotation", 0.0);
            f.layoutMirror = (bool) o.getProperty ("mirror", false);
            f.invertPan = (bool) o.getProperty ("invertPan", false);
            f.invertTilt = (bool) o.getProperty ("invertTilt", false);
            f.hiddenInLayout = (bool) o.getProperty ("hiddenInLayout", false);
            if (auto* p3 = o["pos3d"].getArray(); p3 != nullptr && p3->size() == 6)
            {
                f.has3D = true;
                f.x3 = (float) (double) (*p3)[0]; f.y3 = (float) (double) (*p3)[1]; f.z3 = (float) (double) (*p3)[2];
                f.rx3 = (float) (double) (*p3)[3]; f.ry3 = (float) (double) (*p3)[4]; f.rz3 = (float) (double) (*p3)[5];
            }
            list.push_back (f);
        }
    sortByFid();
}

} // namespace pl
