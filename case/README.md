# Desk stand + back cover

For the Elecrow CrowPanel Advance 10.1" (ESP32-P4), board revision **V1.2**.

Three printed parts: a front **housing**, a back **cover**, and two wedge feet
that bolt to the cover and lean it back 18°.

The board drops into the housing from the **back**. Glass lands on the window
lip; the walls locate it. The cover then screws to ten posts in the housing
walls — **not** through the board's own corner holes. Those holes sit 1.25 mm
from the LCD module, so there is nowhere for a boss or an insert next to the
glass. Four M3s stay in the left/right walls; six more (three top, three
bottom) clamp the long edges. They go into **M3 x 6 x 4.2 brass heat-set
inserts**, and nothing shows on the front.

| File | What it is |
|---|---|
| `tempest_stand.scad` | The source. Parametric — change one number and re-export. |
| `tempest_housing.stl` | Front tray, 1 off |
| `tempest_cover.stl` | Back plate, 1 off |
| `tempest_foot.stl` | Wedge foot, **2 off** |
| `check_geometry.py` | Geometry self-test — run it after changing any constant |
| `preview.png`, `housing.png`, `cover.png`, `foot.png` | Renders |

## Hardware

| Qty | Part |
|---|---|
| 10 | **M3 x 6 x 4.2 mm OD** knurled brass heat-set inserts (M3x4x4.2 and M3x8x4.2 also fit — set `INSERT_LEN`) |
| 10 | M3 button-head screws, **12 mm** (see below) |
| 8 | M3 x 12 mm, any head, for the feet — **four per foot** |

Screw length is worked out from the constants rather than assumed. Run:

```bash
openscad -o /dev/null -D 'PART="none"' tempest_stand.scad
```

and it prints the window — currently *at least ~1.8 mm to reach the insert, at
most ~14 mm before it bottoms.* **M3 x 12** is the fit. Too long still
matters: the insert is blind, and another quarter turn can bulge the wall.
These are short on purpose; the old clamp needed M3 x 30 through the whole
stack.

## Room in the back

The cover is a **3.6 mm** plate (`WALL`; was 2.4). It drops the full plate
thickness into a well in the back of the housing (`COVER_RECESS = WALL`) so
the back sits flush, wrapped by 1.5 mm of wall so the rim takes shear.
Housing walls are **10 mm** so the inserts keep ~2 mm of plastic after the
well; 8 mm left them on the rim.
Spacer pads on the inside press on
empty solder-mask pour on the PCB back (not the corner mounting holes — those
hit the alignment lip and the SD / BOOT / Grove / USB parts) so the stack
cannot rattle. They are not screw paths. Lower pair: `(70, 40)`, `(178, 40)`,
each in the centre of a foot pad. Upper pair: `(80, 108)`, `(168, 108)`
(front view). The four case-screw holes are just holes — an inner boss around
them sat as high as the alignment lip and stopped the cover seating.

`BAY_DEPTH` is 2 mm. It used to be 12 mm for an internal LiPo; that is off
(`BATTERY_BAY = false`) so the panel is USB-powered. Put `BAY_DEPTH` back to
12 and re-enable `battery_bay()` if you ever want the battery.

- **Speakers** — stuck to the back of the PCB, not to the case. The cover has
  grilles over them. `SPK_W` / `SPK_H` are still waiting on a caliper; the
  speakers are accessories and are not in the STEP model.
- **Indoor sensor** — AHT20/DHT20 on a post inside the cover, grille through
  a window so it reads room air rather than panel waste heat. The whole
  module sits on one 2.4 mm bed so the board is level; a 3 mm U-shaped
  wall on the three sides away from the screw blocks display heat.

## Measure this before you print

These were guesses until Elecrow's STEP model of the assembly turned up
(`ESP32-P4-10_1-inch-20251230.stp`). **All four were wrong**, so they are now
read off that model instead. Taking the PCB's front face as the reference:

| Plane | Offset | |
|---|---|---|
| glass front surface | +2.00 | |
| active area | -0.20 | 222.7 x 125.3 — the lit rectangle |
| LCD module footprint | -4.80 | 235.5 x 143.5, centred on the board |
| PCB front face | -4.90 | |
| PCB back face | -6.50 | PCB is 1.60 mm |
| rear-most extent | -19.62 | 199.0 x 76.5 |

| Constant | Now | From |
|---|---|---|
| `REAR_CLEARANCE` | **13.5** | 13.12 measured, plus 0.4 mm of air |
| `FRONT_GLASS` | **6.9** | glass +2.00 over PCB front -4.90 |
| `BOARD_THICK` | **8.5** | glass +2.00 to PCB back -6.50 |

`REAR_CLEARANCE` at 11.0 would have bowed the board. `FRONT_GLASS` at 3.5 was
out by nearly a factor of two, which would have left the face resting on the
PCB and rocking on the glass.

### The window

The model puts the lit area at **222.7 x 125.3**, not quite centred in the LCD
module — off by 0.65 mm one way and 3.0 mm the other. The first print used a
226 x 135 window centred on the board because the **sign** of that 3 mm was
unknown.

A photo of that print (USB-C on the left, facing the screen) settled it: a
thick black strip under the 7-day forecast and a tight top. The 3 mm is
**vertical** — the lit area sits high on the board. Left and right were close;
the right frame looking wider was mostly camera angle.

So the window edges are independent. The left is the reference. **y = 0
is the status-bar edge**, not the alert ticker — a print that raised
`ACTIVE_Y` cropped BAT / SKY and left the bottom black bar. The hole now
opens toward y = 0 (~7 mm more than the first even window) and the high-y
edge sits lower (~7 mm) to hide the unlit strip. It still sits inside the
LCD module (235.5 x 143.5), so no PCB shows.

The front of the housing **slopes** from the glass out to the case
(`CHAMFER = 5` mm over a 3 mm face) so a finger reaches the pixels instead of
hitting a square well. A short flat land (`LIP_FLAT`) at the glass keeps that
edge a face, not a knife. Printed face-down this is an inward overhang; if it
fails on your printer, drop `CHAMFER` to 3 (45°).

Tweak `ACTIVE_X` / `ACTIVE_Y` / `ACTIVE_W` / `ACTIVE_H` if a later print is
still fat or tight on one edge. Too small covers pixels and cannot be undone
once printed. `ACTIVE_Y` is the status-bar side of the hole.

After changing anything, re-run the self-test — it catches the failure mode this
design is prone to, where a pocket cut in the wrong order silently deletes a
boss:

```bash
python check_geometry.py
```

It also prints which spans of each housing wall are open, so you can confirm
at a glance that the two USB-C openings are on the left and nothing else.

## Where the numbers came from

Read out of `Eagle_SCH&PCB/1.2/ESP32-P4 Display 10.1 inch V1.2.brd` in
Elecrow's repository, not measured by hand or taken from the spec sheet:

- **Board outline** 247.04 × 147.01 mm
- **Mounting holes** M3.2, four corners, 3.1 mm in from each edge
  (240.9 × 141.0 mm pattern) — unused. Cover pads sit inboard on empty
  pour at `(70, 40)`, `(178, 40)`, `(80, 108)`, `(168, 108)` (front view).
- **Every connector position** — all measured. `ALL_PORTS = true` near the top
  of the `.scad` opens every connector; it defaults to `false` (USB-C only).
  All of these are **back-view** x values, and are mirrored by `mx()` before
  cutting:
  - Right edge: XH2.54 at y=41.3, USB-C at y=63.0 and y=84.0, power switch at y=100.9
  - Top edge: Grove at x=18.1 and x=40.1, GPIO headers at x=215.5 and x=231.0
  - Bottom edge: PH2.0 at x=35.8, 2×12 header at x=123.9, test points at
    x=162.4, PH2.0 at x=215.1 and x=231.0
  - Left edge: microphone port at y=135.1

Origin is the bottom-left corner of the board seen from the **front**.

### Which way round the board data is

Every position here was read from Elecrow's Eagle PCB file, whose top view is
the board's **component side** — which on a display board is the **back**. The
model screenshot confirms it: the Grove pair (22 mm apart) on the left, the
GPIO pair (15 mm apart) on the right, exactly as the file says, with the
buttons and the power switch visible.

But this `.scad` assembles with the screen facing +z — housing on top, cover
at z = 0 — so its own x,y is a **front** view. The two are mirror images:
looking at the finished case from behind, the model's +x is on your left while
the board's +x is on your right.

So every position taken from the PCB file is mirrored through `mx()` before it
is cut. **Without this, all the openings came out on the wrong side**, which is
what the earlier exports did.

> **Five-second check before you print:** stand the panel up facing you. The
> two USB-C ports should be on your **left**. If they are on your right, set
> `BOARD_DATA_IS_BACK_VIEW = false` and re-export.

### The controls face backwards, so they get cover slots

Only the two USB-C ports are cut into a housing edge. BOOT, RESET and the power
switch face out of the **back** of the board, so they get slots through the
cover:

| Board x, y (back view) | Slot | What |
|---|---|---|
| 7.0, 25.0 | 11 x 30 | K3 + K4, one slot covering both buttons |
| 240.5, 100.9 | 13 x 15 | SW1 power slide switch, with room to slide it |

These are **slots, not close-fitting holes**, deliberately. The button
positions come off a screenshot rather than a dimensioned drawing and are good
to maybe 3 mm; the switch comes from the PCB file and is good to 0.1 mm. A slot
a few mm oversize costs nothing; a hole in the wrong place cannot be undone.
Tighten them once you can measure the real board.

An earlier version of this file put all three on the right-hand edge, on the
strength of a STEP transform chain that turned out to be wrong. That version
also "corrected" the connector table below, claiming SW1 at y = 100.9 was
really CN2. **That correction was itself wrong and has been reverted** — SW1 at
y = 100.9 is the power switch, as the PCB file always said.

### Grove is sealed unless you open it

The Grove connector at x=18.1 on the top edge is where the indoor temperature
sensor plugs in. **`ALL_PORTS` defaults to `false`**, so that cutout is not
included. Set it `true` if you want the Grove opening in the housing wall.

## Printing

Housing is about **268 × 168 mm** (`FRAME = 10` mm so the insert pockets keep
plastic around them after the cover well). The cover is ~3.6 mm smaller on
each side so it drops into the well. Each needs a bed of at least 270 mm.
Print them square to the axes, not diagonally, and one at a time.

- Housing: **face down** on the bed, so the visible front is the smooth first
  layer and the touch ramp is a self-supporting inward overhang. Insert
  pockets open on the floor of the cover well. No supports.
- Cover: flat on the bed, open side up. Counterbores for the case screws are
  in the first layers. No supports.
- 0.2 mm layers, 3 walls, 15% infill.
- PETG or ASA if it will sit in sunlight. **Not PLA** — a weather panel in a
  window gets hot enough to sag it.

**Foot** ×2 — lay the triangular face on the bed. No supports. Same material,
25% infill; these carry the load.

## Assembly

1. **Inserts first, housing face down on the bench.** Press the ten brass
   inserts into the holes on the **floor of the cover well** (not the outer
   rim) with a soldering iron at about 220 °C and a flat or insert tip. Go
   slowly and let the plastic melt rather than pushing. Stop when the flange
   is flush with the seat — the pocket is 6.2 mm deep for a 6 mm insert, so
   there is nowhere for a proud insert to hide.
2. Check each one is **square** before it cools. A leaning insert will not
   accept the screw later, and reheating to fix it is much harder than getting
   it right now.
3. **Fit the feet now, before the cover meets the housing.** Their screws are
   driven from *inside* the cover — eight **M3 × 12 mm**, four per foot, down
   through the pads and tapping into the wedge. Once the cover is on you
   cannot reach them.
4. Housing face-down on the bench. **Drop the board in from the back** so the
   glass sits on the window land and the walls locate it.
5. Drop the cover into the well. The plate should sit on the seat with the
   housing walls wrapping it; the alignment lip tucks into the board cavity.
   The spacer pads should meet the PCB at the four pours.
6. Drive the ten **M3 × 12 button-head** screws in from the back, through the
   cover, into the brass. They never pass through the board.
7. Route the USB-C power cable out of the left-edge cutout (facing the
   screen).

Tighten the ten cover screws **gradually and in a diagonal order**. They are
closing a glass-fronted panel, and taking one side fully home first is how you
crack one. Stop as soon as they are snug — the spacer pads, not the screw
torque, set the clamp.

## Adjusting

Everything is a named constant at the top of the `.scad`:

- `ACTIVE_X` / `ACTIVE_Y` / `ACTIVE_W` / `ACTIVE_H` — glass-level window in
  board coordinates. Left is the reference; change one edge at a time.
- `CHAMFER` — how far the front slope runs out from the glass. Drop to 3 if
  the overhang fails.
- `TILT` — 18° suits a desk you look down at slightly. Raise it toward 25° for
  a low shelf, drop it toward 10° for eye level. The feet and the cover both
  read this, so they stay consistent.
- `FIT_GAP` — 0.6 mm of slack around the board. Tighten to 0.4 mm if your
  printer runs dimensionally accurate.
- `WALL` — 3.6 mm cover plate (nine perimeters at a 0.4 mm nozzle). The
  housing well is the same depth (`COVER_RECESS = WALL`) so the back is flush.
- `FOOT_X`, `FOOT_BOLT_U`, `FOOT_BOLT_DZ` — where the feet stand and where
  their bolts are. The cover, the foot and the preview all read these, so the
  drilling cannot drift out of step with the part being drilled into.
- `FOOT_DEPTH` — 92 mm of rearward reach. It only needs to exceed the 45 mm
  the panel's top leans back, so there is plenty of margin; shorten it if the
  stand is too deep for your shelf.

Re-render a preview after any change:

```bash
openscad -o preview.png --imgsize=1000,720 -D 'PART="preview"' tempest_stand.scad
```
