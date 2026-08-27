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
| 4 | M3 countersunk screws, **20 mm** (see below) |
| 8 | M3 x 12 mm, any head, for the feet — **four per foot** |

Screw length is worked out from your measurements rather than assumed. Run:

```bash
openscad -o /dev/null -D 'PART="none"' tempest_stand.scad
```

and it prints the window, e.g. *"at least 15.9 mm to reach the insert, at most
22.1 mm before it bottoms out."* Too long is the failure that matters: the screw
bottoms in a blind pocket with only 2 mm of bezel left above it, and keeping
turning cracks the front face. **Do not fit 25 mm.**

## Measure this before you print

Four numbers are guesses. Everything else came out of Elecrow's own Eagle PCB
file and is good to about 0.1 mm — but that file carries no component heights
and no panel outline, so these cannot be read from it.

| Constant | Default | How to measure it |
|---|---|---|
| `REAR_CLEARANCE` | 11.0 | Straight edge across the **back**, gap to the tallest thing on it — usually the C6 module can or a connector body. Add 1.5 mm of air. |
| `FRONT_GLASS` | 3.5 | Straight edge across the **front**, down to the PCB. How far the glass stands proud. |
| `BOARD_THICK` | 6.0 | Total, glass face to back of PCB. Only sets screw length. |
| `ACTIVE_W` / `ACTIVE_H` | 225.3 x 133.7 | Power the panel on and measure the **lit** rectangle. |

`REAR_CLEARANCE` too small bows the board as you tighten; too large and the
shell stands proud of the edges.

`FRONT_GLASS` is what stops the bezel rocking — the frame is recessed by this
much so it lands on glass, not on the PCB.

The active-area default is the area implied by a 10.1" diagonal at 1024x600
(221.3 x 129.7 mm), plus 2 mm of safety all round, assumed centred. It is
**deliberately generous**, and the error is asymmetric: too large shows a sliver
of PCB, too small covers pixels and cannot be undone once printed. Measure the
lit area and tighten it if you want a chunkier frame.

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
- **Every connector position** — all measured, but only the two USB-C ports
  are cut. `ALL_PORTS = false` near the top of the `.scad` opens the rest if
  you ever want them; the positions below stay correct either way:
  - Right edge: XH2.54 at y=41.3, USB-C at y=63.0 and y=84.0, power switch at y=100.9
  - Top edge: Grove at x=18.1 and x=40.1, GPIO headers at x=215.5 and x=231.0
  - Bottom edge: PH2.0 at x=35.8, 2×12 header at x=123.9, test points at
    x=162.4, PH2.0 at x=215.1 and x=231.0
  - Left edge: microphone port at y=135.1

Origin is the bottom-left corner of the board seen from the **front**.

### Two things the back no longer opens

Worth knowing before you print, because both are sealed in once assembled:

- **SW1, the power switch**, at y=100.9 on the right edge. With `ALL_PORTS`
  off you cannot reach it — the panel powers up and down by its USB-C cable.
- **The Grove connector at x=18.1** on the top edge, which is where the indoor
  temperature sensor plugs in. Its cable has nowhere to leave the case.

Set `ALL_PORTS = true` and re-export the shell if you want either of them.

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
4. Lay the board into the bezel, face down, so the glass sits in the relief and
   the four bosses land on the PCB at the corners.
5. Drop the shell over the back. The bosses inside it should meet the PCB.
6. Drive the four **M3 countersunk** screws in from the back. They pass through
   the shell, through the board, into the brass.
7. Route the USB-C power cable out of the right-edge cutout.

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
