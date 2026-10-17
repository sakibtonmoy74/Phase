#pragma once
#include <juce_core/juce_core.h>
#include "FixtureType.h"

namespace pl
{

struct PatchedFixture
{
    int fid = 0;                // fixture ID, like MA's FID
    juce::String name;
    juce::String typeId;
    int universe = 1;           // 1-based, unlimited
    int address = 1;            // 1..512
    juce::String folder;        // patch folder ("" = top level)

    // Position in the Layout view (layout units, roughly pixels at 100%)
    bool hasLayoutPosition = false;
    float layoutX = 0.0f, layoutY = 0.0f;
    float layoutRotation = 0.0f;    // degrees clockwise (turns the cell grid of pixel fixtures)
    bool layoutMirror = false;      // cell grid mirrored left-right (before rotation)

    bool invertPan = false, invertTilt = false;   // output inverted (hung the other way round)
    bool hiddenInLayout = false;    // removed from the Layout view (still patched)

    // 3D view: position in metres (x right, y up, z towards the audience) and the
    // mount: rotation in degrees (x = tip forward/back, y = turn, z = roll).
    // A fixture with no rotation hangs with its beam pointing down.
    bool has3D = false;
    float x3 = 0.0f, y3 = 4.0f, z3 = 0.0f;
    float rx3 = 0.0f, ry3 = 0.0f, rz3 = 0.0f;
};

/** Layout position of one cell (1-based) of a fixture, with its rotation and
    mirror. Cell 0 (or a fixture without cells) is the fixture position. */
std::pair<float, float> cellLayoutPosition (const PatchedFixture&, const FixtureType&, int cell);

class Patch
{
public:
    const std::vector<PatchedFixture>& fixtures() const { return list; }

    /** Finds a fixture by FID; a cell target finds its fixture. */
    const PatchedFixture* find (int fidOrTarget) const;

    /** Returns an empty string on success or a description of the problem. */
    juce::String add (const PatchedFixture&, const FixtureLibrary&);
    juce::String update (int originalFid, const PatchedFixture&, const FixtureLibrary&);
    void remove (int fid);

    /** Re-patches `fids` (in that order) one after another from universe.address,
        leaving `gap` free channels between fixtures and moving on to the next
        universe when one doesn't fit. All or nothing: returns an error (and changes
        nothing) when the new addresses would overlap fixtures that aren't moving. */
    juce::String repatch (const std::vector<int>& fids, int universe, int address, int gap, const FixtureLibrary&);
    void setLayoutPosition (int fid, float x, float y);
    void setLayoutOrientation (int fid, float rotationDegrees, bool mirror);
    void setInvert (int fid, bool pan, bool tilt);
    void setHiddenInLayout (int fid, bool hidden);
    void set3D (int fid, float x, float y, float z, float rx, float ry, float rz);
    void setFolder (int fid, const juce::String& folder);
    void renameFolder (const juce::String& from, const juce::String& to);
    juce::StringArray folders() const;   // in first-use order
    void clear() { list.clear(); }

    /** Adds `count` fixtures, auto-incrementing FID and address and wrapping
        to the next universe when a fixture won't fit. */
    juce::String addRange (const juce::String& typeId, int count, int firstFid,
                           int universe, int address, const juce::String& baseName,
                           const FixtureLibrary&, const juce::String& folder = {});

    int nextFreeFid() const;
    std::vector<int> usedUniverses() const;

    /** Next free address after everything patched in `universe`. */
    int nextFreeAddress (int universe, const FixtureLibrary&) const;

    juce::String checkConflicts (const PatchedFixture&, const FixtureLibrary&,
                                 int ignoreFid = -1) const;

    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    void sortByFid();
    std::vector<PatchedFixture> list;
};

} // namespace pl
