# Desk stand + back cover

For the Elecrow CrowPanel Advance 10.1" (ESP32-P4), board revision **V1.2**.

Three printed parts: a back shell, a front bezel, and two wedge feet that bolt
to the shell and lean it back 18°.

Shell and bezel clamp the board between them. The four M3 screws go in from the
**back**, pass through the shell, through the board's own M3 corner holes, and
thread into **M3 x 6 x 4.2 brass heat-set inserts** in the bezel. So the threads
are in brass rather than printed plastic, and nothing shows on the front.

| File | What it is |
|---|---|
| `tempest_stand.scad` | The source. Parametric — change one number and re-export. |
| `tempest_shell.stl` | Back cover, 1 off |
| `tempest_bezel.stl` | Front bezel, 1 off |
| `tempest_foot.stl` | Wedge foot, **2 off** |
| `check_geometry.py` | Geometry self-test — run it after changing any constant |
| `preview.png`, `bezel.png`, `shell.png`, `foot.png` | Renders |

## Hardware

| Qty | Part |
|---|---|
| 4 | M3 x 6 mm x 4.2 mm OD brass heat-set inserts |
| 4 | M3 countersunk screws, **35 mm** (see below) |
| 8 | M3 x 12 mm, any head, for the feet — **four per foot** |
| 1 | Flat 3.7 V LiPo with a PH2.0 lead, up to 140 x 75 x 12 mm |
| — | Foam tape or a strap so the battery cannot move |

Screw length is worked out from your measurements rather than assumed. Run:

```bash
openscad -o /dev/null -D 'PART="none"' tempest_stand.scad
```

and it prints the window — currently *"at least 29.5 mm to reach the insert, at
most 35.7 mm before it bottoms out."* They got longer because the shell is now
deep enough to hold a battery. Too long is still the failure that matters: the
screw bottoms in a blind pocket, and another quarter turn cracks the front
face. **Do not fit 40 mm.**

## Room in the back

The shell is deepened by `BAY_DEPTH` (12 mm) rather than growing a hump, so the
back stays flat for the feet and there is somewhere to put the speakers
wherever their wires reach. Front to back it is now about 29 mm plus the bezel.

Stacking from the back plate forward: the battery and speakers sit on the
inside of the plate and take `BAY_DEPTH`; the board's own rear components take
`REAR_CLEARANCE` above that.

- **Battery** — a rib fence sized **140 x 75 x 12**, which takes a 10000 mAh
  flat pack (typically around 130 x 65 x 10) and also swallows a 5000 mAh one
  (around 100 x 55 x 8). A 5000 will rattle in it, so tape it down. The fence
  has a notch for the lead, which runs to the PH2.0 socket at x = 35.8 on the
  bottom edge. It is a fence rather than a box: almost no plastic, and the wire
  has somewhere to go.
- **Speakers** — two fenced pockets in the bottom corners, at x = 30 and
  x = 217, spread apart for stereo and clear of both foot pads. Each has slots
  through the back plate underneath, so the speaker is not firing into a sealed
  box.

`SPK_W` / `SPK_H` default to **34 x 24** and are the one dimension here still
waiting on a caliper — the speakers are accessories and are not in the STEP
model. Measure yours and adjust if they do not drop in.

Both are sealed inside once the shell is on. That is fine for a battery you
charge over USB, but it does mean you fit them before you close it up.

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

| Constant | Was | Now | From |
|---|---|---|---|
| `REAR_CLEARANCE` | 11.0 | **13.5** | 13.12 measured, plus 0.4 mm of air |
| `FRONT_GLASS` | 3.5 | **6.9** | glass +2.00 over PCB front -4.90 |
| `BOARD_THICK` | 6.0 | **8.5** | glass +2.00 to PCB back -6.50 |
| `ACTIVE_W`/`H` | 225.3 x 133.7 | **226 x 135** | see below |

`REAR_CLEARANCE` at 11.0 would have bowed the board as the screws came up
tight. `FRONT_GLASS` at 3.5 was out by nearly a factor of two, which would have
left the bezel resting on the PCB and rocking on the glass.

### The window

The model puts the lit area at **222.7 x 125.3**, and says it is *not* quite
centred in the LCD module — off by 0.65 mm one way and 3.0 mm the other. The
model's in-plane axes cannot be tied to a left/right/up/down without an anchor
I do not have, so the **sign** of that 3 mm offset is unknown.

So the window is sized to be right either way: **226 x 135, centred**. That
clears the lit area with margin whichever way the offset runs, while staying
inside the LCD module (235.5 x 143.5, which *is* centred), so no bare PCB shows
through. The error is deliberately one-sided — slightly too big shows a sliver
of black module border, too small covers pixels and cannot be undone once
printed.

Power the panel on, measure the lit rectangle, and tighten it if you want a
narrower frame.

After changing anything, re-run the self-test — it catches the failure mode this
design is prone to, where a pocket cut in the wrong order silently deletes a
boss:

```bash
python check_geometry.py
```

It also prints which spans of each wall are open, so you can confirm at a
glance that the back has the two USB-C openings and nothing else.

## Where the numbers came from

Read out of `Eagle_SCH&PCB/1.2/ESP32-P4 Display 10.1 inch V1.2.brd` in
Elecrow's repository, not measured by hand or taken from the spec sheet:

- **Board outline** 247.04 × 147.01 mm
- **Mounting holes** M3.2, four corners, 3.1 mm in from each edge
  (240.9 × 141.0 mm pattern)
- **Every connector position** — all measured, but only the two USB-C ports,
  the two buttons and the power switch are cut. `ALL_PORTS = false` near the
  top of the `.scad` opens the rest if you ever want them:
  - Right edge: XH2.54 at y=41.3, USB-C at y=63.0 and y=84.0, power switch at y=100.9
  - Top edge: Grove at x=18.1 and x=40.1, GPIO headers at x=215.5 and x=231.0
  - Bottom edge: PH2.0 at x=35.8, 2×12 header at x=123.9, test points at
    x=162.4, PH2.0 at x=215.1 and x=231.0
  - Left edge: microphone port at y=135.1

Origin is the bottom-left corner of the board seen from the **front**.

### The buttons and the switch are on the right edge, not the back

BOOT, RESET and the power slide switch all sit on the **same right-hand edge**
as the two USB-C ports, so they get edge openings rather than holes in the back.
Positions come from the STEP model, cross-checked against the two USB-C ports,
which the model and the PCB file agree on to 0.6 mm:

| Board y | What |
|---|---|
| 19.8 | tactile button (K3 or K4), 5.2 mm body |
| 34.3 | the other tactile button |
| 44.9 | MST22D18G2 power slide switch |
| 63.0, 84.0 | the two USB-C ports |
| 103.7 | CN2, a 4-pin through-hole connector |

This **corrects the table below**: what it calls "J10 XH2.54-4P at 41.3" is the
slide switch, and what it calls "SW1 power switch at 100.9" is CN2. The STEP
model names its parts, so it wins over designators inferred from the PCB file.

The second button and the switch share one opening. Their bodies end up 1.8 mm
apart and no rib that thin is worth printing, so the merge is deliberate.

### One thing the back no longer opens

Worth knowing before you print, because both are sealed in once assembled:

**The Grove connector at x=18.1** on the top edge, which is where the indoor
temperature sensor plugs in. Its cable has nowhere to leave the case. Set
`ALL_PORTS = true` and re-export the shell if you want it.

## Printing

**Shell** and **bezel** are both 253 × 153 mm, so each needs a bed of at least
260 mm. On a 270 mm bed you have 17 mm to spare; print them square to the axes,
not diagonally, and one at a time.

- Shell: flat on the bed, open side up. No supports.
- Bezel: **face down** on the bed, so the visible front is the smooth first
  layer, the chamfer around the window is self-supporting, and the insert
  bosses print upward as solid pillars.
- 0.2 mm layers, 3 walls, 15% infill.
- PETG or ASA if it will sit in sunlight. **Not PLA** — a weather panel in a
  window gets hot enough to sag it.

**Foot** ×2 — lay the triangular face on the bed. No supports. Same material,
25% infill; these carry the load.

## Assembly

1. **Inserts first, bezel face down on the bench.** Press the four brass
   inserts into the bosses on the back of the bezel with a soldering iron at
   about 220 °C and a flat or insert tip. Go slowly and let the plastic melt
   rather than pushing. Stop when the flange is flush with the boss — the
   pocket is 6.2 mm deep for a 6 mm insert, so there is nowhere for a
   proud insert to hide.
2. Check each one is **square** before it cools. Sighting across the bezel is
   enough; a leaning insert will not accept the screw later, and reheating to
   fix it is much harder than getting it right now.
3. **Fit the feet now, before the board goes anywhere near the shell.** Their
   screws are driven from *inside* the shell — eight **M3 × 12 mm**, four per
   foot, down through the pads and tapping into the wedge. Once the board is
   in you cannot reach them.
4. **Drop the battery and the speakers into their fences**, also from inside
   the shell, and tape the battery down. Route the leads toward the bottom
   edge. Same reason: no access once the board is in.
5. Lay the board into the bezel, face down, so the glass sits in the relief and
   the four bosses land on the PCB at the corners.
6. Plug the battery and speaker leads into the board.
7. Drop the shell over the back. The bosses inside it should meet the PCB.
8. Drive the four **M3 countersunk** screws in from the back. They pass through
   the shell, through the board, into the brass.
9. Route the USB-C power cable out of the right-edge cutout.

Tighten the four main screws **gradually and in a diagonal order**. They are
clamping a glass-fronted panel, and taking one corner fully home first is how
you crack one. Stop as soon as they are snug — the boss height, not the screw
torque, is what sets the clamp.

## Adjusting

Everything is a named constant at the top of the `.scad`:

- `TILT` — 18° suits a desk you look down at slightly. Raise it toward 25° for
  a low shelf, drop it toward 10° for eye level. The feet and the shell both
  read this, so they stay consistent.
- `FIT_GAP` — 0.6 mm of slack around the board. Tighten to 0.4 mm if your
  printer runs dimensionally accurate.
- `WALL` — 2.4 mm, which is six perimeters at a 0.4 mm nozzle.
- `FOOT_X`, `FOOT_BOLT_U`, `FOOT_BOLT_DZ` — where the feet stand and where
  their bolts are. The shell, the foot and the preview all read these, so the
  drilling cannot drift out of step with the part being drilled into.
- `FOOT_DEPTH` — 92 mm of rearward reach. It only needs to exceed the 45 mm
  the panel's top leans back, so there is plenty of margin; shorten it if the
  stand is too deep for your shelf.

Re-render a preview after any change:

```bash
openscad -o preview.png --imgsize=1000,720 -D 'PART="preview"' tempest_stand.scad
```
