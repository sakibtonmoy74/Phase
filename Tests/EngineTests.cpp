// Headless engine tests. Run: ./PhaseTests  (exit code 0 = all passed)
#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic ignored "-Wmissing-field-initializers"   // short fixture initialisers below
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include "Engine/DmxEngine.h"
#include "Engine/ShowFile.h"
#include "Engine/AudioAnalysis.h"
#include "Engine/Gdtf.h"
#include "Engine/UndoHistory.h"
#include "Engine/FixtureTemplates.h"
#include "Control/GdtfShare.h"
#include "Control/ProDjLink.h"
#include "Engine/CommandLine.h"
#include "Engine/Stage3D.h"
#include <iostream>

using namespace pl;

static int failures = 0, checks = 0;

#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; \
    std::cout << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

#define CHECK_EQ(a, b) do { ++checks; auto _a = (a); auto _b = (b); if (! (_a == _b)) { ++failures; \
    std::cout << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b \
              << "  (" << (long long) _a << " vs " << (long long) _b << ")\n"; } } while (0)

static void section (const char* name) { std::cout << "- " << name << "\n"; }

//==============================================================================
static void testArtDmxPacket()
{
    section ("ArtDmx packet layout");
    juce::uint8 data[512] {};
    data[0] = 11; data[511] = 99;

    auto mb = artnet::buildArtDmx (0x1234, 7, data, 512);
    auto* p = static_cast<const juce::uint8*> (mb.getData());

    CHECK_EQ ((int) mb.getSize(), 18 + 512);
    CHECK (std::memcmp (p, "Art-Net\0", 8) == 0);
    CHECK_EQ (p[8], 0x00);  CHECK_EQ (p[9], 0x50);      // OpDmx, little-endian
    CHECK_EQ (p[10], 0);    CHECK_EQ (p[11], 14);       // protocol 14
    CHECK_EQ (p[12], 7);                                // sequence
    CHECK_EQ (p[14], 0x34); CHECK_EQ (p[15], 0x12);     // SubUni, Net
    CHECK_EQ (p[16], 0x02); CHECK_EQ (p[17], 0x00);     // length 512, big-endian
    CHECK_EQ (p[18], 11);   CHECK_EQ (p[18 + 511], 99);

    auto odd = artnet::buildArtDmx (0, 1, data, 3);
    CHECK_EQ ((int) odd.getSize(), 18 + 4);             // length rounded up to even

    auto top = artnet::buildArtDmx (40000, 1, data, 512);
    auto* t = static_cast<const juce::uint8*> (top.getData());
    CHECK_EQ (t[15], 0x7f); CHECK_EQ (t[14], 0xff);     // clamped to 15-bit
}

static void testArtPoll()
{
    section ("ArtPoll / ArtPollReply");
    auto poll = artnet::buildArtPoll();
    CHECK_EQ ((int) poll.getSize(), 14);
    CHECK_EQ (artnet::opCodeOf (poll.getData(), (int) poll.getSize()), 0x2000);

    juce::uint8 reply[239] {};
    std::memcpy (reply, "Art-Net\0", 8);
    reply[8] = 0x00; reply[9] = 0x21;
    reply[10] = 2; reply[11] = 0; reply[12] = 0; reply[13] = 10;
    std::memcpy (reply + 26, "Node A", 6);
    std::memcpy (reply + 44, "Test Node Long Name", 19);
    reply[173] = 4;

    artnet::NodeInfo info;
    CHECK (artnet::parsePollReply (reply, sizeof (reply), info));
    CHECK (info.ip == "2.0.0.10");
    CHECK (info.shortName == "Node A");
    CHECK (info.longName == "Test Node Long Name");
    CHECK_EQ (info.numPorts, 4);
    CHECK (! artnet::parsePollReply (poll.getData(), (int) poll.getSize(), info));
}

static void testPatch()
{
    section ("Patch: conflicts and ranges");
    FixtureLibrary lib;
    Patch patch;

    CHECK (patch.add ({ 1, "Dim", "builtin.dimmer", 1, 1 }, lib).isEmpty());
    CHECK (patch.add ({ 1, "Dup", "builtin.dimmer", 1, 2 }, lib).isNotEmpty());   // FID used
    CHECK (patch.add ({ 2, "Ovl", "builtin.rgb", 1, 1 }, lib).isNotEmpty());      // overlap
    CHECK (patch.add ({ 2, "Edge", "builtin.rgb", 1, 511 }, lib).isNotEmpty());   // won't fit
    CHECK (patch.add ({ 2, "Ok", "builtin.rgb", 1, 2 }, lib).isEmpty());
    CHECK_EQ (patch.nextFreeAddress (1, lib), 5);

    // 40 x 14ch spots from 1.500 should wrap into the next universes
    CHECK (patch.addRange ("builtin.spot16", 40, 101, 1, 500, "Spot", lib).isEmpty());
    auto* first = patch.find (101);
    CHECK (first != nullptr && first->universe == 2 && first->address == 1);
    auto* last = patch.find (140);
    CHECK (last != nullptr && last->universe == 3);

    // A failing range adds nothing
    const auto before = patch.fixtures().size();
    CHECK (patch.addRange ("builtin.dimmer", 5, 140, 9, 1, "X", lib).isNotEmpty());
    CHECK_EQ (patch.fixtures().size(), before);

    auto u = patch.usedUniverses();
    CHECK_EQ ((int) u.size(), 3);
}

static UniverseMap render (const FixtureLibrary& lib, const Patch& patch, const Programmer& prog,
                           const Playback& pb, const std::map<int, double>& phases,
                           double now, float gm, float size = 1.0f)
{
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, now, gm, size };
    DmxEngine::renderFrame (ctx, out);
    return out;
}

static void testRender()
{
    section ("Render: defaults, 16-bit, grand master, virtual dimmer");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    std::map<int, double> phases;
    patch.add ({ 1, "Spot", "builtin.spot16", 1, 1 }, lib);
    patch.add ({ 2, "RGB",  "builtin.rgb",    1, 101 }, lib);
    patch.add ({ 3, "Far",  "builtin.dimmer", 1000, 512 }, lib);

    auto out = render (lib, patch, prog, pb, phases, 0, 1.0f);
    auto& u1 = out[1];
    CHECK_EQ (u1[0], 128);   // pan default
    CHECK_EQ (u1[5], 0);     // dimmer default
    CHECK_EQ (u1[6], 255);   // shutter default open
    CHECK_EQ (u1[100], 0);   // RGB with virtual dimmer at 0 → black
    CHECK (out.count (1000) == 1);

    prog.set (1, "Pan", 0.5f);
    prog.set (1, "Dimmer", 1.0f);
    prog.set (2, "Dimmer", 1.0f);
    prog.set (2, "ColorG", 0.0f);
    out = render (lib, patch, prog, pb, phases, 0, 0.5f);
    auto& v = out[1];
    CHECK_EQ (v[0], 0x80);   CHECK_EQ (v[1], 0x00);    // 16-bit pan = 32768
    CHECK_EQ (v[5], 128);                              // dimmer x GM 50%
    CHECK_EQ (v[100], 128);                            // red default 255 x virtual dim x GM
    CHECK_EQ (v[101], 0);
    CHECK_EQ (v[102], 128);

    out = render (lib, patch, prog, pb, phases, 0, 0.0f);  // blackout
    CHECK_EQ (out[1][5], 0);
    CHECK_EQ (out[1][100], 0);
    CHECK_EQ (out[1][0], 0x80);                        // position unaffected by GM
}

static void testFixtureBuilder()
{
    section ("Fixture builder: channel numbers, validation, auto-number, v1 import");
    FixtureType t;
    t.name = "Custom";
    t.attributes = { { "Pan", 1, 3, 0 }, { "Tilt", 2, 4, 0 }, { "Dimmer", 5, 0, 0 } };
    CHECK (t.validate().isEmpty());
    CHECK_EQ (t.footprint(), 5);
    auto slots = t.attributeSlots();
    CHECK_EQ ((int) slots.size(), 3);
    CHECK (slots[0].attribute == "Pan" && slots[0].coarse == 0 && slots[0].fine == 2);
    auto map = t.channelMap();
    CHECK (map[2] == "Pan fine");
    CHECK (map[4] == "Dim");

    auto bad = t;
    bad.attributes[2].coarse = 3;                      // collides with Pan fine
    CHECK (bad.validate().isNotEmpty());

    t.attributes = { { "Dimmer", 9, 0, 0 }, { "Pan", 1, 7, 0 } };
    t.autoNumber();
    CHECK (t.attributes[0].coarse == 1 && t.attributes[0].fine == 0);
    CHECK (t.attributes[1].coarse == 2 && t.attributes[1].fine == 3);

    // v0.1 show files stored one entry per channel
    auto v1 = juce::JSON::parse (R"({"id":"user.x","name":"Old","channels":[
        {"attribute":"Pan","fine":false,"default":128},{"attribute":"Tilt","fine":false,"default":128},
        {"attribute":"Pan","fine":true,"default":0},{"attribute":"Tilt","fine":true,"default":0},
        {"attribute":"Dimmer","fine":false,"default":0},{"attribute":"None","fine":false,"default":0}]})");
    auto old = FixtureType::fromVar (v1);
    CHECK_EQ ((int) old.attributes.size(), 3);
    CHECK (old.attributes[0].coarse == 1 && old.attributes[0].fine == 3);
    CHECK_EQ (old.attributes[0].defaultValue, 128);
    CHECK_EQ (old.footprint(), 6);

    FixtureLibrary lib;
    auto dupId = lib.duplicate ("builtin.rgb");
    CHECK (lib.findMutable (dupId) != nullptr);
    CHECK (lib.findMutable ("builtin.rgb") == nullptr);
    auto* spot = lib.find ("builtin.spot16");
    CHECK (spot != nullptr && spot->footprint() == 14);
}

static void testEffectPhases()
{
    section ("Effects: selection-order phase, mirror, blocks, groups");
    EffectParams e;
    e.fixtures = { 1, 2, 3, 4, 5, 6, 7, 8 };
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.001f; };

    CHECK (near (e.phaseOffsetFor (0), 0.0f));
    CHECK (near (e.phaseOffsetFor (4), 0.5f));
    CHECK (near (e.phaseOffsetFor (7), 0.875f));

    e.mirror = true;                                   // symmetric from the outside in
    CHECK (near (e.phaseOffsetFor (0), e.phaseOffsetFor (7)));
    CHECK (near (e.phaseOffsetFor (3), e.phaseOffsetFor (4)));
    CHECK (near (e.phaseOffsetFor (1), 0.25f));
    e.mirror = false;

    e.blocks = 2;                                      // pairs share a phase
    CHECK (near (e.phaseOffsetFor (0), e.phaseOffsetFor (1)));
    CHECK (near (e.phaseOffsetFor (2), 0.25f));
    e.blocks = 0;

    e.groups = 2;                                      // alternating
    CHECK (near (e.phaseOffsetFor (0), e.phaseOffsetFor (2)));
    CHECK (near (e.phaseOffsetFor (1), 0.5f));
    e.groups = 0;

    e.phaseStart = 0; e.phaseEnd = 0;                  // all in sync
    CHECK (near (e.phaseOffsetFor (5), 0.0f));
}

static void testEffectsRender()
{
    section ("Effects: dimmer and movement output, size and speed masters");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    patch.add ({ 1, "A", "builtin.spot16", 1, 1 }, lib);
    patch.add ({ 2, "B", "builtin.spot16", 1, 15 }, lib);

    EffectParams dim;
    dim.id = 10; dim.family = Family::Dimmer; dim.form = "Sine";
    dim.fixtures = { 1, 2 };                           // phases 0 and 180
    prog.setEffect (dim);

    std::map<int, double> phases { { 10, 0.0 } };
    auto out = render (lib, patch, prog, pb, phases, 0, 1.0f);
    CHECK_EQ (out[1][5], 0);                           // A at low
    CHECK_EQ (out[1][14 + 5], 255);                    // B half a cycle ahead → high

    out = render (lib, patch, prog, pb, phases, 0, 1.0f, 0.5f);
    CHECK_EQ (out[1][14 + 5], 255);                    // the size master leaves dimmer effects alone

    EffectParams mov;
    mov.id = 11; mov.kind = EffectKind::Movement; mov.updateFamily(); mov.form = "Circle"; mov.size = 0.2f;
    mov.fixtures = { 1 };
    prog.setEffect (mov);
    prog.set (1, "Tilt", 0.5f);
    phases[11] = 0.0;
    out = render (lib, patch, prog, pb, phases, 0, 1.0f);
    const int tilt = (out[1][2] << 8) | out[1][3];
    CHECK (std::abs (tilt - juce::roundToInt (0.6f * 65535.0f)) <= 1);   // 0.5 + size/2
    CHECK_EQ (out[1][0], 128);                         // pan: default + sin(0)

    Masters m;
    std::map<int, double> ph;
    DmxEngine::advanceEffects (prog, pb, m, 1.0, ph);  // 60 bpm, 1 s → 1 cycle
    CHECK (std::abs (ph[10] - 1.0) < 1e-6);
    m.dimmerSpeed = 1.0f;                              // 400 %
    m.movementSpeed = 0.0f;                            // stopped
    DmxEngine::advanceEffects (prog, pb, m, 1.0, ph);
    CHECK (std::abs (ph[10] - 5.0) < 1e-6);
    CHECK (std::abs (ph[11] - 1.0) < 1e-6);
}

static void testColourAndAttributeEffects()
{
    section ("Colour effects, attribute effects, effect slots");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    patch.add ({ 1, "A", "builtin.rgbw8", 1, 1 }, lib);
    patch.add ({ 2, "B", "builtin.rgbw8", 1, 9 }, lib);
    patch.add ({ 3, "S", "builtin.spot16", 1, 20 }, lib);

    EffectParams col;
    col.id = 21; col.kind = EffectKind::Color; col.updateFamily(); col.form = "A-B Fade";
    col.colourA = 0xffff0000u; col.colourB = 0xff0000ffu; col.fixtures = { 1, 2 };   // phases 0 and 180
    prog.setEffect (col);

    EffectParams zoom;
    zoom.id = 22; zoom.kind = EffectKind::Attribute; zoom.attribute = "Zoom"; zoom.updateFamily();
    zoom.form = "Sine"; zoom.low = 0.2f; zoom.high = 0.8f; zoom.fixtures = { 3 };
    prog.setEffect (zoom);

    EffectParams focus = zoom;
    focus.id = 23; focus.attribute = "Focus";
    prog.setEffect (focus);
    CHECK_EQ ((int) prog.effects().size(), 3);                     // zoom and focus coexist
    auto zoom2 = zoom; zoom2.high = 0.6f;
    prog.setEffect (zoom2);
    CHECK_EQ ((int) prog.effects().size(), 3);                     // same attribute replaces
    prog.setEffect (zoom);

    std::map<int, double> phases { { 21, 0.0 }, { 22, 0.0 }, { 23, 0.5 } };
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][1], 255); CHECK_EQ (out[1][3], 0);            // fixture 1 at A (red)
    CHECK_EQ (out[1][9], 0);   CHECK_EQ (out[1][11], 255);         // fixture 2 half a cycle on: B (blue)
    // spot 16-bit: Focus is channel 12, Zoom channel 13 (patched at 1.020)
    CHECK_EQ (out[1][19 + 12], juce::roundToInt (0.2f * 255.0f));  // zoom sine at low
    CHECK_EQ (out[1][19 + 11], juce::roundToInt (0.8f * 255.0f));  // focus half a cycle on: high

    // white extraction for RGBW: pure white goes to W
    EffectParams white = col;
    white.colourA = 0xffffffffu; white.colourB = 0xffffffffu;
    prog.setEffect (white);
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][4], 255); CHECK_EQ (out[1][1], 0);

    // the size master leaves colour effects alone: same output at size 0 and 1
    EffectParams rb = col; rb.form = "Rainbow"; rb.colourA = 0xff00ff00u;
    prog.setEffect (rb);
    out.clear();
    RenderContext ctx0 { lib, patch, prog, pb, phases, 0.0, 1.0f, 0.0f };
    DmxEngine::renderFrame (ctx0, out);
    UniverseMap full;
    RenderContext ctx1 { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx1, full);
    CHECK (out[1] == full[1]);

    auto rt = EffectParams::fromVar (juce::JSON::parse (juce::JSON::toString (zoom.toVar())));
    CHECK (rt.kind == EffectKind::Attribute && rt.attribute == "Zoom" && rt.family == Family::Focus);
    auto rt2 = EffectParams::fromVar (juce::JSON::parse (juce::JSON::toString (col.toVar())));
    CHECK (rt2.kind == EffectKind::Color && rt2.colourB == 0xff0000ffu);
    auto old = EffectParams::fromVar (juce::JSON::parse (R"({"id":5,"family":"Position","form":"Circle"})"));
    CHECK (old.kind == EffectKind::Movement);
}

static void testCells()
{
    section ("Multi-cell fixtures");
    FixtureLibrary lib;
    auto* bar = lib.find ("builtin.bar8");
    CHECK (bar != nullptr && bar->cellCount() == 8 && bar->footprint() == 25);   // dimmer + 8 x RGB
    CHECK (bar != nullptr && bar->validate().isEmpty());
    auto slots = bar->attributeSlots();
    // 8 virtual pixel dimmers, then the 25 real channels
    CHECK (slots.size() == 33 && slots[0].attribute == "Dimmer@1" && slots[0].coarse < 0 && slots[0].defaultNormalised == 1.0f);
    CHECK (slots[8].attribute == "Dimmer" && slots[9].attribute == "ColorR@1" && slots[32].attribute == "ColorB@8");
    CHECK (familyOf ("ColorG@5") == Family::Color && baseAttribute ("ColorG@5") == "ColorG" && cellOf ("ColorG@5") == 5);
    CHECK (attributeLabel ("ColorR@3") == "Red 3");

    FixtureType t;
    t.cellsX = 3;
    t.attributes = { { "Dimmer", 1, 0, 0, 0 }, { "ColorR", 2, 0, 0, 1 }, { "ColorG", 3, 0, 0, 1 } };
    t.repeatCellOne();
    CHECK_EQ ((int) t.attributes.size(), 7);
    CHECK (t.attributes.back().key() == "ColorG@3" && t.attributes.back().coarse == 7);
    auto bad = t; bad.attributes.push_back ({ "ColorB", 9, 0, 0, 4 });
    CHECK (bad.validate().isNotEmpty());                                  // only 3 cells

    auto rt = FixtureType::fromVar (juce::JSON::parse (juce::JSON::toString (t.toVar())));
    CHECK (rt.cellsX == 3 && rt.attributes.back().cell == 3);

    // a colour effect lands on every cell; programmer per-cell values override
    Patch patch;
    Programmer prog;
    Playback pb;
    patch.add ({ 1, "Bar", "builtin.bar8", 1, 1 }, lib);
    EffectParams col; col.id = 31; col.kind = EffectKind::Color; col.updateFamily(); col.form = "A-B Fade";
    col.colourA = 0xff00ff00u; col.fixtures = { 1 };
    prog.setEffect (col);
    prog.set (1, "Dimmer", 1.0f);
    prog.set (1, "ColorB@8", 1.0f);
    std::map<int, double> phases { { 31, 0.0 } };
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 255);                                            // master dimmer
    CHECK_EQ (out[1][1 + 1], 255); CHECK_EQ (out[1][1 + 0], 0);           // cell 1 green
    CHECK_EQ (out[1][1 + 21 + 1], 255);                                   // cell 8 green
    CHECK_EQ (out[1][1 + 21 + 2], 0);                                     // the effect overrides static values in the same layer
}

static void testPixelsAndForms()
{
    section ("Pixel selection, pixel dimmers, orientation, colour forms, effect maps");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    std::map<int, double> phases;
    patch.add ({ 1, "Panel", "builtin.panel4x4", 1, 1 }, lib);        // 16 x RGB, no dimmer channel
    auto* panel = lib.find ("builtin.panel4x4");
    CHECK (panel->usesVirtualDimmer() && panel->cellUsesVirtualDimmer (1) && panel->cellUsesVirtualDimmer (16));

    CHECK_EQ (cellTarget (301, 5), -301005);
    CHECK (targetFid (-301005) == 301 && targetCell (-301005) == 5 && targetName (-301005) == "301.5");
    CHECK (patch.find (cellTarget (1, 3)) != nullptr);

    // programming one pixel: its dimmer at 50 %, the fixture opens for it
    prog.setSelection ({ cellTarget (1, 2) });
    prog.set (cellTarget (1, 2), "Dimmer@2", 0.5f);
    CHECK (prog.get (1, "Dimmer@2").has_value());
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][3], 128);                   // pixel 2 red at half
    CHECK_EQ (out[1][0], 255);                   // pixel 1 at full (its dimmer defaults to full)

    // a dimmer chase across two pixels, and a colour on one pixel only
    prog.clearValues();
    prog.set (1, "Dimmer", 1.0f);
    EffectParams sq; sq.id = 61; sq.kind = EffectKind::Dimmer; sq.updateFamily(); sq.form = "Square"; sq.width = 0.5f;
    sq.fixtures = { cellTarget (1, 1), cellTarget (1, 2) };      // phase 0 and 180
    prog.setEffect (sq);
    EffectParams col; col.id = 62; col.kind = EffectKind::Color; col.updateFamily(); col.form = "A-B Chase";
    col.colourA = 0xff0000ffu; col.colourB = 0xff0000ffu; col.fixtures = { cellTarget (1, 3) };
    prog.setEffect (col);
    phases = { { 61, 0.25 }, { 62, 0.0 } };
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 255);                   // pixel 1 on (first half of the square)
    CHECK_EQ (out[1][3], 0);                     // pixel 2 off (180 degrees later)
    CHECK (out[1][6] == 0 && out[1][8] == 255);  // pixel 3 blue
    CHECK (out[1][9] == 255 && out[1][11] == 255);   // pixel 4 untouched: white at full

    // rotation and mirror of the cell grid
    PatchedFixture bar { 2, "Bar", "builtin.bar8", 1, 100 };
    bar.layoutX = 100; bar.layoutY = 100;
    auto* bt = lib.find ("builtin.bar8");
    auto p1 = cellLayoutPosition (bar, *bt, 1);
    CHECK (std::abs (p1.first - 44.0f) < 0.01f && std::abs (p1.second - 100.0f) < 0.01f);
    bar.layoutRotation = 90.0f;
    p1 = cellLayoutPosition (bar, *bt, 1);
    CHECK (std::abs (p1.first - 100.0f) < 0.01f && std::abs (p1.second - 44.0f) < 0.01f);
    bar.layoutRotation = 0.0f; bar.layoutMirror = true;
    p1 = cellLayoutPosition (bar, *bt, 1);
    CHECK (std::abs (p1.first - 156.0f) < 0.01f);
    patch.add (bar, lib);
    patch.setLayoutOrientation (2, -90.0f, true);
    auto rt = Patch(); rt.fromVar (patch.toVar());
    CHECK (rt.find (2) != nullptr && std::abs (rt.find (2)->layoutRotation - 270.0f) < 0.01f && rt.find (2)->layoutMirror);

    // new colour forms
    EffectParams f; f.kind = EffectKind::Color; f.colourA = 0xffff0000u; f.colourB = 0xff0000ffu; f.width = 0.5f;
    f.form = "A-Black-B-Black";
    CHECK (effects::colourAtPhase (f, 0.1, 0, 1).r == 1.0f && effects::colourAtPhase (f, 0.35, 0, 1).r == 0.0f
           && effects::colourAtPhase (f, 0.35, 0, 1).b == 0.0f && effects::colourAtPhase (f, 0.6, 0, 1).b == 1.0f);
    f.form = "A-B-Black";
    CHECK (effects::colourAtPhase (f, 0.5, 0, 1).b == 1.0f && effects::colourAtPhase (f, 0.9, 0, 1).r == 0.0f);
    f.form = "Pulse A-B";
    CHECK (effects::colourAtPhase (f, 0.125, 0, 1).r > 0.99f && effects::colourAtPhase (f, 0.375, 0, 1).r < 0.01f
           && effects::colourAtPhase (f, 0.625, 0, 1).b > 0.99f);
    f.form = "Pulse A";
    CHECK (effects::colourAtPhase (f, 0.25, 0, 1).r > 0.99f && effects::colourAtPhase (f, 0.75, 0, 1).r < 0.01f);
    CHECK (effects::colourForms().contains ("Random Hue"));

    // pixel map with the effect generator: a chase travelling left to right
    PixelMap m;
    m.source = pixelmap::effectSource();
    m.effect.form = "A-B Chase"; m.effect.colourA = 0xffff0000u; m.effect.colourB = 0xff0000ffu; m.effect.width = 0.5f;
    RGBf c;
    CHECK (pixelmap::sampleMap (m, nullptr, 0.25f, 0.5f, 0.0, c, 0.0, 1.0f) && c.b == 1.0f);   // behind the front: B
    CHECK (pixelmap::sampleMap (m, nullptr, 0.75f, 0.5f, 0.0, c, 0.0, 1.0f) && c.r == 1.0f);
    m.direction = "Right to left";
    CHECK (pixelmap::sampleMap (m, nullptr, 0.25f, 0.5f, 0.0, c, 0.0, 1.0f) && c.r == 1.0f);
    auto mrt = PixelMap::fromVar (juce::JSON::parse (juce::JSON::toString (m.toVar())));
    CHECK (mrt.source == "Effect" && mrt.direction == "Right to left" && mrt.effect.form == "A-B Chase");
}

static void testChaseAndButtons()
{
    section ("Cue chase, Swop, Temp, Toggle, speed fader, executor buttons");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    patch.add ({ 1, "D1", "builtin.dimmer", 1, 1 }, lib);
    patch.add ({ 2, "D2", "builtin.dimmer", 1, 2 }, lib);

    ValueMap c1 { { 1, { { "Dimmer", 1.0f } } } }, c2 { { 2, { { "Dimmer", 1.0f } } } };
    pb.store (1, 1, c1, {}, StoreMode::Add, 0.0);
    pb.store (1, 1, c2, {}, StoreMode::Add, 0.0);
    pb.store (1, 1, c1, {}, StoreMode::Add, 0.0);
    pb.setChase (1, 1, true, 1.0f, 0.0f, ChaseMode::Loop);
    pb.go (1, 1, 10.0);
    pb.update (10.0, 2.0f);                        // 2 beats per second, 1 step per beat
    CHECK_EQ (pb.state (1, 1).cue, 0);
    pb.update (10.3, 2.0f);
    CHECK_EQ (pb.state (1, 1).cue, 0);
    pb.update (10.55, 2.0f);
    CHECK_EQ (pb.state (1, 1).cue, 1);             // stepped after half a second
    pb.update (11.05, 2.0f);
    pb.update (11.55, 2.0f);
    CHECK_EQ (pb.state (1, 1).cue, 0);             // looped round
    pb.togglePause (1, 1);
    pb.update (12.5, 2.0f);
    CHECK_EQ (pb.state (1, 1).cue, 0);             // paused
    pb.togglePause (1, 1);

    pb.setChase (1, 1, true, 1.0f, 0.0f, ChaseMode::Bounce);
    pb.gotoCue (1, 1, 2, 20.0);
    pb.update (20.0, 1.0f); pb.update (21.01, 1.0f);
    CHECK_EQ (pb.state (1, 1).cue, 1);             // bounced back from the last step

    // speed fader: doesn't start or stop, just sets the rate (0 = stopped)
    pb.setFaderFunction (1, 1, FaderFunction::Speed);
    pb.setFader (1, 1, 0.0f, 22.0);
    CHECK (pb.state (1, 1).isOn());
    pb.update (22.0, 1.0f); pb.update (24.0, 1.0f);
    CHECK_EQ (pb.state (1, 1).cue, 1);
    CHECK (pb.faderPosition (1, 1) == 0.0f && pb.state (1, 1).fader == 1.0f);
    pb.off (1, 1);

    // round trip
    pb.setButtonFunction (1, 1, 3, ButtonFunction::Swop);
    Playback rt; rt.fromVar (pb.toVar());
    auto* e = rt.find (1, 1);
    CHECK (e != nullptr && e->chase && e->chaseMode == ChaseMode::Bounce && e->faderFunction == FaderFunction::Speed
           && e->buttons[3] == ButtonFunction::Swop && e->buttons[0] == ButtonFunction::Go);

    // Swop: held executor at full, everything else's intensity out
    Playback p2;
    p2.store (1, 1, c1, {}, StoreMode::Add, 0.0);
    p2.store (1, 2, c2, {}, StoreMode::Add, 0.0);
    p2.setButtonFunction (1, 2, 3, ButtonFunction::Swop);
    p2.setFader (1, 2, 0.0f, 0.0);
    p2.go (1, 1, 0.0);
    std::map<int, double> phases;
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, p2, phases, 1.0, 1.0f, 1.0f };
    p2.button (1, 2, 3, true, 1.0);
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][0] == 0 && out[1][1] == 255);
    p2.button (1, 2, 3, false, 1.0);
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][0] == 255 && out[1][1] == 0 && ! p2.state (1, 2).isOn());

    // Temp: on while held at fader level; Toggle
    p2.setButtonFunction (1, 2, 0, ButtonFunction::Temp);
    p2.setFader (1, 2, 0.5f, 2.0);                  // auto start
    p2.off (1, 2);
    p2.button (1, 2, 0, true, 2.0);
    CHECK (p2.state (1, 2).isOn() && p2.state (1, 2).level() == 0.5f);
    p2.button (1, 2, 0, false, 2.1);
    CHECK (! p2.state (1, 2).isOn());
    p2.toggle (1, 2, 3.0);
    CHECK (p2.state (1, 2).isOn());
    p2.toggle (1, 2, 3.1);
    CHECK (! p2.state (1, 2).isOn());

    // groups from folders: same name updates the same group
    PresetLibrary presets;
    const int g1 = presets.setGroup ("Wash", { 3, 1 });
    const int g2 = presets.setGroup ("Spots", { 5 });
    CHECK (g1 == 1 && g2 == 2 && presets.setGroup ("Wash", { 1, 2 }) == 1);
    CHECK (presets.find (PresetPool::Group, 1)->fixtures == std::vector<int> ({ 1, 2 }));

    // Select through the engine
    DmxEngine engine;
    engine.playback.store (1, 3, c2, {}, StoreMode::Add, 0.0);
    engine.playback.setButtonFunction (1, 3, 1, ButtonFunction::Select);
    engine.executorButton (1, 3, 1, true);
    CHECK (engine.programmer.selection() == std::vector<int> { 2 });
}

static void testGdtf()
{
    section ("GDTF import");
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<GDTF DataVersion="1.1">
  <FixtureType Name="Bar" LongName="Test Bar" Manufacturer="Acme" FixtureTypeID="1234-ABCD">
    <Geometries>
      <Geometry Name="Body">
        <GeometryReference Name="Pixel 1" Geometry="Pixel"><Break DMXBreak="1" DMXOffset="4"/></GeometryReference>
        <GeometryReference Name="Pixel 2" Geometry="Pixel"><Break DMXBreak="1" DMXOffset="7"/></GeometryReference>
        <GeometryReference Name="Pixel 3" Geometry="Pixel"><Break DMXBreak="1" DMXOffset="10"/></GeometryReference>
      </Geometry>
      <Geometry Name="Pixel"/>
    </Geometries>
    <DMXModes>
      <DMXMode Name="Standard" Geometry="Body">
        <DMXChannels>
          <DMXChannel DMXBreak="1" Offset="1,2" Geometry="Body">
            <LogicalChannel Attribute="Dimmer"><ChannelFunction Attribute="Dimmer" Default="65535/2"/></LogicalChannel>
          </DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="Body">
            <LogicalChannel Attribute="Shutter1"><ChannelFunction Attribute="Shutter1" Default="255/1"/></LogicalChannel>
          </DMXChannel>
          <DMXChannel DMXBreak="1" Offset="1" Geometry="Pixel"><LogicalChannel Attribute="ColorAdd_R"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="2" Geometry="Pixel"><LogicalChannel Attribute="ColorAdd_G"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="Pixel"><LogicalChannel Attribute="ColorAdd_B"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="" Geometry="Body"><LogicalChannel Attribute="Pan"/></DMXChannel>
        </DMXChannels>
      </DMXMode>
      <DMXMode Name="Simple" Geometry="Body">
        <DMXChannels>
          <DMXChannel DMXBreak="1" Offset="1" Geometry="Body" Default="128/1"><LogicalChannel Attribute="Dimmer"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="2" Geometry="Body"><LogicalChannel Attribute="SomethingOdd"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="Body"><LogicalChannel Attribute="AlsoOdd"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="4,5,6" Geometry="Body"><LogicalChannel Attribute="Zoom"/></DMXChannel>
        </DMXChannels>
      </DMXMode>
    </DMXModes>
  </FixtureType>
</GDTF>)";

    // packed like a real .gdtf (zip)
    juce::MemoryOutputStream zipped;
    {
        juce::ZipFile::Builder b;
        auto text = std::make_unique<juce::MemoryInputStream> (xml, std::strlen (xml), true);
        b.addEntry (text.release(), 5, "description.xml", juce::Time::getCurrentTime());
        b.writeToStream (zipped, nullptr);
    }
    gdtf::Description d;
    CHECK (gdtf::read (zipped.getMemoryBlock(), d).isEmpty());
    CHECK (d.manufacturer == "Acme" && d.name == "Test Bar" && d.modes.size() == 2);
    CHECK (d.modes[0].footprint == 12 && d.modes[0].cells == 3);

    FixtureType t;
    CHECK (gdtf::toFixtureType (d, "Standard", t).isEmpty());
    CHECK (t.cellsX == 3 && t.footprint() == 12 && t.name == "Test Bar (Standard)");
    CHECK (t.attributes[0].attribute == "Dimmer" && t.attributes[0].fine == 2 && t.attributes[0].defaultValue == 255);
    CHECK (t.attributes[1].attribute == "Shutter" && t.attributes[1].defaultValue == 255);
    bool sawCell3Blue = false;
    for (auto& a : t.attributes) sawCell3Blue |= a.key() == "ColorB@3" && a.coarse == 12;
    CHECK (sawCell3Blue);

    CHECK (gdtf::toFixtureType (d, "Simple", t).isEmpty());
    CHECK (t.footprint() == 6 && t.attributes[0].defaultValue == 128);
    CHECK (t.attributes[1].attribute == "Raw" && t.attributes.size() == 3);     // second unknown becomes a spare
    CHECK (gdtf::mapAttribute ("ColorAdd_WW") == "ColorW" && gdtf::mapAttribute ("Gobo1PosRotate") == "Gobo1Rot");

    // optional: every .gdtf in $PHASE_GDTF_DIR
    const auto dir = juce::SystemStats::getEnvironmentVariable ("PHASE_GDTF_DIR", {});
    if (dir.isNotEmpty())
        for (auto& f : juce::File (dir).findChildFiles (juce::File::findFiles, true, "*.gdtf"))
        {
            juce::MemoryBlock mb;
            f.loadFileAsData (mb);
            gdtf::Description real;
            const auto err = gdtf::read (mb, real);
            CHECK (err.isEmpty());
            std::cout << "    " << f.getFileName() << ": " << real.manufacturer << " " << real.name << err << "\n";
            for (auto& m : real.modes)
            {
                FixtureType rt;
                const auto e2 = gdtf::toFixtureType (real, m.name, rt);
                std::cout << "      - " << m.name << "  " << rt.footprint() << " ch, " << (int) rt.attributes.size()
                          << " attributes, cells " << rt.cellCount() << "  " << e2 << "\n";
                CHECK (e2.isEmpty());
            }
        }
}

static void testMovementAndFly()
{
    section ("Movement mirror, shapes, fly effects");
    EffectParams m; m.kind = EffectKind::Movement; m.updateFamily(); m.form = "Pan"; m.size = 0.4f; m.mirror = true;
    m.fixtures = { 1, 2, 3, 4 };
    // mirrored: outer fixtures share the phase and pan opposite ways
    const auto a = effects::movementOffset (m, 0.1, 0, 1.0f), d = effects::movementOffset (m, 0.1, 3, 1.0f);
    CHECK (std::abs (a.x + d.x) < 0.0001f && std::abs (a.x) > 0.01f && std::abs (a.y - d.y) < 0.0001f);
    CHECK (! m.mirroredSide (1) && m.mirroredSide (2));
    m.mirror = false;
    m.form = "Circle"; m.tiltSize = 0.0f;
    CHECK (std::abs (effects::movementOffset (m, 0.0, 0, 1.0f).y) < 0.0001f);      // tilt size 0: pan only
    m.form = "Square"; m.tiltSize = -1.0f; m.size = 1.0f; m.phaseEnd = 0.0f;
    const auto sq = effects::movementOffset (m, 0.0, 0, 1.0f);
    CHECK (std::abs (sq.x + 0.5f) < 0.001f && std::abs (sq.y + 0.5f) < 0.001f);     // first corner
    m.reverse = true;
    CHECK (std::abs (effects::movementOffset (m, 0.125, 0, 1.0f).x + 0.5f) < 0.001f); // runs the other way (left edge)
    auto rt = EffectParams::fromVar (juce::JSON::parse (juce::JSON::toString (m.toVar())));
    CHECK (rt.reverse && rt.form == "Square");

    EffectParams f; f.kind = EffectKind::Fly; f.updateFamily(); f.form = "Fly Out"; f.width = 0.5f;
    f.size = 0.6f; f.tiltSize = 0.4f; f.phaseEnd = 0.0f; f.fixtures = { 1, 2, 3 };
    CHECK (f.family == Family::Dimmer);
    auto mid = effects::flyValue (f, 0.25, 0, 1.0f);       // halfway through the lit part
    CHECK (mid.dimmer > 0.99f && mid.offset.x < -0.1f && mid.offset.y > 0.05f);      // up = more tilt
    CHECK (effects::flyValue (f, 0.25, 2, 1.0f).offset.x > 0.1f);
    CHECK (std::abs (effects::flyValue (f, 0.25, 1, 1.0f).offset.x) < 0.001f);   // centre fixture goes straight up
    CHECK (effects::flyValue (f, 0.75, 0, 1.0f).dimmer < 0.001f);               // dark between flights

    // through the renderer: pan, tilt and dimmer together
    FixtureLibrary lib; Patch patch; Programmer prog; Playback pb;
    patch.add ({ 1, "Spot", "builtin.spot16", 1, 1 }, lib);
    f.fixtures = { 1 };
    f.id = 77;
    prog.setEffect (f);
    std::map<int, double> phases { { 77, 0.25 } };
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    auto* spot = lib.find ("builtin.spot16");
    int dimCh = -1, tiltCh = -1;
    for (auto& sl : spot->attributeSlots())
    {
        if (sl.attribute == "Dimmer") dimCh = sl.coarse;
        if (sl.attribute == "Tilt") tiltCh = sl.coarse;
    }
    CHECK (dimCh >= 0 && out[1][(size_t) dimCh] == 255);
    CHECK (tiltCh >= 0 && out[1][(size_t) tiltCh] > 128);    // flew up from the centre (more tilt)

    // inverted tilt: the same flight goes the other way on the wire
    patch.setInvert (1, false, true);
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][(size_t) tiltCh] < 128);
    Patch prt; prt.fromVar (patch.toVar());
    CHECK (prt.find (1)->invertTilt && ! prt.find (1)->invertPan);
}

static void testUndo()
{
    section ("Undo / redo");
    DmxEngine engine;
    UndoHistory history (engine);
    history.reset();
    CHECK (! history.canUndo());

    { const juce::ScopedLock sl (engine.dataLock); engine.patch.add ({ 1, "Dim", "builtin.dimmer", 1, 1 }, engine.library); }
    CHECK (history.capture());
    { const juce::ScopedLock sl (engine.dataLock); engine.programmer.select (1, false); engine.programmer.set (1, "Dimmer", 0.7f); }
    history.capture();
    CHECK (! history.capture());                          // nothing changed: no step
    { const juce::ScopedLock sl (engine.dataLock);
      engine.playback.store (1, 1, engine.programmer.allValues(), {}, StoreMode::Add, 0.0);
      engine.playback.go (1, 1, 0.0); }
    history.capture();
    { const juce::ScopedLock sl (engine.dataLock); engine.playback.setFader (1, 1, 0.4f, 0.0); }
    CHECK (! history.capture());                          // fader moves are not undo steps
    CHECK_EQ (history.undoSteps(), 3);

    CHECK (history.undo());                               // the store
    CHECK (engine.playback.find (1, 1) == nullptr && ! engine.playback.state (1, 1).isOn());
    CHECK (history.undo());                               // the programmer value
    CHECK (! engine.programmer.get (1, "Dimmer").has_value() && engine.programmer.selection().empty());
    CHECK (history.undo());                               // the patch
    CHECK (engine.patch.fixtures().empty());
    CHECK (! history.undo());
    CHECK (history.redo() && history.redo() && history.redo());
    CHECK (engine.patch.fixtures().size() == 1 && engine.playback.find (1, 1) != nullptr);
    CHECK (engine.programmer.get (1, "Dimmer").value_or (0.0f) > 0.69f);
    CHECK (! history.redo());

    // a running executor keeps running when an edit to it is undone
    { const juce::ScopedLock sl (engine.dataLock); engine.playback.go (1, 1, 1.0); }
    { const juce::ScopedLock sl (engine.dataLock); engine.playback.renameExecutor (1, 1, "Renamed"); }
    history.capture();
    history.undo();
    CHECK (engine.playback.find (1, 1)->name != "Renamed" && engine.playback.state (1, 1).isOn());
    // a new edit after undo drops the redo branch
    { const juce::ScopedLock sl (engine.dataLock); engine.programmer.set (1, "Dimmer", 0.1f); }
    history.capture();
    CHECK (! history.canRedo());
}

static void testTemplatesAndLibrary()
{
    section ("Fixture templates, defaults, library import / export");
    CHECK (suggestedDefault ("Pan") == 128 && suggestedDefault ("Tilt@2") == 128 && suggestedDefault ("Shutter") == 255
           && suggestedDefault ("ColorR") == 0);
    for (auto& info : templates::all())
    {
        auto t = templates::make (info.id, 8, 2);
        CHECK (t.validate().isEmpty() && ! t.attributes.empty() && t.id.startsWith ("user."));
    }
    auto spot = templates::make ("spot");
    CHECK (spot.attributes[0].attribute == "Pan" && spot.attributes[0].fine == 2 && spot.attributes[0].defaultValue == 128);
    auto panel = templates::make ("panel", 6, 3);
    CHECK (panel.cellCount() == 18 && panel.footprint() == 54);
    auto bar = templates::make ("bar", 12, 1);
    CHECK (bar.cellCount() == 12 && bar.hasRealDimmer() && bar.footprint() == 37);

    // custom pixel layouts: a ring
    auto ring = templates::make ("ring", 12);
    CHECK (ring.hasCustomLayout() && ring.cellCount() == 12 && ring.validate().isEmpty());
    auto top = ring.cellOffset (1), right = ring.cellOffset (4);
    CHECK (std::abs (top.first) < 0.01f && top.second < -10.0f && right.first > 10.0f && std::abs (right.second) < 0.01f);
    auto ringRt = FixtureType::fromVar (juce::JSON::parse (juce::JSON::toString (ring.toVar())));
    CHECK (ringRt.hasCustomLayout() && std::abs (ringRt.cellOffset (4).first - right.first) < 0.01f);
    PatchedFixture rf { 9, "Ring", ring.id, 1, 1 };
    rf.layoutX = 100; rf.layoutY = 100;
    CHECK (std::abs (cellLayoutPosition (rf, ring, 4).first - (100.0f + right.first)) < 0.01f);
    auto sn = cellLayouts::snake (3, 2);
    CHECK (sn[3].first == 2.0f && sn[5].first == 0.0f);
    FixtureType changed = ring; changed.cellsX = 10;                 // count changed: back to the grid
    CHECK (! changed.hasCustomLayout());

    DmxEngine engine;
    { const juce::ScopedLock sl (engine.dataLock); engine.library.addOrReplace (spot); engine.library.addOrReplace (panel); }
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("phase-lib-test");
    dir.deleteRecursively();
    dir.createDirectory();
    CHECK (showfile::exportLibrary ({ spot, panel }, dir.getChildFile ("mine.phaselib")).isEmpty());

    DmxEngine other;
    int added = 0;
    CHECK (showfile::importLibrary (other, dir, added).isEmpty() && added == 2);
    CHECK (other.library.find (spot.id) != nullptr);

    // the machine library follows the user into new shows
    showfile::saveMachineLibrary (engine);
    DmxEngine fresh;
    showfile::newShow (fresh);
    CHECK (fresh.library.find (panel.id) != nullptr);
    dir.deleteRecursively();
}

static void testGdtfShareClient()
{
    // needs a local stand-in server: PHASE_GDTF_SHARE_URL=http://127.0.0.1:8765/
    if (juce::SystemStats::getEnvironmentVariable ("PHASE_GDTF_SHARE_URL", {}).isEmpty()) return;
    section ("GDTF Share client against a local test server");
    GdtfShare share;
    CHECK (share.login ("tester", "wrong").isNotEmpty() && ! share.isLoggedIn());
    const auto err = share.login ("tester", "secret");
    std::cout << "    login: " << (err.isEmpty() ? "ok" : err) << "\n";
    CHECK (err.isEmpty() && share.isLoggedIn());
    std::vector<GdtfShare::Entry> list;
    CHECK (share.fetchList (list).isEmpty() && list.size() == 1 && list[0].rid == 42 && list[0].modes.size() == 1);
    juce::MemoryBlock data;
    const auto dl = share.download (42, data);
    std::cout << "    download: " << (dl.isEmpty() ? "ok" : dl) << " (" << (int) data.getSize() << " bytes)\n";
    CHECK (dl.isEmpty() && data.getSize() > 100);
}

static void testCommandLineAndHighlight()
{
    section ("Command line, value spreading, highlight");
    FixtureLibrary lib; Patch patch; Programmer prog; Playback pb; PresetLibrary presets;
    patch.addRange ("builtin.dimmer", 10, 1, 1, 1, "D", lib);
    patch.add ({ 301, "Bar", "builtin.bar8", 2, 1 }, lib);
    patch.add ({ 401, "Spot", "builtin.spot16", 3, 1 }, lib);

    auto run = [&] (const char* c) { return command::execute (c, prog, patch, lib, presets); };
    auto r = run ("1 thru 10");
    CHECK (r.error.isEmpty() && prog.selection().size() == 10 && prog.selection().front() == 1);
    run ("10 thru 1");
    CHECK (prog.selection().front() == 10 && prog.selection().back() == 1);
    run ("1 thru 5 + 8 - 3");
    CHECK (prog.selection() == std::vector<int> ({ 1, 2, 4, 5, 8 }));
    r = run ("1 thru 5 at 0 thru 100");
    CHECK (r.error.isEmpty() && std::abs (prog.get (3, "Dimmer").value_or (-1) - 0.5f) < 0.001f
           && prog.get (5, "Dimmer").value_or (-1) > 0.999f && prog.get (1, "Dimmer").value_or (-1) < 0.001f);
    run ("at full");
    CHECK (prog.get (1, "Dimmer").value_or (0) > 0.999f);
    run ("1 thru 5 at 100 thru 0 thru 100");
    CHECK (prog.get (3, "Dimmer").value_or (-1) < 0.001f && prog.get (5, "Dimmer").value_or (0) > 0.999f);
    r = run ("301.1 thru 301.4 at 50");
    CHECK (r.error.isEmpty() && prog.selection().size() == 4 && prog.get (301, "Dimmer@2").value_or (-1) > 0.49f);
    r = run ("401 pan at 25");
    CHECK (r.error.isEmpty() && std::abs (prog.get (401, "Pan").value_or (-1) - 0.25f) < 0.001f);
    CHECK (run ("401 red at 50").error.isNotEmpty());           // the spot has no red
    CHECK (run ("banana").error.isNotEmpty() && run ("999").error.isNotEmpty());
    presets.setGroup ("Odd", { 1, 3, 5 });
    run ("group 1");
    CHECK (prog.selection() == std::vector<int> ({ 1, 3, 5 }));
    CHECK (spreadValues ({ 0.0f, 1.0f }, 3)[1] == 0.5f && spreadValues ({ 0.3f }, 4)[3] == 0.3f);

    // highlight: the selected spot goes full and open, others untouched
    prog.clearValues();
    run ("401");
    run ("highlight");
    CHECK (prog.isHighlight());
    std::map<int, double> phases;
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    auto* spot = lib.find ("builtin.spot16");
    int dimCh = -1, shutterCh = -1, panCh = -1;
    for (auto& sl : spot->attributeSlots())
    {
        if (sl.attribute == "Dimmer") dimCh = sl.coarse;
        if (sl.attribute == "Shutter") shutterCh = sl.coarse;
        if (sl.attribute == "Pan") panCh = sl.coarse;
    }
    CHECK (out[3][(size_t) dimCh] == 255 && out[3][(size_t) shutterCh] == 255 && out[3][(size_t) panCh] == 128);
    CHECK (out[1][0] == 0);
    prog.setHighlightList ({ 2 });                                 // the patch view's list
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][1] == 255 && out[3][(size_t) dimCh] == 0);
    prog.setHighlight (false);
}

static void testNextPrevLowlight()
{
    section ("Next / Prev through the selection, lowlight colour");
    FixtureLibrary lib; Patch patch; Programmer prog; Playback pb; PresetLibrary presets;
    patch.addRange ("builtin.rgb", 4, 1, 1, 1, "P", lib);          // 3 channels each: R G B
    auto run = [&] (const char* c) { return command::execute (c, prog, patch, lib, presets); };

    CHECK (! prog.step (1));                                       // nothing selected
    run ("1 thru 4");
    CHECK (prog.step (1) && prog.selection() == std::vector<int> ({ 1 }));
    CHECK (prog.step (1) && prog.selection() == std::vector<int> ({ 2 }));
    CHECK (prog.step (-1) && prog.step (-1) && prog.selection() == std::vector<int> ({ 4 }));   // wraps
    CHECK (prog.isStepping() && prog.stepPosition() == 3);
    CHECK (run ("next").error.isEmpty() && prog.selection() == std::vector<int> ({ 1 }));
    CHECK (run ("all").selectionChanged && prog.selection().size() == 4 && ! prog.isStepping());
    run ("2 thru 3");                                              // a new selection starts a new step set
    prog.step (1);
    CHECK (prog.stepTargets().size() == 2 && prog.selection() == std::vector<int> ({ 2 }));

    // lowlight: the step set in the lowlight colour at its level; highlight on the current one
    run ("1 thru 4");
    prog.step (1); prog.step (1);                                  // fixture 2
    prog.lowlightColour = 0xff0000ffu;                             // blue
    prog.lowlightLevel = 0.5f;
    CHECK (run ("lowlight").error.isEmpty() && prog.isLowlight());
    prog.setHighlight (true);
    std::map<int, double> phases;
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    // fixture 1 (lowlight): blue at half (the rgb type has no dimmer: virtual dimmer scales RGB)
    CHECK (out[1][0] == 0 && out[1][1] == 0 && out[1][2] >= 126 && out[1][2] <= 129);
    // fixture 2 (current, highlighted): white full
    CHECK (out[1][3] == 255 && out[1][4] == 255 && out[1][5] == 255);
    prog.setHighlight (false);
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][5] >= 126 && out[1][5] <= 129 && out[1][3] == 0);   // without highlight, 2 is in the lowlight too
    prog.setLowlight (false);
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][2] == 0);

    // a colour-wheel-only fixture takes the lowlight wheel position
    patch.add ({ 50, "Spot", "builtin.spot16", 2, 1 }, lib);
    auto* spot = lib.find ("builtin.spot16");
    int wheelCh = -1;
    for (auto& sl : spot->attributeSlots()) if (sl.attribute == "ColorWheel") wheelCh = sl.coarse;
    bool mixes = false;
    for (auto& a : spot->attributes) mixes |= a.attribute == "ColorR" || a.attribute == "ColorC";
    if (wheelCh >= 0 && ! mixes)
    {
        prog.setSelection ({ 50 });
        prog.lowlightWheel = 0.5f;
        prog.setLowlight (true);
        out.clear();
        DmxEngine::renderFrame (ctx, out);
        CHECK (out[2][(size_t) wheelCh] == 128);
        prog.setLowlight (false);
    }

    // empty executors' faders sit at 0; a stored one starts at full
    Playback p2;
    CHECK (p2.faderPosition (1, 3) == 0.0f);
    p2.store (1, 3, ValueMap { { 1, { { "Dimmer", 1.0f } } } }, {}, StoreMode::Add, 0.0);
    CHECK (p2.faderPosition (1, 3) > 0.99f);
    p2.deleteExecutor (1, 3);
    CHECK (p2.faderPosition (1, 3) == 0.0f);
}

static void testCopyMoveAndPixelSets()
{
    section ("Copy / move / delete, pixel sets, custom rings");
    Playback pb;
    ValueMap v1 { { 1, { { "Dimmer", 1.0f } } } }, v2 { { 2, { { "Dimmer", 1.0f } } } };
    pb.store (1, 1, v1, {}, StoreMode::Add, 0.0);
    pb.store (1, 1, v2, {}, StoreMode::Add, 0.0);
    pb.store (1, 2, v2, {}, StoreMode::Add, 0.0);
    CHECK (pb.copyExecutor (1, 1, 2, 5, false) && pb.find (2, 5) != nullptr && pb.find (2, 5)->cues.size() == 2);
    CHECK (pb.find (2, 5)->name == "Exec 2.5" && pb.find (1, 1) != nullptr);
    CHECK (pb.copyExecutor (1, 1, 1, 2, true));                   // move onto a taken slot swaps
    CHECK (pb.find (1, 2)->cues.size() == 2 && pb.find (1, 1)->cues.size() == 1);
    CHECK (pb.copyExecutor (1, 2, 1, 7, true) && pb.find (1, 2) == nullptr && pb.find (1, 7) != nullptr);
    CHECK (! pb.copyExecutor (3, 3, 1, 1, false));                // empty source

    auto* ex = pb.findMutable (1, 7);
    ex->cues[0].name = "Intro"; ex->cues[1].name = "Drop";
    CHECK (pb.copyCue (1, 7, 0, 1, true));                        // move Intro after Drop
    CHECK (ex->cues[0].name == "Drop" && ex->cues[1].name == "Intro");
    CHECK (ex->cues[0].number < ex->cues[1].number);
    CHECK (pb.copyCue (1, 7, 0, 1, false) && ex->cues.size() == 3 && ex->cues[2].name == "Drop copy");

    PresetLibrary presets;
    const int g = presets.setGroup ("Front", { 1, 2, 3 });
    CHECK (presets.copy (PresetPool::Group, g, 9, false) && presets.find (PresetPool::Group, 9)->fixtures.size() == 3);
    CHECK (presets.copy (PresetPool::Group, 9, 4, true) && presets.find (PresetPool::Group, 9) == nullptr && presets.find (PresetPool::Group, 4) != nullptr);
    CuePart part; part.group = 4;
    ex->cues[0].parts.push_back (part);
    pb.remapPresetReferences ("group", 4, 6, true);
    CHECK (ex->cues[0].parts[0].group == 6);

    // pixel sets: 8 RGB cells + 8 separate white cells
    FixtureType t;
    t.id = "test.dual"; t.name = "Dual";
    t.cellsX = 8; t.cellsY = 1;
    for (int c = 1; c <= 8; ++c)
        for (auto a : { "ColorR", "ColorG", "ColorB" }) { AttributeDef d; d.attribute = a; d.cell = c; d.coarse = (c - 1) * 3 + (a[5] == 'R' ? 1 : a[5] == 'G' ? 2 : 3); t.attributes.push_back (d); }
    CHECK (t.cellsAreUniform());
    t.addPixelSet (8, { "ColorW" }, true);
    CHECK (t.cellCount() == 16 && t.highestChannel() == 32 && ! t.cellsAreUniform() && t.hasCustomLayout());
    FixtureLibrary lib;
    lib.addOrReplace (t);
    Patch patch; Programmer prog; PresetLibrary pr2;
    patch.add ({ 1, "Dual", "test.dual", 1, 1 }, lib);
    prog.set (1, "ColorR@9", 1.0f);       // nothing (cell 9 is white only) - just must not crash
    prog.setSelection ({ 1 });
    prog.lowlightColour = 0xffffffffu; prog.lowlightLevel = 1.0f; prog.setLowlight (true);   // white lowlight lights the white cells too
    std::map<int, double> phases; UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f };
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][24] == 255 && out[1][31] == 255);              // white cells 9 and 16
    prog.lowlightColour = 0xffff0000u;
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK (out[1][0] == 255 && out[1][24] == 0);                  // red: RGB cells red, white cells off

    // rings: 12 + 6 + 1 (centre)
    auto r = cellLayouts::rings ({ 12, 6, 1 });
    CHECK (r.size() == 19 && std::abs (r[18].first) < 1e-4f && std::abs (r[18].second) < 1e-4f);
    const float r0 = std::hypot (r[0].first, r[0].second), r1 = std::hypot (r[12].first, r[12].second);
    CHECK (r0 > r1 && r1 > 0.5f && r[0].second < 0.0f);           // outer first, starting at the top

    // hidden in the layout survives a save
    Patch p2;
    p2.add ({ 5, "X", "builtin.dimmer", 1, 100 }, lib);
    p2.setHiddenInLayout (5, true);
    Patch p3; p3.fromVar (p2.toVar());
    CHECK (p3.find (5) != nullptr && p3.find (5)->hiddenInLayout);
}

static void testRoutesAndDmxTest()
{
    section ("Art-Net routes per universe, DMX tester");
    NetSettings ns;
    ArtNetOutput a; a.localIp = "10.0.0.5"; a.broadcast = true; a.startUniverse = 1; a.universes = 2;
    ArtNetOutput b; b.localIp = "10.0.0.5"; b.broadcast = false; b.nodeIp = "10.0.0.20"; b.startUniverse = 3; b.universes = 4; b.artNetStart = 0;
    ArtNetOutput c = b; c.nodeIp = "10.0.0.21"; c.startUniverse = 5; c.universes = 1; c.artNetStart = 16;
    ArtNetOutput off = a; off.enabled = false; off.startUniverse = 1; off.universes = 0;
    ns.outputs = { a, b, c, off };
    auto d1 = ns.destinationsFor (1);
    CHECK (d1.size() == 1 && d1[0].ip.isEmpty() && d1[0].portAddress == 0 && d1[0].localIp == "10.0.0.5");
    auto d4 = ns.destinationsFor (4);
    CHECK (d4.size() == 1 && d4[0].ip == "10.0.0.20" && d4[0].portAddress == 1);          // node b's second universe
    auto d5 = ns.destinationsFor (5);                                                     // b and c both
    CHECK (d5.size() == 2 && d5[1].ip == "10.0.0.21" && d5[1].portAddress == 16 && d5[0].portAddress == 2);
    CHECK (ns.destinationsFor (7).empty());
    NetSettings back; back.fromVar (ns.toVar());
    CHECK (back.outputs.size() == 4 && back.outputs[1] == b && ! back.outputs[3].enabled);
    // older shows: one interface + base
    NetSettings old; old.fromVar (juce::JSON::parse ("{\"localIp\":\"2.0.0.1\",\"unicast\":true,\"unicastIp\":\"2.0.0.9\",\"artNetBase\":5}"));
    CHECK (old.outputs.size() == 1 && old.outputs[0].nodeIp == "2.0.0.9" && ! old.outputs[0].broadcast
           && old.destinationsFor (3).size() == 1 && old.destinationsFor (3)[0].portAddress == 7);

    // monitor: parse an ArtDmx packet
    juce::uint8 dmx[512] {}; dmx[0] = 11; dmx[511] = 99;
    auto pkt = artnet::buildArtDmx (0x123, 1, dmx, 512);
    int pa = 0, len = 0; DmxUniverse got {};
    CHECK (ArtNetMonitor::parseArtDmx (pkt.getData(), (int) pkt.getSize(), pa, got, len) && pa == 0x123 && len == 512 && got[0] == 11 && got[511] == 99);

    // batch re-patch from a start address (wraps to the next universe)
    {
        FixtureLibrary lib; Patch patch;
        patch.addRange ("builtin.spot16", 4, 1, 1, 1, "S", lib);       // 4 spots, 1.1 ...
        patch.add ({ 10, "D", "builtin.dimmer", 2, 500 }, lib);
        const int fp = lib.find ("builtin.spot16")->footprint();
        CHECK (patch.repatch ({ 1, 2, 3, 4 }, 1, 512 - fp * 2 + 1, 0, lib).isEmpty());
        CHECK (patch.find (1)->universe == 1 && patch.find (2)->address == 512 - fp + 1 && patch.find (3)->universe == 2 && patch.find (3)->address == 1);
        CHECK (patch.repatch ({ 1, 2 }, 2, 1, 0, lib).isNotEmpty());    // would overlap fixture 3, which stays put
        CHECK (patch.find (1)->universe == 1);                          // nothing changed
        CHECK (patch.repatch ({ 1, 2, 3, 4 }, 5, 10, 2, lib).isEmpty() && patch.find (2)->address == 10 + fp + 2);
    }

    DmxEngine::DmxTest t;
    t.values[1][10] = 200;
    t.values[4][1] = 255;                                         // an unpatched universe still goes out
    UniverseMap frame;
    frame[1].fill (0);
    DmxEngine::applyDmxTest (t, frame, 0.0);
    CHECK (frame[1][9] == 200 && frame.count (4) == 1 && frame[4][0] == 255);
    t.values.clear();
    t.chase = true; t.chaseUniverse = 1; t.chaseChannels = { 1, 2, 3 }; t.chaseStepsPerSecond = 1.0f; t.chaseLevel = 255;
    UniverseMap f2; DmxEngine::applyDmxTest (t, f2, 1.5);           // step 1 -> channel 2
    CHECK (f2[1][0] == 0 && f2[1][1] == 255 && f2[1][2] == 0);
}

namespace
{
    // a status packet like a nexus CDJ sends (0xd4 bytes)
    juce::MemoryBlock fakeStatus (int number, int trackPlayer, int slot, int type, juce::uint32 id, bool playing, bool master,
                                  int bpm100, juce::uint32 pitchRaw, juce::uint32 beat, int beatInBar)
    {
        juce::MemoryBlock b (0xd4, true);
        auto* p = static_cast<juce::uint8*> (b.getData());
        const juce::uint8 magic[10] { 0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c };
        std::memcpy (p, magic, 10);
        p[0x0a] = 0x0a;
        std::memcpy (p + 0x0b, "CDJ-2000NXS2", 12);
        p[0x21] = (juce::uint8) number; p[0x24] = (juce::uint8) number;
        p[0x27] = 1; p[0x28] = (juce::uint8) trackPlayer; p[0x29] = (juce::uint8) slot; p[0x2a] = (juce::uint8) type;
        p[0x2c] = (juce::uint8) (id >> 24); p[0x2d] = (juce::uint8) (id >> 16); p[0x2e] = (juce::uint8) (id >> 8); p[0x2f] = (juce::uint8) id;
        p[0x7b] = playing ? 3 : 5;
        p[0x89] = (juce::uint8) ((playing ? 0x40 : 0) | (master ? 0x20 : 0) | 0x08);
        for (int i = 0; i < 4; ++i) p[0x8c + i] = (juce::uint8) (pitchRaw >> (24 - 8 * i));
        p[0x92] = (juce::uint8) (bpm100 >> 8); p[0x93] = (juce::uint8) bpm100;
        for (int i = 0; i < 4; ++i) p[0xa0 + i] = (juce::uint8) (beat >> (24 - 8 * i));
        p[0xa6] = (juce::uint8) beatInBar;
        return b;
    }

    juce::MemoryBlock pwv5Tag (int entries)
    {
        juce::MemoryOutputStream o;
        o.writeInt (0);                        // a few bytes before the tag, as the dbserver sends
        o.write ("PWV5", 4);
        o.writeIntBigEndian (24);              // header length
        o.writeIntBigEndian (24 + entries * 2);
        o.writeIntBigEndian (2);               // bytes per entry
        o.writeIntBigEndian (entries);
        o.writeIntBigEndian (0x00960000);
        for (int i = 0; i < entries; ++i)
        {
            const int r = 7, g = i % 8, b = 0, h = 31;
            o.writeShortBigEndian ((short) ((r << 13) | (g << 10) | (b << 7) | (h << 2)));
        }
        return o.getMemoryBlock();
    }

    /** A pretend CDJ database server on localhost: port query, greeting, setup, and the track requests. */
    class FakeDbServer : public juce::Thread
    {
    public:
        FakeDbServer() : juce::Thread ("fake dbserver") {}
        ~FakeDbServer() override { query.close(); db.close(); stopThread (2000); }
        int queryPort = 0, dbPort = 0;
        std::atomic<int> requests { 0 };

        bool open()
        {
            for (int p = 23523; p < 23600 && queryPort == 0; ++p) if (query.createListener (p, "127.0.0.1")) queryPort = p;
            for (int p = 24051; p < 24150 && dbPort == 0; ++p) if (db.createListener (p, "127.0.0.1")) dbPort = p;
            if (queryPort == 0 || dbPort == 0) return false;
            startThread();
            return true;
        }

        void run() override
        {
            using namespace prodj;
            while (! threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> q (query.waitForNextConnection());
                if (q == nullptr || threadShouldExit()) return;
                juce::uint8 ask[19];
                if (! readAll (*q, ask, 19)) continue;
                const juce::uint8 port[2] { (juce::uint8) (dbPort >> 8), (juce::uint8) dbPort };
                q->write (port, 2);
                q->close();

                std::unique_ptr<juce::StreamingSocket> c (db.waitForNextConnection());
                if (c == nullptr) return;
                juce::uint8 hello[5];
                if (! readAll (*c, hello, 5)) continue;
                c->write (hello, 5);
                Message m;
                while (readMsg (*c, m))
                {
                    ++requests;
                    auto reply = [&] (int type, std::vector<Field> args) { Message r; r.txId = m.txId; r.type = type; r.args = std::move (args); send (*c, r); };
                    auto blobReply = [&] (int type, const juce::MemoryBlock& blob)
                    { reply (type, { Field::num ((juce::uint32) m.type), Field::num (0), Field::num ((juce::uint32) blob.getSize()), Field::data (blob) }); };
                    switch (m.type)
                    {
                        case 0x0000: reply (0x4000, { Field::num (0), Field::num (2) }); break;
                        case 0x2002: reply (0x4000, { Field::num (0x2002), Field::num (4) }); break;
                        case 0x3000:
                        {
                            reply (0x4001, { Field::num (1), Field::num (0) });
                            auto item = [&] (int type, juce::uint32 id, const juce::String& label)
                            {
                                reply (0x4101, { Field::num (0), Field::num (id), Field::num ((juce::uint32) label.length() * 2 + 2), Field::str (label),
                                                 Field::num (2), Field::str (""), Field::num ((juce::uint32) type), Field::num (0), Field::num (0),
                                                 Field::num (0), Field::num (0), Field::num (0) });
                            };
                            item (0x04, 77, juce::CharPointer_UTF8 ("Dhaka Dub \xe2\x80\x93 Mix"));
                            item (0x07, 5, "Bhai Bhai");
                            item (0x0f, 0, "8A");
                            item (0x0b, 245, "");
                            reply (0x4201, {});
                            break;
                        }
                        case 0x2204:
                        {
                            juce::MemoryOutputStream g;
                            for (int i = 0; i < 0x14; ++i) g.writeByte (0);
                            for (int beat = 1; beat <= 64; ++beat)
                            {
                                g.writeShort ((short) ((beat - 1) % 4 + 1));          // little-endian
                                g.writeShort (12800);
                                g.writeInt (100 + (beat - 1) * 469);
                                g.writeInt (0); g.writeInt (0);
                            }
                            blobReply (0x4602, g.getMemoryBlock());
                            break;
                        }
                        case 0x2004:
                        {
                            juce::MemoryBlock pv (900, true);
                            for (int i = 0; i < 400; ++i) { static_cast<juce::uint8*> (pv.getData())[i * 2] = (juce::uint8) (i % 32); static_cast<juce::uint8*> (pv.getData())[i * 2 + 1] = 7; }
                            blobReply (0x4402, pv);
                            break;
                        }
                        case 0x2c04: blobReply (0x4f02, pwv5Tag (1500)); break;
                        default: reply (0x4003, { Field::num ((juce::uint32) m.type) }); break;
                    }
                }
            }
        }

    private:
        static bool readAll (juce::StreamingSocket& s, void* d, int n)
        {
            int got = 0;
            while (got < n) { if (s.waitUntilReady (true, 2000) != 1) return false; const int r = s.read (static_cast<char*> (d) + got, n - got, false); if (r <= 0) return false; got += r; }
            return true;
        }
        static bool readMsg (juce::StreamingSocket& s, prodj::Message& m)
        {
            juce::MemoryBlock buf;
            for (int guard = 0; guard < 4096; ++guard)
            {
                const int used = prodj::decode (buf.getData(), buf.getSize(), m);
                if (used > 0) return true;
                if (used < 0) return false;
                juce::uint8 b;
                if (! readAll (s, &b, 1)) return false;
                buf.append (&b, 1);
            }
            return false;
        }
        static void send (juce::StreamingSocket& s, const prodj::Message& m) { auto b = prodj::encode (m); s.write (b.getData(), (int) b.getSize()); }

        juce::StreamingSocket query, db;
    };
}

static void testProDjLink()
{
    section ("Pro DJ Link: packets, dbserver messages, waveforms, a deck end to end");
    using namespace prodj;

    // keep-alive
    juce::MACAddress mac ((const juce::uint8*) "\x01\x02\x03\x04\x05\x06");
    auto ka = buildKeepAlive (4, "Phase", mac, "192.168.1.20", 3);
    Device dev;
    CHECK (ka.getSize() == 0x36 && parseKeepAlive (ka.getData(), (int) ka.getSize(), dev));
    CHECK (dev.number == 4 && dev.name == "Phase" && dev.ip == "192.168.1.20" && ! dev.isMixer);

    // status: deck 2 playing, master, 128 BPM at +6 %, beat 33
    const auto pitch6 = (juce::uint32) std::lround (1048576.0 * 1.06);
    auto st = fakeStatus (2, 2, 3, 1, 77, true, true, 12800, pitch6, 33, 1);
    Status s;
    CHECK (parseStatus (st.getData(), (int) st.getSize(), s));
    CHECK (s.number == 2 && s.trackPlayer == 2 && s.slot == 3 && s.trackType == 1 && s.rekordboxId == 77);
    CHECK (s.playing && s.master && s.onAir && std::abs (s.trackBpm - 128.0f) < 0.01f && std::abs (s.pitch - 6.0f) < 0.01f);
    CHECK (s.beat == 33 && s.beatInBar == 1 && s.name == "CDJ-2000NXS2");

    // dbserver message round trip (blob left out after a 0, strings in UTF-16)
    Message m;
    m.txId = 9; m.type = 0x4402;
    juce::MemoryBlock blob ("abc", 3);
    m.args = { Field::num (0x2004), Field::num (0), Field::num (3), Field::data (blob), Field::str (juce::CharPointer_UTF8 ("H\xc3\xa9llo")) };
    auto bytes = encode (m);
    Message back;
    CHECK (decode (bytes.getData(), bytes.getSize(), back) == (int) bytes.getSize());
    CHECK (back.type == 0x4402 && back.txId == 9 && back.args.size() == 5 && back.blobArg() != nullptr && *back.blobArg() == blob && back.stringArg (4) == juce::String (juce::CharPointer_UTF8 ("H\xc3\xa9llo")));
    CHECK (decode (bytes.getData(), bytes.getSize() - 3, back) == 0);           // not all there yet
    Message empty; empty.type = 0x4a02; empty.args = { Field::num (0x2904), Field::num (0), Field::num (0), Field::data ({}) };
    auto eb = encode (empty);
    CHECK (decode (eb.getData(), eb.getSize(), back) == (int) eb.getSize() && back.args.size() == 4);
    CHECK (fourcc ("PWV4") == 878073680u && fourcc ("EXT") == 5527621u);
    CHECK (dmst (3, 8, 2, 1) == 0x03080201u);

    // waveforms
    auto colour = decodeColourDetail (pwv5Tag (300));
    CHECK (colour.height.size() == 300 && colour.perSecond == 150 && colour.height[0] == 31 && (colour.colour[0] & 0xffffff) == 0xff0000);
    juce::MemoryBlock det (150, true);
    static_cast<juce::uint8*> (det.getData())[0] = (juce::uint8) ((7 << 5) | 20);
    auto mono = decodeDetail (det);
    CHECK (mono.height.size() == 150 && mono.height[0] == 20 && (mono.colour[0] & 0xff) == 255);

    // end to end: a fake player database on localhost, a deck that loads a track
    FakeDbServer server;
    if (! server.open()) { std::cout << "  (skipped: no local ports)\n"; return; }
    dbQueryPort = server.queryPort;
    {
        DbClient db;
        CHECK (db.connect ("127.0.0.1", 3).isEmpty());
        auto t = db.fetchTrack (3, 1, 77);
        CHECK (t.info.title == juce::String (juce::CharPointer_UTF8 ("Dhaka Dub \xe2\x80\x93 Mix")) && t.info.artist == "Bhai Bhai" && t.info.key == "8A" && t.info.durationSec == 245);
        CHECK (t.grid.timeMs.size() == 64 && t.grid.timeMs[1] == 569 && t.grid.beatInBar[4] == 1 && std::abs (t.grid.bpm[0] - 128.0f) < 0.01f);
        CHECK (t.preview.height.size() == 400 && t.preview.height[5] == 5);
        CHECK (t.detail.height.size() == 1500 && t.detail.perSecond == 150);
        db.close();
    }

    ProDjLink link;
    auto keep = buildKeepAlive (2, "CDJ-2000NXS2", mac, "127.0.0.1", 2);
    link.handlePacket (announcePort, keep.getData(), (int) keep.getSize(), "127.0.0.1");
    link.handlePacket (statusPort, st.getData(), (int) st.getSize(), "127.0.0.1");
    bool loaded = false;
    for (int i = 0; i < 60 && ! loaded; ++i)
    {
        juce::Thread::sleep (50);
        for (auto& d : link.decks()) loaded |= d.number == 2 && d.track != nullptr && ! d.track->detail.empty();
    }
    CHECK (loaded);
    auto decks = link.decks();
    CHECK (decks.size() == 1 && std::abs (decks[0].bpm() - 135.68f) < 0.05f && std::abs (link.masterBpm() - 135.68f) < 0.05f);
    // beat 33 is at 100 + 32 * 469 ms in the grid: the playhead starts there
    const double ph = decks[0].playheadMs (decks[0].beatTime);
    CHECK (std::abs (ph - (100 + 32 * 469)) < 1.0);
    CHECK (decks[0].track->info.title.startsWith ("Dhaka Dub"));
    dbQueryPort = 12523;
}

static void testEffectTracking()
{
    section ("Cue lists: effects track until replaced or stopped; store into a cue");
    Playback pb;
    auto fx = [] (EffectKind k, std::vector<int> f)
    {
        EffectParams e; e.kind = k; e.id = EffectParams::nextId(); e.fixtures = std::move (f); e.updateFamily(); return e;
    };
    auto mov = fx (EffectKind::Movement, { 1, 2, 3, 4 });
    auto dim = fx (EffectKind::Dimmer, { 1, 2, 3, 4 });
    auto col = fx (EffectKind::Color, { 1, 2, 3, 4 });
    ValueMap v { { 1, { { "Dimmer", 1.0f } } } };
    pb.store (1, 1, v, { mov, dim }, StoreMode::Add, 0.0);
    pb.store (1, 1, {}, { col }, StoreMode::Add, 0.0);
    auto* ex = pb.find (1, 1);
    CHECK (ex->cues.size() == 2);
    CHECK (pb.effectsAt (*ex, 1).size() == 3);           // movement + dimmer tracked in, colour added

    pb.go (1, 1, 0.0); pb.go (1, 1, 0.1);
    CHECK (pb.state (1, 1).cue == 1);
    CHECK (pb.state (1, 1).liveEffects.size() == 3);
    bool sameId = false;                                 // same effect id: the phase carries on
    for (auto& e : pb.state (1, 1).liveEffects) sameId |= e.id == mov.id;
    CHECK (sameId);

    // a new movement on fixtures 3-4 in cue 3 takes those over; 1-2 keep cue 1's
    auto mov2 = fx (EffectKind::Movement, { 3, 4 });
    pb.store (1, 1, {}, { mov2 }, StoreMode::Add, 0.2);
    auto at2 = pb.effectsAt (*ex, 2);
    CHECK (at2.size() == 4);
    for (auto& ce : at2)
        if (ce.effect.id == mov.id) { CHECK (ce.effect.fixtures == std::vector<int> ({ 1, 2 })); CHECK (ce.fromCue == 0); }

    // stop the dimmer effect from cue 3 on; cue 2 still has it
    pb.stopEffect (1, 1, 2, dim);
    CHECK (pb.effectsAt (*ex, 2).size() == 3);
    CHECK (pb.effectsAt (*ex, 1).size() == 3);
    // stopping an effect stored in that cue removes it
    pb.stopEffect (1, 1, 2, mov2);
    CHECK (pb.effectsAt (*ex, 2).size() == 2);
    // un-stop: remove the marker
    int marker = 0;
    for (auto& e : ex->cues[2].effects) if (e.stop) marker = e.id;
    CHECK (marker != 0);
    pb.removeCueEffect (1, 1, 2, marker);
    CHECK (pb.effectsAt (*ex, 2).size() == 3);

    // tracking off: only the cue's own effects
    pb.setTracking (1, 1, false);
    CHECK (pb.effectsAt (*ex, 1).size() == 1);
    pb.setTracking (1, 1, true);

    // store into a chosen cue: merge, overwrite, insert after
    pb.store (1, 1, ValueMap { { 2, { { "Dimmer", 0.5f } } } }, {}, StoreMode::Merge, 0.3, {}, 0);
    CHECK (ex->cues[0].values.count (2) == 1 && ex->cues[0].values.count (1) == 1);
    pb.store (1, 1, ValueMap { { 5, { { "Dimmer", 0.5f } } } }, {}, StoreMode::Add, 0.3, {}, 0);
    CHECK (ex->cues.size() == 4);
    CHECK (std::abs (ex->cues[1].number - 1.5) < 1e-9);
    CHECK (ex->cues[1].name == "Cue 1.5");
    CHECK (ex->cues[1].values.count (5) == 1);
    pb.store (1, 1, ValueMap { { 6, { { "Dimmer", 0.5f } } } }, {}, StoreMode::Overwrite, 0.3, {}, 1);
    CHECK (ex->cues.size() == 4 && ex->cues[1].values.count (6) == 1 && ex->cues[1].values.count (5) == 0);
    CHECK (ex->cues[1].name == "Cue 1.5");
    CHECK (ex->cues[0].effects.size() == 2);             // other cues untouched

    // the stop flag survives a save
    CHECK (ex->cues[3].effects.empty());                 // cue 3: its own movement removed, the stop marker removed
    auto copy = EffectParams::fromVar (ex->cues[2].effects.back().toVar());
    auto s = dim; s.stop = true;
    CHECK (EffectParams::fromVar (s.toVar()).stop);
    CHECK (! copy.stop);
}

static void testCueFolders()
{
    section ("Cue folders: groups and preset references follow updates");
    DmxEngine engine;
    auto& prog = engine.programmer;
    engine.patch.addRange ("builtin.spot16", 3, 1, 1, 1, "Spot", engine.library);
    auto panOf = [&] (int fid)
    {
        engine.playback.refreshParts();
        UniverseMap out;
        std::map<int, double> phases;
        RenderContext ctx { engine.library, engine.patch, prog, engine.playback, phases, 10.0, 1.0f, 1.0f };
        ctx.presets = &engine.presets;
        DmxEngine::renderFrame (ctx, out);
        auto* pf = engine.patch.find (fid);
        return out[1][(size_t) pf->address - 1];      // pan coarse is channel 1
    };

    engine.presets.setGroup ("Pair", { 1, 2 });
    prog.setSelection ({ 1, 2 });
    prog.set (1, "Pan", 0.2f); prog.set (2, "Pan", 0.2f);
    CHECK (engine.presets.store (PresetPool::Position, 1, prog, engine.patch, engine.library, PresetStoreMode::Overwrite).isEmpty());
    prog.clearValues();

    // program like a console: group, position preset, full
    engine.presets.recall (PresetPool::Group, 1, prog, engine.patch, engine.library);
    engine.presets.recall (PresetPool::Position, 1, prog, engine.patch, engine.library);
    command::execute ("at full", prog, engine.patch, engine.library, engine.presets);
    CHECK (prog.presetReferences().at (1).at ("position") == 1);
    CHECK (engine.storeFromProgrammer (1, 1, StoreMode::Add, 0.0).isNotEmpty());
    auto& cue = engine.playback.find (1, 1)->cues[0];
    CHECK (cue.parts.size() == 1 && cue.parts[0].group == 1 && cue.parts[0].presets.at ("position") == 1
           && cue.parts[0].values.at ("Dimmer") == 1.0f && cue.values.empty());
    prog.clearValues(); prog.clearSelection();

    engine.playback.go (1, 1, 0.0);
    CHECK (panOf (1) == (juce::roundToInt (0.2f * 65535.0f) >> 8));

    // update the preset: the cue follows
    prog.setSelection ({ 1, 2 });
    prog.set (1, "Pan", 0.7f); prog.set (2, "Pan", 0.7f);
    engine.presets.store (PresetPool::Position, 1, prog, engine.patch, engine.library, PresetStoreMode::Overwrite);
    prog.clearValues(); prog.clearSelection();
    const int coarse07 = juce::roundToInt (0.7f * 65535.0f) >> 8;     // 16-bit pan, coarse byte
    CHECK (panOf (2) == coarse07);

    // add a fixture to the group: it joins the cue (preset by position, dimmer from the folder)
    engine.presets.setGroup ("Pair", { 1, 2, 3 });
    CHECK (panOf (3) == coarse07);
    auto* st = &engine.playback.state (1, 1);
    CHECK (st->target.count (3) && st->target.at (3).at ("Dimmer") == 1.0f);

    // a plain value (not from a preset) stays a plain cue value
    prog.setSelection ({ 3 });
    prog.set (3, "Tilt", 0.9f);
    engine.storeFromProgrammer (1, 1, StoreMode::Add, 0.0);
    CHECK (engine.playback.find (1, 1)->cues[1].parts.empty() && engine.playback.find (1, 1)->cues[1].values.at (3).at ("Tilt") == 0.9f);

    // round trip
    auto rt = Cue::fromVar (juce::JSON::parse (juce::JSON::toString (engine.playback.find (1, 1)->cues[0].toVar())));
    CHECK (rt.parts.size() == 1 && rt.parts[0].group == 1 && rt.parts[0].presets.at ("position") == 1);
}

static void testStage3D()
{
    section ("3D: positions, mounts, beams");
    FixtureLibrary lib;
    auto* spot = lib.find ("builtin.spot16");
    PatchedFixture f { 1, "Spot", "builtin.spot16", 1, 1 };
    f.layoutX = 200; f.layoutY = 50;
    auto p = stage3d::position (f);
    CHECK (std::abs (p.x - 4.0f) < 0.001f && std::abs (p.y - 4.0f) < 0.001f && std::abs (p.z - 1.0f) < 0.001f);
    auto down = stage3d::beamDirection (f, *spot, 0.5f, 0.5f);
    CHECK (std::abs (down.y + 1.0f) < 0.001f);                                   // hung: straight down
    auto tilted = stage3d::beamDirection (f, *spot, 0.5f, 0.5f + 90.0f / 270.0f);   // tilt +90 degrees
    CHECK (std::abs (tilted.y) < 0.01f && tilted.z > 0.99f);                   // towards the audience
    f.rx3 = 180.0f;                                                              // standing on the floor
    CHECK (stage3d::beamDirection (f, *spot, 0.5f, 0.5f).y > 0.99f);
    auto hit = stage3d::floorHit ({ 0, 4, 0 }, { 0, -1, 0 }, 20.0f);
    CHECK (std::abs (hit.y) < 0.001f);
    CHECK (std::abs (stage3d::beamAngle (*spot, 0.5f) - 26.0f) < 0.01f);
    Patch patch;
    patch.add (f, lib);
    patch.set3D (1, 1.0f, 6.0f, -2.0f, 0.0f, 90.0f, 0.0f);
    Patch rt; rt.fromVar (patch.toVar());
    CHECK (rt.find (1)->has3D && std::abs (rt.find (1)->y3 - 6.0f) < 0.001f && std::abs (rt.find (1)->ry3 - 90.0f) < 0.001f);
}

static void testPixelMap()
{
    section ("Pixel map: test pattern and video frame sampling");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    std::map<int, double> phases;
    patch.add ({ 1, "Par", "builtin.rgb", 1, 1 }, lib);
    patch.add ({ 2, "Bar", "builtin.bar8", 1, 10 }, lib);
    patch.add ({ 3, "Off", "builtin.rgb", 1, 40 }, lib);
    patch.setLayoutPosition (1, 50.0f, 50.0f);           // u = 0.25 inside a 0..200 map
    patch.setLayoutPosition (2, 100.0f, 50.0f);          // bar cells span x = 44..156
    patch.setLayoutPosition (3, 500.0f, 50.0f);          // outside

    std::vector<PixelMap> maps (1);
    maps[0].x = 0; maps[0].y = 0; maps[0].w = 200; maps[0].h = 100;
    maps[0].source = "Test: Gradient";                    // red -> blue left to right
    maps[0].intensity = 0.5f;

    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f, nullptr, nullptr, nullptr, &maps, nullptr };
    DmxEngine::renderFrame (ctx, out);
    // RGB par with virtual dimmer: colour (0.75, 0, 0.25) x intensity 0.5
    CHECK_EQ (out[1][0], juce::roundToInt (0.75f * 0.5f * 255.0f));
    CHECK_EQ (out[1][2], juce::roundToInt (0.25f * 0.5f * 255.0f));
    CHECK_EQ (out[1][39], 0);                             // outside the map: dark
    CHECK_EQ (out[1][9], 128);                            // bar master dimmer = map intensity
    const int firstR = out[1][10], lastR = out[1][10 + 21];
    CHECK (firstR > lastR && firstR > 150 && lastR < 100); // the gradient runs across the bar's cells

    prog.set (1, "ColorR", 0.0f);                         // the programmer still wins
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 0);
    prog.clearValues();

    // NDI-style source from the video hub
    VideoHub hub;
    auto frame = std::make_shared<VideoFrame>();
    frame->width = 2; frame->height = 1;
    frame->pixels = { 0xff00ff00u, 0xff00ff00u };          // all green
    hub.setFrame ("NDI: Test", frame);
    maps[0].source = "NDI: Test";
    maps[0].intensity = 1.0f;
    RenderContext ctx2 { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f, nullptr, nullptr, nullptr, &maps, &hub };
    out.clear();
    DmxEngine::renderFrame (ctx2, out);
    CHECK_EQ (out[1][1], 255); CHECK_EQ (out[1][0], 0);

    maps[0].source = "NDI: Missing";                      // no frame yet: map does nothing
    out.clear();
    DmxEngine::renderFrame (ctx2, out);
    CHECK_EQ (out[1][1], 0);

    RGBf mid = frame->sample (0.5f, 0.5f);
    CHECK (std::abs (mid.g - 1.0f) < 0.001f);
    auto rt = PixelMap::fromVar (juce::JSON::parse (juce::JSON::toString (maps[0].toVar())));
    CHECK (rt.source == "NDI: Missing" && std::abs (rt.w - 200.0f) < 0.01f);
}

static void testPlayback()
{
    section ("Playback: store, tracking, HTP/LTP, fades, auto start, flash");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    std::map<int, double> phases;
    patch.add ({ 1, "Spot", "builtin.spot16", 1, 1 }, lib);

    ValueMap c1 { { 1, { { "Dimmer", 1.0f }, { "Pan", 0.25f } } } };
    ValueMap c2 { { 1, { { "Pan", 0.75f } } } };
    CHECK (pb.store (1, 1, c1, {}, StoreMode::Add, 0).isNotEmpty());
    pb.store (1, 1, c2, {}, StoreMode::Add, 0);
    CHECK_EQ ((int) pb.find (1, 1)->cues.size(), 2);
    CHECK (pb.store (1, 2, {}, {}, StoreMode::Add, 0).startsWith ("Programmer is empty"));

    auto tracked = Playback::trackedState (*pb.find (1, 1), 1);
    CHECK (std::abs (tracked[1]["Dimmer"] - 1.0f) < 0.001f);      // tracked from cue 1

    pb.go (1, 1, 0.0);
    auto out = render (lib, patch, prog, pb, phases, 0.0, 1.0f);
    CHECK_EQ (out[1][5], 255);
    CHECK_EQ (out[1][0], 0x40);

    pb.go (1, 1, 1.0);                                 // cue 2: pan 0.75, dimmer tracks
    out = render (lib, patch, prog, pb, phases, 1.0, 1.0f);
    CHECK_EQ (out[1][0], 0xbf);
    CHECK_EQ (out[1][5], 255);

    pb.setFader (1, 1, 0.5f, 1.0);                     // fader scales intensity only
    out = render (lib, patch, prog, pb, phases, 1.0, 1.0f);
    CHECK_EQ (out[1][5], 128);
    CHECK_EQ (out[1][0], 0xbf);

    // second executor: HTP on dimmer, LTP on pan
    ValueMap other { { 1, { { "Dimmer", 0.8f }, { "Pan", 0.0f } } } };
    pb.store (1, 2, other, {}, StoreMode::Add, 1.0);
    pb.go (1, 2, 1.0);
    out = render (lib, patch, prog, pb, phases, 1.0, 1.0f);
    CHECK_EQ (out[1][5], 204);                         // max(0.5, 0.8)
    CHECK_EQ (out[1][0], 0);                           // latest executor wins pan

    prog.set (1, "Dimmer", 0.1f);                      // programmer overrides everything
    out = render (lib, patch, prog, pb, phases, 1.0, 1.0f);
    CHECK_EQ (out[1][5], 26);
    prog.clearValues();

    // fade: cue with 2 s fade, check halfway
    pb.off (1, 2);
    pb.setCueProperties (1, 1, 0, "Look", 2.0f, 2.0f);
    pb.setFader (1, 1, 1.0f, 2.0);
    pb.off (1, 1);
    pb.go (1, 1, 10.0);                                // cue 1 from off: dimmer fades from 0
    out = render (lib, patch, prog, pb, phases, 11.0, 1.0f);
    CHECK_EQ (out[1][5], 128);
    out = render (lib, patch, prog, pb, phases, 12.5, 1.0f);
    CHECK_EQ (out[1][5], 255);

    // release: Off with a 2 s fade out holds the look and fades intensity
    pb.release (1, 1, 20.0);
    out = render (lib, patch, prog, pb, phases, 21.0, 1.0f);
    CHECK_EQ (out[1][5], 128);
    CHECK (pb.state (1, 1).isOn());
    pb.update (22.1);
    CHECK (! pb.state (1, 1).isOn());

    // in / out fades: cue 2 drops intensity with a 4 s out fade but moves pan with a 1 s in fade
    ValueMap down { { 1, { { "Dimmer", 0.0f }, { "Pan", 1.0f } } } };
    pb.store (1, 1, down, {}, StoreMode::Add, 22.0);
    pb.setCueProperties (1, 1, 0, "Up", 0.0f, 0.0f);
    pb.setCueProperties (1, 1, 2, "Down", 1.0f, 4.0f);
    pb.gotoCue (1, 1, 1, 30.0);                        // cue 2: dimmer 1 (tracked), pan 0.75
    pb.gotoCue (1, 1, 2, 31.0);
    out = render (lib, patch, prog, pb, phases, 33.0, 1.0f);   // 2 s in
    CHECK_EQ (out[1][5], 128);                         // out fade halfway
    CHECK_EQ (out[1][0], 0xff);                        // in fade done

    // tracking off: cue 2 alone has no dimmer value
    pb.setTracking (1, 1, false);
    CHECK (! Playback::trackedState (*pb.find (1, 1), 1)[1].count ("Dimmer"));
    pb.setTracking (1, 1, true);
    CHECK (Playback::trackedState (*pb.find (1, 1), 1)[1].count ("Dimmer"));
    pb.deleteCue (1, 1, 2);

    // auto start / stop with the fader
    pb.off (1, 1);
    pb.setFader (1, 1, 0.0f, 20.0);
    CHECK (! pb.state (1, 1).isOn());
    pb.setFader (1, 1, 0.3f, 20.0);
    CHECK (pb.state (1, 1).isOn() && pb.state (1, 1).cue == 0);
    pb.setFader (1, 1, 0.0f, 20.0);
    CHECK (! pb.state (1, 1).isOn());

    // flash: full while held, off on release
    pb.setFlash (1, 1, true, 21.0);
    out = render (lib, patch, prog, pb, phases, 30.0, 1.0f);
    CHECK_EQ (out[1][5], 255);
    pb.setFlash (1, 1, false, 30.0);
    CHECK (! pb.state (1, 1).isOn());

    // store modes
    pb.store (1, 1, ValueMap { { 1, { { "Zoom", 1.0f } } } }, {}, StoreMode::Merge, 31.0);
    CHECK_EQ ((int) pb.find (1, 1)->cues.size(), 2);
    CHECK (pb.find (1, 1)->cues[1].values.at (1).count ("Zoom") == 1);
    pb.store (1, 1, c1, {}, StoreMode::Overwrite, 31.0);
    CHECK_EQ ((int) pb.find (1, 1)->cues.size(), 1);

    // wrap around
    pb.go (1, 1, 40.0);
    pb.go (1, 1, 41.0);
    CHECK_EQ (pb.state (1, 1).cue, 0);
}

static void testPresets()
{
    section ("Presets: groups, colour, by-type fallback, merge, effects, all");
    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    PresetLibrary presets;
    for (int i = 1; i <= 4; ++i) patch.add ({ i, "Par", "builtin.rgbw8", 1, 1 + (i - 1) * 8 }, lib);
    patch.add ({ 101, "Spot", "builtin.spot16", 1, 100 }, lib);

    // group keeps selection order
    prog.setSelection ({ 3, 1, 4 });
    CHECK (presets.store (PresetPool::Group, 1, prog, patch, lib, PresetStoreMode::Overwrite).isEmpty());
    prog.clearSelection();
    CHECK (presets.recall (PresetPool::Group, 1, prog, patch, lib));
    CHECK (prog.selection() == std::vector<int> ({ 3, 1, 4 }));
    prog.setSelection ({ 2 });
    presets.recall (PresetPool::Group, 1, prog, patch, lib, true);      // add to selection
    CHECK (prog.selection() == std::vector<int> ({ 2, 3, 1, 4 }));

    // colour preset stores only colour attributes
    prog.setSelection ({ 1 });
    prog.set (1, "ColorR", 1.0f); prog.set (1, "ColorG", 0.0f); prog.set (1, "ColorB", 0.0f);
    prog.set (1, "Dimmer", 1.0f);
    CHECK (presets.store (PresetPool::Color, 1, prog, patch, lib, PresetStoreMode::Overwrite).isEmpty());
    auto* red = presets.find (PresetPool::Color, 1);
    CHECK (red != nullptr && red->values.at (1).count ("Dimmer") == 0);
    CHECK (red != nullptr && red->swatch == 0xffff0000u);
    CHECK (presets.store (PresetPool::Gobo, 1, prog, patch, lib, PresetStoreMode::Overwrite).isNotEmpty());

    // recall onto a fixture that wasn't stored: mapped by selection position
    prog.clearValues();
    prog.setSelection ({ 4 });
    presets.recall (PresetPool::Color, 1, prog, patch, lib);
    CHECK (prog.get (4, "ColorR").value_or (-1.0f) > 0.99f);
    CHECK (! prog.get (4, "Dimmer").has_value());

    // nothing selected: recalls onto the stored fixtures and selects them
    prog.clearValues(); prog.clearSelection();
    presets.recall (PresetPool::Color, 1, prog, patch, lib);
    CHECK (prog.selection() == std::vector<int> ({ 1 }));
    CHECK (prog.get (1, "ColorG").value_or (-1.0f) < 0.01f);

    // merge adds a fixture without losing the first
    prog.clearValues();
    prog.setSelection ({ 2 });
    prog.set (2, "ColorB", 1.0f);
    presets.store (PresetPool::Color, 1, prog, patch, lib, PresetStoreMode::Merge);
    CHECK (presets.find (PresetPool::Color, 1)->values.size() == 2);

    // a two-fixture "gradient" recalled on other fixtures alternates by position
    prog.clearValues();
    prog.setSelection ({ 3, 4 });
    presets.recall (PresetPool::Color, 1, prog, patch, lib);
    CHECK (prog.get (3, "ColorR").value_or (-1.0f) > 0.99f);      // from fixture 1 (red)
    CHECK (prog.get (4, "ColorB").value_or (-1.0f) > 0.99f);      // from fixture 2 (blue)

    // effects follow the new selection
    prog.clearValues();
    EffectParams e; e.id = 5; e.form = "Sine"; e.fixtures = { 1, 2 };
    prog.setEffect (e);
    CHECK (presets.store (PresetPool::Effect, 1, prog, patch, lib, PresetStoreMode::Overwrite).isEmpty());
    prog.clearValues();
    prog.setSelection ({ 4, 3 });
    presets.recall (PresetPool::Effect, 1, prog, patch, lib);
    CHECK (prog.effects().size() == 1 && prog.effects()[0].fixtures == std::vector<int> ({ 4, 3 }));

    // "All" takes every attribute and effects
    prog.set (101, "Pan", 0.3f); prog.set (101, "Dimmer", 0.5f);
    CHECK (presets.store (PresetPool::All, 1, prog, patch, lib, PresetStoreMode::Overwrite).isEmpty());
    CHECK (presets.find (PresetPool::All, 1)->values.at (101).size() == 2);
    CHECK (presets.find (PresetPool::All, 1)->effects.size() == 1);

    // round trip
    PresetLibrary copy;
    copy.fromVar (juce::JSON::parse (juce::JSON::toString (presets.toVar())));
    CHECK (copy.find (PresetPool::Group, 1) != nullptr && copy.find (PresetPool::Group, 1)->fixtures.size() == 3);
    CHECK (copy.find (PresetPool::Color, 1) != nullptr && copy.find (PresetPool::Color, 1)->swatch == 0xffff0000u);
    CHECK (copy.find (PresetPool::Color, 1)->swatches.size() == 2);                 // red + blue (merged)
    presets.setLabel (PresetPool::Group, 1, 0xff00ff00u);
    copy.fromVar (juce::JSON::parse (juce::JSON::toString (presets.toVar())));
    CHECK (copy.find (PresetPool::Group, 1)->label == 0xff00ff00u);
    CHECK (copy.find (PresetPool::Effect, 1) != nullptr);
}

static void testFanAndFolders()
{
    section ("Fan weights, patch folders, live values");
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.001f; };
    CHECK (near (fanWeight (FanMode::Spread, 0, 5), -1.0f));
    CHECK (near (fanWeight (FanMode::Spread, 2, 5), 0.0f));
    CHECK (near (fanWeight (FanMode::Spread, 4, 5), 1.0f));
    CHECK (near (fanWeight (FanMode::RampUp, 0, 3), 0.0f));
    CHECK (near (fanWeight (FanMode::RampDown, 0, 3), 1.0f));
    CHECK (near (fanWeight (FanMode::V, 0, 5), fanWeight (FanMode::V, 4, 5)));
    CHECK (near (fanWeight (FanMode::Off, 1, 5), 1.0f));
    CHECK (near (fanGradientPosition (FanMode::Spread, 1, 3), 0.5f));

    FixtureLibrary lib;
    Patch patch;
    CHECK (patch.addRange ("builtin.rgb", 3, 1, 1, 1, "Par", lib, "Front truss").isEmpty());
    patch.add ({ 10, "Spot", "builtin.spot16", 1, 100 }, lib);
    CHECK (patch.folders().size() == 1 && patch.folders()[0] == "Front truss");
    patch.setFolder (10, "Back truss");
    patch.renameFolder ("Front truss", "FOH");
    CHECK (patch.find (1)->folder == "FOH" && patch.find (10)->folder == "Back truss");
    Patch reloaded;
    reloaded.fromVar (patch.toVar());
    CHECK (reloaded.find (2)->folder == "FOH");

    // live values: Dimmer before the grand master, Intensity after
    Programmer prog;
    Playback pb;
    std::map<int, double> phases;
    prog.set (1, "Dimmer", 1.0f);
    UniverseMap out;
    std::map<int, std::map<juce::String, float>> resolved;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 0.5f, 1.0f };
    DmxEngine::renderFrame (ctx, out, &resolved);
    CHECK (near (resolved[1]["Dimmer"], 1.0f));
    CHECK (near (resolved[1]["Intensity"], 0.5f));
    CHECK (near (resolved[1]["ColorR"], 1.0f));        // colour reported before the virtual dimmer
    CHECK (near (resolved[10]["Pan"], 128.0f / 255.0f));
}

static void testAudioAndTempo()
{
    section ("Audio links, tap tempo, speed master range");
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.002f; };

    CHECK (near (Masters::speedMultiplier (0.5f), 1.0f));
    CHECK (near (Masters::speedMultiplier (1.0f), 4.0f));
    CHECK (near (Masters::speedMultiplier (0.25f), 0.5f));
    for (float m : { 0.0f, 0.3f, 1.0f, 2.13f, 4.0f })
        CHECK (near (Masters::speedMultiplier (Masters::speedFaderFor (m)), m));

    TapTempo tap;
    CHECK (near (tap.tap (10.0), 0.0f));
    tap.tap (10.5); tap.tap (11.0);
    CHECK (near (tap.tap (11.5), 120.0f));
    CHECK (near (tap.tap (20.0), 0.0f));               // long gap starts again

    FixtureLibrary lib;
    Patch patch;
    Programmer prog;
    Playback pb;
    PresetLibrary presets;
    std::map<int, double> phases;
    patch.add ({ 1, "A", "builtin.rgbw8", 1, 1 }, lib);
    patch.add ({ 2, "B", "builtin.rgbw8", 1, 9 }, lib);
    patch.add ({ 3, "C", "builtin.rgbw8", 1, 17 }, lib);
    prog.setSelection ({ 2, 3 });
    presets.store (PresetPool::Group, 1, prog, patch, lib, PresetStoreMode::Overwrite);
    prog.clearSelection();

    AudioLevels audio;
    audio.bass = 0.5f;
    audio.high = 1.0f;

    std::vector<AudioLink> links;
    AudioLink setDim;  setDim.source = AudioSource::Bass; setDim.mode = AudioLinkMode::Set; setDim.fixtures = { 1 };
    AudioLink scaleDim; scaleDim.source = AudioSource::Bass; scaleDim.mode = AudioLinkMode::Scale; scaleDim.groupNumber = 1;
    AudioLink colour;  colour.source = AudioSource::High; colour.attribute = "ColorG"; colour.mode = AudioLinkMode::Set;
    colour.minimum = 0.0f; colour.maximum = 0.4f; colour.fixtures = { 1 };
    links = { setDim, scaleDim, colour };

    prog.set (2, "Dimmer", 1.0f);
    UniverseMap out;
    RenderContext ctx { lib, patch, prog, pb, phases, 0.0, 1.0f, 1.0f, &links, &audio, &presets };
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 128);                         // fixture 1 dimmer follows bass (0.5)
    CHECK_EQ (out[1][2], 102);                         // fixture 1 green follows highs (0..0.4)
    CHECK_EQ (out[1][8], 128);                         // fixture 2: programmer 100% x bass 0.5 (group link)
    CHECK_EQ (out[1][16], 0);                          // fixture 3 in the group but dark: 0 x scale

    prog.set (1, "Dimmer", 0.2f);                      // programmer overrides an audio "set"
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 51);

    links[0].enabled = false;
    prog.clearValues();
    out.clear();
    DmxEngine::renderFrame (ctx, out);
    CHECK_EQ (out[1][0], 0);

    auto rt = AudioLink::fromVar (juce::JSON::parse (juce::JSON::toString (links[2].toVar())));
    CHECK (rt.attribute == "ColorG" && rt.mode == AudioLinkMode::Set && rt.source == AudioSource::High
           && near (rt.maximum, 0.4f) && rt.fixtures.size() == 1);
}

static void testAudioLinkSmoothing()
{
    section ("Audio link smoothing: attack, release, threshold, curve");
    AudioLink l; l.id = 1; l.attackMs = 10.0f; l.releaseMs = 500.0f; l.threshold = 0.2f; l.curve = 2.0f;
    CHECK (l.shape (0.1f) == 0.0f && std::abs (l.shape (0.6f) - 0.25f) < 0.001f && l.shape (1.0f) == 1.0f);
    AudioLevels audio;
    std::map<int, float> levels;
    std::vector<AudioLink> links { l };
    audio.bass = 1.0f;
    for (int i = 0; i < 5; ++i) DmxEngine::updateAudioLinks (links, audio, 0.01, levels);   // 50 ms of loud
    CHECK (levels[1] > 0.98f);                                     // fast attack
    audio.bass = 0.0f;
    DmxEngine::updateAudioLinks (links, audio, 0.1, levels);       // 100 ms of silence
    CHECK (levels[1] > 0.7f && levels[1] < 0.9f);                  // slow release: still mostly up
    auto rt = AudioLink::fromVar (juce::JSON::parse (juce::JSON::toString (l.toVar())));
    CHECK (rt.releaseMs == 500.0f && rt.threshold == 0.2f && rt.curve == 2.0f);
}

static void testAudioAnalysis()
{
    section ("Audio analysis: bands and beats from generated signals");
    const double sr = 48000.0;
    const int block = 512;
    std::vector<float> buf (block);
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    auto runTone = [&] (double freq, double seconds, AudioLevels& lv)
    {
        AudioAnalysis a;
        a.prepare (sr);
        double ph = 0;
        for (int b = 0; b < (int) (seconds * sr / block); ++b)
        {
            for (auto& s : buf) { s = 0.3f * (float) std::sin (ph); ph += twoPi * freq / sr; }
            a.process (buf.data(), block, lv);
        }
    };

    AudioLevels low, high;
    runTone (60.0, 1.5, low);
    runTone (5000.0, 1.5, high);
    std::cout << "    60 Hz -> bass " << low.bass.load() << " mid " << low.mid.load() << " high " << low.high.load() << "\n";
    std::cout << "    5 kHz -> bass " << high.bass.load() << " mid " << high.mid.load() << " high " << high.high.load() << "\n";
    CHECK (low.bass.load() > 0.8f && low.high.load() < 0.1f);
    CHECK (high.high.load() > 0.8f && high.bass.load() < 0.1f);
    CHECK (low.level.load() > 0.8f);

    // kick drum at 120 BPM: 60 Hz bursts every 0.5 s
    AudioAnalysis a;
    a.prepare (sr);
    AudioLevels lv;
    std::vector<double> beatTimes;
    long long n = 0;
    for (int b = 0; b < (int) (4.0 * sr / block); ++b)
    {
        for (auto& s : buf)
        {
            const double t = (double) n / sr;
            const double sinceKick = std::fmod (t, 0.5);
            s = sinceKick < 0.08 ? 0.8f * (float) (std::sin (twoPi * 60.0 * t) * std::exp (-sinceKick * 30.0)) : 0.0f;
            ++n;
        }
        if (a.process (buf.data(), block, lv))
            beatTimes.push_back ((double) n / sr);
    }
    std::cout << "    beats detected: " << beatTimes.size() << "\n";
    CHECK (beatTimes.size() >= 7 && beatTimes.size() <= 9);
    if (beatTimes.size() >= 3)
    {
        const double interval = (beatTimes.back() - beatTimes[1]) / (double) (beatTimes.size() - 2);
        CHECK (std::abs (interval - 0.5) < 0.03);
    }
}

static void testShowFileRoundTrip()
{
    section ("Show file round trip");
    DmxEngine a;
    juce::String typeId;
    {
        const juce::ScopedLock sl (a.dataLock);
        typeId = a.library.createEmpty();
        a.library.findMutable (typeId)->name = "My LED Bar";
        a.patch.add ({ 5, "Bar", typeId, 3, 10 }, a.library);
        a.patch.add ({ 6, "Dim", "builtin.dimmer", 1, 1 }, a.library);
        a.patch.setLayoutPosition (5, 120.0f, 40.0f);

        EffectParams e;
        e.id = 77; e.form = "Pulse"; e.fixtures = { 6, 5 }; e.mirror = true;
        a.playback.store (2, 3, ValueMap { { 6, { { "Dimmer", 0.5f } } } }, { e }, StoreMode::Add, 0);
        a.playback.setCueProperties (2, 3, 0, "Intro", 1.5f, 3.0f);
        a.programmer.setSelection ({ 6, 5 });
        a.presets.store (PresetPool::Group, 4, a.programmer, a.patch, a.library, PresetStoreMode::Overwrite);
        a.presets.rename (PresetPool::Group, 4, "Everything");
        a.patch.setFolder (5, "Bars");
    }
    a.masters.grand = 0.75f;
    a.masters.movementSpeed = 0.8f;

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("phase_test.phase");
    CHECK (showfile::save (a, file).isEmpty());

    DmxEngine b;
    CHECK (showfile::load (b, file).isEmpty());
    {
        const juce::ScopedLock sl (b.dataLock);
        CHECK_EQ ((int) b.patch.fixtures().size(), 2);
        auto* bar = b.patch.find (5);
        CHECK (bar != nullptr && bar->universe == 3 && bar->address == 10);
        CHECK (bar != nullptr && bar->hasLayoutPosition && std::abs (bar->layoutX - 120.0f) < 0.01f);
        auto* type = b.library.find (typeId);
        CHECK (type != nullptr && type->name == "My LED Bar");
        auto* ex = b.playback.find (2, 3);
        CHECK (ex != nullptr && ex->cues.size() == 1);
        CHECK (ex != nullptr && ex->cues[0].name == "Intro" && std::abs (ex->cues[0].fadeTime - 1.5f) < 0.01f
               && std::abs (ex->cues[0].fadeOut - 3.0f) < 0.01f);
        CHECK (ex != nullptr && ex->cues[0].effects.size() == 1 && ex->cues[0].effects[0].mirror
               && ex->cues[0].effects[0].fixtures[0] == 6);
        auto* g = b.presets.find (PresetPool::Group, 4);
        CHECK (g != nullptr && g->name == "Everything" && g->fixtures.front() == 6);
        CHECK (b.patch.find (5)->folder == "Bars");
    }
    CHECK (std::abs (b.masters.grand.load() - 0.75f) < 0.001f);
    CHECK (std::abs (b.masters.movementSpeed.load() - 0.8f) < 0.001f);
    file.deleteFile();
}

static void testLoopbackSend()
{
    section ("Art-Net over UDP (loopback)");
    juce::DatagramSocket rx (false);
    rx.setEnablePortReuse (true);
    if (! rx.bindToPort (artnet::port, "127.0.0.1"))
    {
        std::cout << "  (skipped: port 6454 busy)\n";
        return;
    }

    NetSettings s;
    s.outputs = { ArtNetOutput { true, "127.0.0.1", false, "127.0.0.1", 1, 0, -1 } };

    ArtNetSender sender;
    sender.configure (s);

    UniverseMap frame;
    frame[1].fill (0);
    frame[1][0] = 42;
    frame[300][511] = 7;
    CHECK_EQ (sender.send (frame), 2);

    int received = 0;
    bool sawUni1 = false, sawUni300 = false;
    juce::uint8 buf[600];
    for (int i = 0; i < 10 && received < 2; ++i)
    {
        if (rx.waitUntilReady (true, 200) != 1) continue;
        const int n = rx.read (buf, sizeof (buf), false);
        if (n == 530 && artnet::opCodeOf (buf, n) == 0x5000)
        {
            ++received;
            const int pa = buf[14] | (buf[15] << 8);
            if (pa == 0 && buf[18] == 42) sawUni1 = true;
            if (pa == 299 && buf[18 + 511] == 7) sawUni300 = true;
        }
    }
    CHECK_EQ (received, 2);
    CHECK (sawUni1);
    CHECK (sawUni300);
}

//==============================================================================
int main()
{
    juce::MessageManager::getInstance();
    std::cout << "Phase engine tests\n";
    // keep the tests away from this machine's fixture library
    const auto testLibrary = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("phase-test-library.phaselib");
    testLibrary.deleteFile();
    setenv ("PHASE_LIBRARY_FILE", testLibrary.getFullPathName().toRawUTF8(), 1);

    testArtDmxPacket();
    testArtPoll();
    testPatch();
    testRender();
    testFixtureBuilder();
    testEffectPhases();
    testEffectsRender();
    testColourAndAttributeEffects();
    testCells();
    testPixelMap();
    testPixelsAndForms();
    testChaseAndButtons();
    testGdtf();
    testMovementAndFly();
    testUndo();
    testTemplatesAndLibrary();
    testGdtfShareClient();
    testCommandLineAndHighlight();
    testCueFolders();
    testEffectTracking();
    testProDjLink();
    testRoutesAndDmxTest();
    testNextPrevLowlight();
    testCopyMoveAndPixelSets();
    testStage3D();
    testPlayback();
    testPresets();
    testFanAndFolders();
    testAudioAndTempo();
    testAudioAnalysis();
    testAudioLinkSmoothing();
    testShowFileRoundTrip();
    testLoopbackSend();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    juce::MessageManager::deleteInstance();
    return failures == 0 ? 0 : 1;
}
