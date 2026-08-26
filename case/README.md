# Desk stand + back cover

For the Elecrow CrowPanel Advance 10.1" (ESP32-P4), board revision **V1.2**.

Two printed parts: a back shell that screws to the board's four M3 corner
holes, and two wedge feet that bolt to the shell and lean it back 18°.

| File | What it is |
|---|---|
| `tempest_stand.scad` | The source. Parametric — change one number and re-export. |
| `tempest_shell.stl` | Back cover, 1 off |
| `tempest_foot.stl` | Wedge foot, **2 off** |
| `preview.png`, `shell.png`, `foot.png` | Renders |

## Measure this before you print

```
REAR_CLEARANCE = 11.0;      // line 29 of tempest_stand.scad
```

This is the only dimension in the design that is a guess. Everything else came
out of Elecrow's own Eagle PCB file and is good to about 0.1 mm; component
heights are simply not in that file.

Lay a straight edge across the back of the board and measure the gap to the
tallest thing standing on it — usually the ESP32-C6 module can or a connector
body. Add 1.5 mm of air, put the result on that line, and re-export the shell.

Too small and the shell will bow the board when you tighten the screws. Too
large and it will stand proud of the edges. Nothing else depends on it, so
this is a one-number fix.

## Where the numbers came from

Read out of `Eagle_SCH&PCB/1.2/ESP32-P4 Display 10.1 inch V1.2.brd` in
Elecrow's repository, not measured by hand or taken from the spec sheet:

- **Board outline** 247.04 × 147.01 mm
- **Mounting holes** M3.2, four corners, 3.1 mm in from each edge
  (240.9 × 141.0 mm pattern)
- **Every connector position**, which is what places the cutouts:
  - Right edge: XH2.54 at y=41.3, USB-C at y=63.0 and y=84.0, power switch at y=100.9
  - Top edge: Grove at x=18.1 and x=40.1, GPIO headers at x=215.5 and x=231.0
  - Bottom edge: PH2.0 at x=35.8, 2×12 header at x=123.9, test points at
    x=162.4, PH2.0 at x=215.1 and x=231.0
  - Left edge: microphone port at y=135.1

Origin is the bottom-left corner of the board seen from the **front**.

The Grove connector at x=18.1 on the top edge is the one the indoor
temperature sensor plugs into, so that cutout matters.

## Printing

**Shell** and **bezel** are both 253 × 153 mm, so each needs a bed of at least
260 mm. On a 270 mm bed you have 17 mm to spare; print them square to the axes,
not diagonally, and one at a time.

- Shell: flat on the bed, open side up. No supports.
- Bezel: face down on the bed, so the visible front is the smooth first layer
  and the chamfer around the window is self-supporting.
- 0.2 mm layers, 3 walls, 15% infill.
- PETG or ASA if it will sit in sunlight. **Not PLA** — a weather panel in a
  window gets hot enough to sag it.

**Foot** ×2 — lay the triangular face on the bed. No supports. Same material,
25% infill; these carry the load.

## Assembly

1. Drop the board into the shell, face up.
2. Lay the bezel on top and drive four **M3 countersunk** screws down through
   the bezel, through the board's corner holes, into the shell's bosses. Length
   ≈ `BEZEL_T + FRONT_GLASS + BOARD_THICK + 6`, so about **20 mm** at the
   defaults. The counterbores are modelled so the heads sit flush.
3. Four **M3 × 12 mm** into the feet, two per foot, through the pads on the
   shell's back.
4. Route the USB-C power cable out of the right-edge cutout.

Tighten the four main screws gradually and in a diagonal order. They are
clamping a glass-fronted panel, and doing one corner fully first is how you
crack one.

Self-tapping into printed plastic works fine at these loads. If you would
rather use heat-set inserts, change `BOSS_HOLE` from 2.9 to 4.2 and re-export.

## Adjusting

Everything is a named constant at the top of the `.scad`:

- `TILT` — 18° suits a desk you look down at slightly. Raise it toward 25° for
  a low shelf, drop it toward 10° for eye level. The feet and the shell both
  read this, so they stay consistent.
- `FIT_GAP` — 0.6 mm of slack around the board. Tighten to 0.4 mm if your
  printer runs dimensionally accurate.
- `WALL` — 2.4 mm, which is six perimeters at a 0.4 mm nozzle.
- `FOOT_DEPTH` — 92 mm of rearward reach. It only needs to exceed the 45 mm
  the panel's top leans back, so there is plenty of margin; shorten it if the
  stand is too deep for your shelf.

Re-render a preview after any change:

```bash
openscad -o preview.png --imgsize=1000,720 -D 'PART="preview"' tempest_stand.scad
```
