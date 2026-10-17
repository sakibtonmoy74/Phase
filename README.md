# Phase

A lighting console for macOS (Windows next), built in C++ with JUCE.
It follows the grandMA2/MA3 way of working: patch fixtures, select them,
set attributes in the programmer, and send DMX out over Art-Net.

## New in v0.8.5

- **Pro DJ Link** (the **DJ Link** page): Phase joins the CDJ network as a virtual player and shows every deck:
  - Deck number, PLAY / MASTER / ON AIR / SYNC, title, artist, key, source (player and USB / SD), tempo (with pitch) and pitch %, elapsed / remaining time, beat in the bar.
  - The **whole-track overview** (played part dimmed) and a **scrolling colour waveform** around the playhead with the **beat grid** (downbeats in red); the slider sets how many seconds are shown.
  - **Lights follow the master tempo**: the Dimmer FX speed follows the master deck's BPM (pitch included).
  - **Setup**: CDJs / XDJs and the Mac on the same network (a switch or the DJM's link ports), choose that network's **Interface**, press **Connect**. *Phase as player* Auto takes a free player number 1-4 (the players only hand out track data to player numbers 1-4; with 4 real players Phase borrows another player's number for the requests). Track data needs rekordbox-analysed tracks (USB / SD); a fetch that fails is tried again.
  - Colour waveforms come from nxs2 / CDJ-3000 analysis; older players get the blue / white ones. CDJ-3000 position packets are used when present (exact playhead); otherwise the playhead comes from the beat number and the beat grid.
  - Built from Deep Symmetry's open DJ Link protocol analysis. Not yet tried with real players: rekordbox running on the same Mac takes the DJ Link ports, so close it first.

## New in v0.8.4

- **Art-Net outputs, redone** (Network page): a list of outputs, one per line: **On**, **Interface**, **Send to** (Broadcast or Node IP), **Node IP**, **Start universe** (the first Phase universe it gets), **Universes** (how many it outputs; empty = all from the start) and **Art-Net start** (the node's own universe for its first output; empty = same numbering as Phase). **+ Add output** adds a line (the same interface as many times as you like, one per node), **Delete** removes one (asks first). The status line says where everything goes and warns about patched universes no output covers.
- **Art-Net monitor** (Network page, like Artnetominator): **Listen** shows every Art-Net universe on the network (Phase's own output included) with its source IP and packet rate; click one to see its 512 values live.
- **Nodes**: *Discover nodes* lists the nodes with their output universes; click one to add it as an output (node IP, interface on its network, its universes).
- **Re-patch** (Patch page): select fixtures, *Re-patch...*, give a start universe / address (and optional free channels between fixtures): they're re-addressed one after another in FID order, going on to the next universe when one doesn't fit. Nothing changes if they would overlap fixtures that stay put.
- **3D view selection**: **Select** tool (drag a box around fixtures; the box order follows the drag direction for effects; Shift / Cmd adds), **All**, **None**, **Groups** (pick a group). Without the Select tool, Shift-drag on empty space draws a box too; Alt-drag orbits in Select mode.
- **Controller button LEDs** (X-Touch and other mapped controllers): executors with cues light their buttons (the first and the one under the fader; all four while running), so you see which faders are used when you change pages. With **Store** pressed, empty executors blink (used ones stay lit: they get a new cue). With **Copy / Move**, the used executors blink until you pick one, then the empty ones blink as places to go; **Delete** blinks the used ones. (The faders themselves have no lights on the X-Touch.)

## New in v0.8.3

- **Store, Copy and Move with the controller's executor buttons**: press Store (or Copy / Move / Delete), then an executor button on the X-Touch (or any mapped controller). An empty executor records straight away; a used one pops up the menu with *Add as cue N* first. Copy / Move: press the source executor's button, then the destination's.
- ~~Art-Net per universe~~ (replaced by the outputs list in v0.8.4): every patched universe gets a row: send it or not, which **node IP** it goes to (type it, or pick a discovered node; several IPs separated by commas; empty = the send mode above), and the node's **Art-Net universe** (Port-Address; empty = automatic). *+ Universe* adds one that isn't patched yet. Discovered nodes now show their output universes.
- **DMX tester** (DMX page): select channels (click, drag, Shift-click a range, Cmd-click to add, *Select fixture*, *Select all 512*), then set a level (slider, Full, 50%, 0) or **chase** a level through them at a set speed. Tester channels go out on top of the show (in any universe, patched or not; type a universe number in the list) and are marked red until you **Release** them (*Release all* clears everything). The DMX page shows the Port-Address each universe really goes to.

## New in v0.8.2

- **Fine** (programmer button, or push an encoder on the controller): programmer encoders, on-screen knobs, Effects knobs and masters move 10x finer, for exact positions.
- **Lowlight colour wheel**: **LL colour** now also sets a colour wheel slot for fixtures that only have a colour wheel (no RGB / CMY): slide the DMX value (you see it live), or take it from one of your Color presets.
- **Store** and **Clear** are double-width buttons.
- **Empty executors' faders sit at 0**; storing a new executor brings its fader to full. Deleting an executor puts its fader back to 0.
- **The FX size master is now the Movement size master** (MOVE SIZE): it only scales movement (and Fly movement), no longer dimmer or colour effects.
- **3D view handles** (Place): drag the red / green / blue arrows to move the selection along X, height or Z, and the yellow ring to turn it around its centre (5 degree steps, Shift: free). New Place tools: **Align X / Y / Z** (to the first selected), **Even X / Y / Z**, **Line**, **Circle**, **Arc**, **Spread + / -**, **Mirror X**, **Face centre**, plus Hang, Floor, Front, Turn +45 and From layout.
- **Layout**: right-click → **Arrange selection**: line, grid, circle, align left / centre / right / top / middle / bottom, space evenly, spread out / closer, rotate, flip (also on the Edit layout tools row).
- **MIDI**:
  - **MIDI monitor** on the MIDI page: the last messages your controller sent (moving controls update their line), to see exactly what each knob, button and fader sends.
  - **Standard MIDI X-Touch preset**: the right-hand encoders turn the Effects knobs (they don't send turns in Mackie mode, where the top knobs switch layers instead). Faders now only count while your finger is on them once the touch sensor has been seen, so the motor can't make them jump. Encoder pushes toggle Fine.
  - **Mackie preset**: pushing a top knob toggles Fine.

## New in v0.8.1

- **Next / Prev / All** (programmer, or `.` `,` `/` on the keyboard, or `next` / `prev` / `all` in the command line): step through the selection one fixture at a time; All brings the whole selection back.
- **Lowlight** (button or `L`) with its colour picker (**LL colour**: colour wheel; the brightness slider is the lowlight level). The selection shows in that colour. With **Highlight + Next / Prev**, the current fixture is highlighted and the rest of the selection sits in the lowlight colour: find each fixture, fix its position, Next. The colour is remembered on this computer.
- **Copy / Move / Delete** (programmer buttons; Delete / Backspace key for Delete): press, tap the thing, then (Copy / Move) where it goes. Works on executors (move a fader to another slot: taking a used slot swaps them), presets and groups (any pool; moving a preset keeps the cue folders that use it), and cues (copied / moved after the cue you tap). Delete always asks first, and Cmd+Z undoes. Press again, Clear or Esc to cancel.
- **Layout groups**: select fixtures in the Layout, right-click → *Create group from selection* (or **+ Group**), name it, and it appears as a button above the rig. Tap a group button to select it (Shift adds), right-click it to update it with the selection, rename or delete. They are the same groups as in the Presets view.
- **Delete fixtures in the Layout**: Delete, then tap a selected fixture (or right-click → *Delete N fixtures*): *Remove from the Layout* (stays patched; right-click → *Show hidden fixtures* brings them back) or *Delete from the patch*. Both ask first.
- **X-Touch Compact in Mackie Control mode** (MIDI → *MIDI maps...* → *Controller presets*). Faders use 14-bit pitch bend and the touch sensors, so they're smooth and the motors don't fight you. Loading the preset asks the X-Touch to switch to MC mode (or hold MC while powering it on):

  | X-Touch (MC mode) | Phase |
  |---|---|
  | Faders 1-8 | Executor faders 1-8 of the current page (motorized, touch-sensitive) |
  | Main fader | Grand master |
  | Top knobs 1-8 | Programmer encoders 1-5 + Dimmer FX speed, Movement FX speed, FX size |
  | Push right-hand encoder 1 / 2 / 3 | Top knobs = Programmer / Effects knobs 1-8 / Effects knobs 9-16 (LED shows which) |
  | Push right-hand encoder 4 / 5 / 6 | Highlight / Lowlight / All |
  | Right-hand stepping encoders | Bank: page - / +, Channel: Prev / Next |
  | REC, SOLO, MUTE rows | Executor buttons 1, 2, 3 (Go, Back, Off by default) |
  | SELECT row (under the faders) | Executor button 4 (Flash by default) |
  | Button under the main fader (Flip) | Blackout |
  | Right-hand buttons | Marker: Tap, Nudge: Fan, Cycle: Highlight, Rewind / Forward: Prev / Next, Stop: Clear, Play: Store, Record: Lowlight |

  In MC mode the right-hand encoders don't send turns (only pushes), so the top knobs switch layers to reach the Effects panel. The right-hand buttons' MC names aren't printed on the X-Touch: if one does something else than listed, look at *Last message* on the MIDI page and relearn it. The Standard MIDI preset is still there (now with touch sensing and a stronger motor guard against jumping).
- **Save MIDI maps**: *MIDI maps...* → *Save current map as...* keeps your map on this computer (*Load saved map*), and *Export* / *Import* `.phasemidi` files to move maps between machines.
- **Fixture Builder**:
  - **Rings...** in *Pixel layout*: any number of rings and how many pixels each, outer first (e.g. `24, 16, 8, 1`; 1 = a centre pixel), clockwise or not. A different total changes the fixture's pixel count. *Starting at pixel* lays out only part of the pixels (e.g. just the white set).
  - **+ Pixel set...**: fixtures with two kinds of pixels, like RGB pixels plus separate white pixels: adds N cells (White, RGB, RGBW, Amber, UV, warm / cold white or single-colour dimmer) after the existing ones, with their own channels, beside the existing pixels or in a row under them. White-only pixels take the white part of colours (pixel maps, colour effects, lowlight); highlight lights them all.

## New in v0.8

- **Command line** (the CMD row above the programmer, or just start typing a number):
  - `1 thru 10` selects (in that order; `10 thru 1` reverses), `1 thru 5 + 8 - 3` adds and removes, `301.1 thru 301.8` selects pixels, `group 3` selects a group.
  - `at 50`, `at full`, `at 0` set the dimmer; **`at 0 thru 100` spreads the values across the selection** in selection order (`at 100 thru 0 thru 100` too).
  - Any attribute: `pan at 25 thru 75`, `tilt at 40`, `zoom at 0 thru 100`, `red at 100`...
  - `1 thru 8 at 100 thru 0` selects and sets in one go; `preset 4.2` recalls a preset (1 dimmer, 2 position, 3 gobo, 4 color, 5 beam, 6 focus, 7 effect, 8 all); `highlight`, `clear`. Up / down arrows bring back earlier commands.
  - **Right-click any programmer knob** to type its value (or a spread like `0 thru 100`).
- **Full** and **Dim Off** buttons for the selection.
- **Highlight** (programmer button or `H`): the selection goes full, open white, shutter open, above everything; pan / tilt / zoom stay. The Fixture Builder has a **Highlight** column to set your own highlight value per channel (empty = automatic). **Patch** has a Highlight button too: it highlights the rows you select there, to find fixtures in the rig.
- **Pixel layouts**: Fixture Builder → *Pixel layout...* opens a mini grid where you drag each pixel to where it really is. Ring, 2 rings, snake (zig-zag), columns, flip, rotate and reverse are one click. Layout, pixel maps and the 3D view all use the shape. New template: **LED Ring (pixels)**.
- **3D view** (the 3D tab, also on extra screens): fixtures with live beams (colour, intensity, pan / tilt, zoom) and pools on the floor, so you can preview looks and effects. Drag to orbit, right-drag to pan, scroll to zoom; Front / Top / Side / Frame. **Place** lets you drag fixtures (Shift: height) or type X / Y / Z and mount angles, with Hang, Floor, Face front, From layout, Even X and Turn +45. Positions start from the 2D layout, hung at 4 m.
- **Cue folders** (preset references): store a cue after recalling a **group** and **presets**, and the cue keeps "Group 3: Position 2, Color 5" instead of copies of the values. Update the preset and every cue that uses it changes; add a fixture to the group and it joins the cues. Right-click a cue → *Folders...* shows them: pick the group, the dimmer and the Position / Color / Gobo / Beam / Focus / Effect / All presets per folder, or make a folder from the selection.
- **Audio links**: each link has **Attack**, **Release**, **Gate** (noise threshold) and **Curve**, so links follow the music smoothly instead of flickering. The link meter shows the raw level and the smoothed one.
- **Tilt direction in effects**: "up" now means more tilt everywhere: Fly Up / Fly Out lift the beams, and the Layout's pan / tilt dot moves up with tilt.
- **Effects track through the cue list** (with Tracking on): an effect keeps running in later cues until a later cue replaces it (a new effect of the same kind on those fixtures) or stops it. Cue 1 with movement + dimmer effects, then cue 2 with a colour effect, now runs all three in cue 2. The phase carries on across cues, no restart. Each cue row lists the effects running in it. To stop a tracked effect from a cue on, right-click the cue.
- **Store from the cue list**: **Store** on each cue row (the row is just name, fades, Store, Go, Delete) (or press Store, then tap a cue row) gives *Merge into*, *Overwrite* (keeps the cue's number, name and times) or *Insert new cue after* (numbered between, e.g. 1.5).
- **X-Touch Compact touch faders**: the preset now reads the faders' touch sensors (Standard MIDI mode sends CC 101-109 while a finger is on a fader). While you touch a fader the motor isn't driven, so it never fights your hand, and every move counts; when you let go it settles to the real value (e.g. after a page change). Loading the preset also puts the X-Touch into Standard mode, Layer A. MIDI learn picks up any fader's touch message by itself: touch, then move. No Mackie Control mode needed.
- **Fixes**: drawing a pixel map no longer moves fixtures that had not been placed by hand.

## New in v0.7

- **Undo / Redo**: **Cmd+Z** undoes, **Cmd+Shift+Z** (or Cmd+Y) redoes, as many steps as you like (up to 200), also with the Undo / Redo buttons in the top bar. It covers the patch, fixture types, cues and executors, presets and groups, layout, pixel maps, audio links and the programmer (values, effects, selection). Running playback keeps running; fader moves are not undo steps. A knob drag is one step.
- **Invert Pan / Invert Tilt** in Patch (toolbar buttons and in *Selected fixture*), per fixture, for fixtures hung the other way round. The output is inverted, your programming stays the same, so a mirrored rig moves together. A *Pan / Tilt* column shows which are inverted.
- **Patch type search**: type in *Search* (manufacturer, name or channel count) to narrow the fixture type list.
- **Movement effects**:
  - **Mirror now works for movement**: the mirrored half pans the other way, so circles, pans and figure 8s are truly symmetrical (wings with Mirror on, too).
  - New shapes: Square, Triangle, Diamond, Swing, Ballyhoo; separate **Pan size** and **Tilt size**; **Reverse** runs the shape the other way.
- **Fly effects** (Effects → Fly): movement and dimmer together. **Fly Out** (fixtures fan out and up while they flash), **Fly In**, **Fly Up**, **Fly Fan**. *Lit time* sets how much of each cycle the light is on; Low / High, Pan size, Tilt size, phase, groups, wings and Reverse all apply. Timed by the Dimmer FX speed (tap tempo).
- **Fixture Builder**:
  - **New** asks what kind of fixture you're building and starts from a template: dimmer, LED pars (RGB / RGBW / RGBWA+UV), moving head spot, CMY profile, wash, beam, LED bar or pixel panel (asks for the pixel count), strobe, blinder, fog.
  - **Sensible defaults**: Pan and Tilt start in the middle (128), shutter open, zoom and focus mid; changing a row's attribute brings its usual default unless you set one by hand.
  - **Import library...** takes a Phase library (`.phaselib`), another show (`.phase`), `.gdtf` files or a whole folder of them. **Export library...** saves your custom types to a `.phaselib`.
  - Your custom and imported fixture types are also kept in a machine library, so every show (and New show) has them.
- **GDTF Share login fixed**: it now signs in the way GDTF Share expects (a form post). Use your GDTF Share *user name*. Errors now say what the server answered.
- **NDI in pixel maps**: the map panel shows the NDI status and lists NDI sources as they're found. Phase looks for the NDI runtime in more places (NDI Tools, NDI apps in /Applications, Homebrew) and keeps looking every 10 s, so installing NDI Tools while Phase runs works. *NDI source by name...* lets you type a source that isn't discovered (another subnet).
- **Behringer X-Touch Compact preset** (MIDI → *MIDI maps...* → *Controller presets*; see v0.8.1 for Mackie Control mode). Standard MIDI mode, factory Layer A, global channel 1:

  | X-Touch | Phase |
  |---|---|
  | Faders 1-8 | Executor faders 1-8 of the current page (motorized, follow the page, touch-sensitive) |
  | Main fader | Grand master |
  | Top encoders 1-5 | Programmer encoders 1-5 (the numbered knobs) |
  | Top encoders 6, 7, 8 | Dimmer FX speed, Movement FX speed, FX size |
  | Right-hand encoders (8) | Effects panel knobs 1-8, left to right |
  | Button rows 1, 2, 3 above each fader | Executor buttons 1, 2, 3 (Go, Back, Off by default) |
  | Button under each fader | Executor button 4 (Flash by default, or Swop / Temp...) |
  | Button under the main fader | Blackout |
  | Right-hand buttons 1-6 | Page -, Page +, Tap, Store, Clear, Fan |

  The encoders are used as endless knobs (they keep going at the ends of their range), their LED rings show the value, button LEDs show what's on, and the motor faders follow the page. You can relearn any cell afterwards.
- **MIDI**: up to 8 programmer encoders, 8 Effects-panel knobs, and every encoder or master can be absolute, relative, or "absolute as endless".

## New in v0.6

- **Edit layout tools** (press *Edit layout* for the tools row): Line, Grid, Circle, Auto; **Rot -90 / Rot +90** (Shift-click turns 15°); **Flip H / Flip V**; align **Left, Centre, Right, Top, Middle, Bottom**; **Even H / Even V** spaces the selection evenly; **Tighter / Wider** pulls the selection together or spreads it out. Pixel fixtures turn and mirror their cell grid with the fixture, and the pixel map follows.
- **Pixels**: turn on *Pixels* in Layout to click or lasso single cells of pixel fixtures. They work like fixtures: lasso order is the phase order, knobs set just those pixels, presets and groups store them, and every effect (dimmer, colour, attribute) runs across pixels. *All* selects every pixel while Pixels is on.
- **Pixel dimmers**: each cell with colour but no dimmer channel gets a virtual dimmer (full by default), so dimmer chases work pixel by pixel, also on fixtures without a real dimmer. Setting pixels alone opens the fixture.
- **Effects as a pixel map source**: pick *Effect* as a map's source and choose a colour or dimmer effect, colours A/B, BPM, waves across the map, width and direction (left to right, right to left, top to bottom, bottom to top, centre out, outside in, diagonal, clockwise). It follows the Dimmer FX speed master.
- **More colour effects**: A-Black-B-Black, A-B-Black, Pulse A-B (A pulses, black, B pulses, black), Pulse A, Random Hue.
- **Cue chase**: set an executor to *Chase* (Cues view, or right-click the executor name). The cues step by themselves with the **Dimmer FX speed** master and tap tempo: 1/4 to 4 steps per beat, snap or crossfade, Loop / Bounce / Random. Its fader can be set to **Speed** (50 % = normal), so the fader controls the chase rate.
- **Executor button functions**: each executor's four buttons can be **Go, Back, Off, Flash, Swop** (full, all other executors' intensity out while held), **Temp** (on at fader level while held), **Toggle**, **Pause** (chase), **Select** (selects the cue's fixtures), **Rate Up / Rate Down** or None. Set them in the Cues view or by right-clicking the executor name. MIDI buttons 1-4 do whatever the button is set to.
- **GDTF**: *Import GDTF...* in the Fixture Builder adds fixture types from a `.gdtf` file (pick one mode or all). *GDTF Share...* logs in with your free GDTF Share account, searches the whole library and imports with a double-click. Pixel fixtures (repeated geometries) become multi-cell types. The list is cached so you can search offline; your password is never saved.
- **Groups from folders** (Patch): makes a group for every patch folder, named like the folder; press it again to update them.
- **Fixture Builder** scrolls by itself when you drag a row to the top or bottom edge.

## New in v0.5

- **Cue fades that work**: every cue has **In** and **Out** fade times (Cues view). Intensity rising uses In, falling uses Out; pulling an executor's fader to zero or pressing Off releases it over the Out time. Each executor has a **Tracking** switch: on, values carry through the cue list; off, each cue plays only what it contains.
- **Color effects** (Effects → Color): Rainbow, A-B Fade, A-B Chase, A-B Ramp with two colour swatches from the picker; phase, groups, blocks, wings and mirror all apply.
- **Attribute effects** (Effects → Attrib): pick any attribute of the selection (Zoom, Focus, Iris, Gobo rotation...) and run a sine, ramp, square, pulse or random wave on it between Low and High.
- **Pixel map**: in Layout press **Pixel map**, draw a box over the fixtures, choose its source. Sources are built-in test patterns (Rainbow, Gradient, Bars, Plasma) and **NDI In**. Each map has intensity, Enabled and *Colour only* (leave the dimmer alone). Maps are saved with the show.
  - NDI needs the free **NDI Tools** (ndi.video/tools) installed on the Mac; Phase loads the NDI runtime by itself. Without it the test patterns still work.
- **Multi-cell fixtures**: the Fixture Builder has **Cells X × Y**, a Cell column per attribute and *Repeat cell 1 for all cells*. Layout draws these fixtures as a grid of pixels, each cell showing its own colour, and pixel maps sample every cell. Built-in examples: *LED Bar 8px RGB* and *Pixel Panel 4x4 RGB*.
- **Extra screens**: the **Screens** button opens Screen 2 and Screen 3 as separate windows (drag them to other displays). Each splits into 1–4 panes, and each pane shows any view (Layout, Presets, Cues, Patch, Effects, Executors, Audio, MIDI, DMX, Network). The programmer stays on the main screen. Open screens and their panes come back on the next launch.
- **Colour picker** shows every selected fixture as a numbered dot joined by a line, so you can see a fan or spread at a glance.
- **Colour presets** fill their tile with the stored colour (a gradient shows as bands). **Rename / colour label** adds a coloured stripe for organising the pools.
- **Fit** in Layout now frames the whole rig (and does so on first open).
- **App icon** and logo in the top bar.

## Since v0.4

- **MIDI** page: enable your controller's inputs and a feedback output, click a cell and move a control to map it (right-click clears). Map executor **faders, Go, Back, Flash, Off** (they follow the page on screen; motorized faders move to the new page), **Encoders 1-4** (absolute or relative; they control the first four attributes shown in the programmer, numbered on the knobs), the **masters**, and keys (**Tap, Blackout, Page +/-, Store, Clear, Fan**). CC, notes and pitch bend (Mackie-style faders) all work. Mappings are saved with the machine, not the show.
- **Audio** page: choose the sound card input; meters for **Level, Bass, Mid, High, Beat** with auto gain. **Audio links** connect a band to any attribute on the selected fixtures or a stored group: *Scale* (an audio master, e.g. bass pumping intensity) or *Set* (the attribute follows the audio), with a min-max range. "Beats tap the dimmer FX tempo" syncs effects to the music.
- **Tap tempo** (TAP key, `T`, or MIDI): sets the **Dimmer FX speed** master, which now reads in BPM (a 60 BPM effect runs at the tapped tempo). Speed masters go up to 400 %.
- **Layout selection order**: *Order: Drag* (drag direction) or *Order: FID* (fixture ID), plus **Sort by FID** for the current selection.
- **Presets** page (MA-style pools): **Groups** (keep selection order), **Dimmer**, **Position**, **Gobo**, **Color**, **Beam**, **Focus**, **Effects**, **All**. Tap an empty tile to store, tap a stored one to apply to the selection (with nothing selected it recalls onto its own fixtures). On other fixtures, values map by selection position, so a stored gradient lands as a gradient. Store + tile → Overwrite / Merge. Right-click → rename / delete.
- **Colour picker** (Color feature group): colour wheel, brightness, gel swatches; RGBW gets white extracted automatically, CMY fixtures work too.
- **Fan** (`F` or the Fan key): Spread (centre fixed, ends opposite), Ramp >, Ramp <, V. Turning any encoder spreads the change across the selection in selection order; picking a colour with Fan on makes a gradient. Encoders show the range of a fanned selection (e.g. 17-83%).
- **Encoders show the live value** of the fixture (including playback and effects), with a tick at the default; the arc is drawn from the default, so pan/tilt sit in the middle.
- **Patch folders**: New folder / Move to folder / Rename folder; collapse with the arrow, double-click a folder to select its fixtures. The **Fixture Builder** opens from a button in Patch.
- **Layout** shows each fixture's colour as a ring, even at zero intensity.

**Screen**: main view on top (Layout, Presets, Cues, Patch, Audio, MIDI, DMX, Network) with the Effects editor docked on the right; the programmer and the executor page live at the bottom, MA-style.

- **Layout view**: drag fixtures to match the real rig (Edit layout), arrange the selection as a line, grid or circle. Live intensity, colour and pan/tilt. **Lasso selection follows your drag direction**: drag left→right and fixture 1 is leftmost; drag right→left to reverse. Badges show the selection order, which is the phase order for effects.
- **Programmer**: feature groups (Dim, Pos, Gobo, Color, Beam, Focus, Ctrl) and a **rotary knob per attribute**; "Off" knocks an attribute out.
- **Effects** (MA2-style):
  - **Dimmer**: Sine, Ramp Up/Down, Square, Pulse, Random — Low, High, Width.
  - **Movement**: Circle, Pan, Tilt, Figure 8, Diagonal — Size, around the current position.
  - Both: Speed (BPM), Phase start→end spread in selection order, Groups, Blocks, Wings and **Mirror** (symmetrical from the centre). Edits are live while the effect runs.
- **Store → executor**: press **Store** (or `S`), tap an executor. Empty executor → Cue 1. Otherwise choose **Add cue**, **Merge** or **Overwrite**. Values track through the cue list.
- **Executors**: 10 per page, unlimited pages. Fader (HTP intensity master, auto start/stop), **Go**, **Back**, **Flash**, **Off**. Tap an executor's name to see its cue list.
- **Cues view**: rename cues, set fade times, jump to a cue, delete cues or the executor.
- **Masters**: Grand, **Dimmer FX Speed**, **Movement FX Speed** (both 0–200 %), **FX Size**, Blackout.
- **Fixture builder**: one row per attribute with its **DMX channel** and **fine channel** for 16-bit; **drag rows to reorder**; "Keep channels in row order" renumbers automatically (turn it off to type your own); a **channel map** shows what each DMX channel carries; spare channels; validation for clashes.
- **Art-Net**: unlimited universes, broadcast or unicast, node discovery. **DMX monitor**, **show files** (`.phase`) and autosave. v0.1 shows open fine.

### Recording a look, start to finish

1. **Layout**: drag across the fixtures in the order you want (e.g. left→right).
2. **Programmer**: pick a feature group, turn the knobs.
3. **Effects**: pick Dimmer or Movement, a form, press **Apply to selection**. Toggle **Mirror**, adjust phase, speed, size live.
4. Press **Store**, tap an executor's **Go** (or its name). Choose Add / Merge / Overwrite if it already has cues.
5. **Esc** twice clears the programmer. Run it with the executor's fader and **Go**.

### Keys

`0-9` start a command · `H` Highlight · `Cmd+Z` Undo · `Cmd+Shift+Z` Redo · `Esc` Clear (store mode → selection → programmer) · `S` Store · `F` Fan mode · `T` Tap tempo · `B` Blackout · `E` Effects panel · `[` `]` page down/up · `Cmd+A` select all · `Cmd+1…6` views · `Cmd+S` / `Cmd+Shift+S` / `Cmd+O` / `Cmd+N` file.

---

## Building on macOS

### 1. One-time setup

Open **Terminal** and run:

```bash
# Apple's compiler and git
xcode-select --install

# Homebrew (skip if you already have it): https://brew.sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# CMake (the build tool)
brew install cmake
```

### 2. Build

```bash
cd ~/Downloads/Phase          # wherever you unzipped it
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```

The first `cmake -B build` downloads JUCE (about 150 MB), so it needs internet and takes a few minutes. Later builds only recompile what changed.

The app ends up at:

```
build/Phase_artefacts/Release/Phase.app
```

Drag it to Applications, or run it straight from there:

```bash
open build/Phase_artefacts/Release/Phase.app
```

**Universal app (Apple Silicon + Intel)**: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the first command.

**Prefer Xcode?** `cmake -B build-xcode -G Xcode`, then open `build-xcode/Phase.xcodeproj`, pick the *Phase* scheme and press Run.

### 3. First launch

- The app isn't code-signed yet, so the first time **right-click Phase.app → Open → Open**.
- macOS will ask to let Phase **find devices on your local network**. Click **Allow**, or Art-Net won't reach your node. (If you missed it: System Settings → Privacy & Security → Local Network → Phase.)

### 4. Run the engine tests (optional)

```bash
./build/PhaseTests_artefacts/Release/PhaseTests
```

It should end with `491/491 checks passed`.

The MIDI mapping can also be checked on any machine with `./build/Phase_artefacts/Release/Phase.app/Contents/MacOS/Phase --midi-selftest`.

---

## Connecting your Art-Net node

1. Plug the node into the Mac (directly or through a switch).
2. Give the Mac's Ethernet port an address in the node's range. Most nodes ship on `2.x.x.x`:
   System Settings → Network → Ethernet → Details → TCP/IP → Configure IPv4: **Manually**, IP `2.0.0.100`, Subnet `255.0.0.0`.
3. In Phase open **Network**, pick that interface (`2.0.0.100`), and press **Discover nodes** to confirm the node answers.
4. Leave **Broadcast on interface**, or switch to **Unicast** and type the node's IP.
5. **Universe 1 =** `0` means Phase universe 1 goes to Art-Net 0:0:0, universe 2 to 0:0:1, and so on. Match it to how the node's ports are set.

The green dot in the top bar shows packets going out.

---

## Project layout

```
Source/Engine/   no UI code: fixtures, patch, programmer, effects, playback, DMX render, Art-Net, show files
Source/UI/       JUCE views: Layout, Effects, Programmer bar, Executor bar, Cues, Patch, Fixture Builder, DMX, Network, Screens
Source/Control/  MIDI, audio input, NDI input, GDTF Share client
ThirdParty/NDI/  NDI SDK headers (the runtime is loaded from NDI Tools at run time)
Tests/           headless engine tests
```

The engine runs on its own real-time thread and never waits for the UI, which keeps the output steady and makes the Windows port and a future remote (iPad/web) straightforward.

## Roadmap

1. ~~Art-Net output, patch, fixture builder, programmer~~ (v0.1)
2. ~~Layout view, knob programmer, effects, cues, executors, pages, masters~~ (v0.2)
3. ~~MIDI mapping with motor-fader feedback~~, ~~audio-reactive links~~, ~~tap tempo~~ (v0.4)
4. ~~Groups and presets~~ (v0.3); ~~cue fade in/out, release, tracking switch, colour and attribute effects, pixel map with NDI, multi-cell fixtures, extra screens~~ (v0.5); ~~chases, button functions, pixel selection, GDTF import~~ (v0.6); cue delay times
5. Audio device routing, LTC/MTC in, LTC generator out
6. Timeline: cue clips on tracks, audio waveform track, timecode-driven playback
7. ~~GDTF import~~ (v0.6); MVR import
8. Windows build with multi-touch
