// Desk stand + back cover for the Elecrow CrowPanel Advance 10.1" (ESP32-P4).
//
// Two printed parts:
//   shell()  - back cover, screws to the board's four M3 corner holes
//   foot()   - angled leg, x2, screws to the shell
//
// Every dimension below that starts "MEASURED" came out of Elecrow's own Eagle
// PCB file for board revision V1.2:
//   Eagle_SCH&PCB/1.2/ESP32-P4 Display 10.1 inch V1.2.brd
// Board outline and every connector position were read from that file rather
// than estimated, so they are trustworthy to about 0.1 mm.
//
// The one number that is NOT from the PCB file is REAR_CLEARANCE. The Eagle
// file carries no component heights, so you must measure it -- see below.
//
// Units: millimetres. Render with $fn=48 for a clean preview, 96 to export.

// ---------------------------------------------------------------------------
// MEASURE THIS ONE
// ---------------------------------------------------------------------------

// Distance from the back surface of the PCB to the highest thing standing on
// it -- usually the ESP32-C6 module can, an electrolytic cap, or the tallest
// connector body. Put a straight edge across the back and measure the gap.
// Add 1.5 mm of air on top of whatever you read.
//
// If it is wrong the shell will either bow the board (too small) or stand
// proud of it (too large). Nothing else in this file depends on it.
REAR_CLEARANCE = 11.0;      // <-- CHANGE ME

// How far the display surface stands PROUD of the PCB's front face. The bezel
// is recessed by this much so it lies flat on the glass instead of on the PCB
// and rocking. Straight edge across the front, measure to the PCB.
FRONT_GLASS = 3.5;          // <-- CHANGE ME

// Total board thickness, front glass to back of PCB, used only to work out
// screw length. Not critical.
BOARD_THICK = 6.0;          // <-- CHANGE ME

// Visible active area of the panel and where its bottom-left corner sits
// relative to the board's bottom-left corner, seen from the FRONT.
//
// The default is the active area implied by a 10.1" diagonal at 1024x600
// (221.3 x 129.7 mm) plus 2 mm of safety all round, assumed centred. It is
// deliberately GENEROUS: too large only shows a sliver of PCB, too small
// covers pixels and cannot be undone once printed. Measure the lit area with
// the panel powered on and tighten these if you want a chunkier frame.
ACTIVE_W = 225.3;
ACTIVE_H = 133.7;
ACTIVE_X = (247.04 - ACTIVE_W) / 2;
ACTIVE_Y = (147.01 - ACTIVE_H) / 2;

// ---------------------------------------------------------------------------
// Board geometry (MEASURED, from the PCB file)
// ---------------------------------------------------------------------------

BOARD_W = 247.04;
BOARD_H = 147.01;

// M3.2 through-holes, 3.1 mm in from each edge. Origin is the bottom-left
// corner of the board as seen from the FRONT.
HOLE_D      = 3.2;
HOLE_INSET_X = 3.1;
HOLE_INSET_Y = 3.0;
holes = [
    [HOLE_INSET_X,           HOLE_INSET_Y],
    [BOARD_W - HOLE_INSET_X, HOLE_INSET_Y],
    [HOLE_INSET_X,           BOARD_H - HOLE_INSET_Y],
    [BOARD_W - HOLE_INSET_X, BOARD_H - HOLE_INSET_Y],
];

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

WALL      = 2.4;        // 6 perimeters at 0.4 mm
FIT_GAP   = 0.6;        // slack around the board so it drops in
CORNER_R  = 4;

SHELL_W = BOARD_W + 2*FIT_GAP + 2*WALL;     // ~253.8 -- fits a 270 mm bed
SHELL_H = BOARD_H + 2*FIT_GAP + 2*WALL;     // ~153.8
SHELL_D = REAR_CLEARANCE + WALL;

BOSS_D    = 8;          // screw boss outside diameter
BOSS_HOLE = 2.9;        // self-tapping pilot for M3; use 4.2 for heat-set

TILT = 18;              // degrees off vertical

// ---------------------------------------------------------------------------
// Connector cutouts (MEASURED, board coordinates, from the PCB file)
//
// Each entry is [centre_position_along_that_edge, opening_width]. Openings are
// cut through the full depth of the wall so connector height cannot be wrong.
// ---------------------------------------------------------------------------

// Right edge, positioned by Y (height up the board)
right_cuts = [
    [ 41.3, 14],    // J10  XH2.54-4P
    [ 63.0, 14],    // J16  USB-C
    [ 84.0, 14],    // J1   USB-C
    [100.9, 16],    // SW1  power switch
];

// Top edge, positioned by X
top_cuts = [
    [ 18.1, 16],    // J2   Grove / Crowtail  <- the indoor sensor plugs here
    [ 40.1, 16],    // J13  Grove / Crowtail
    [215.5, 24],    // J9   GPIO header
    [231.0, 24],    // J11  GPIO header
];

// Bottom edge, positioned by X
bottom_cuts = [
    [ 35.8, 12],    // J4   PH2.0 2-pin
    [123.9, 38],    // J7   2x12 header
    [162.4, 12],    // J8   test points
    [215.1, 12],    // J3   PH2.0 2-pin
    [231.0, 12],    // J6   PH2.0 2-pin
];

// Left edge, positioned by Y
left_cuts = [
    [135.1, 8],     // U176 microphone -- a port, not a connector
];

// ---------------------------------------------------------------------------

module rounded_box(w, h, d, r) {
    hull() for (x = [r, w-r], y = [r, h-r])
        translate([x, y, 0]) cylinder(r=r, h=d, $fn=48);
}

module vent_grid() {
    // Slots rather than holes: they bridge cleanly when printed and move more
    // air per unit of lost stiffness. Kept clear of the screw bosses.
    for (x = [60 : 22 : BOARD_W - 60])
        for (y = [28 : 18 : BOARD_H - 28])
            translate([x, y, -1])
                hull() {
                    translate([0, -5, 0]) cylinder(d=4, h=WALL+2, $fn=24);
                    translate([0,  5, 0]) cylinder(d=4, h=WALL+2, $fn=24);
                }
}

module edge_cutouts() {
    depth = SHELL_D + 2;
    // Right wall
    for (c = right_cuts)
        translate([BOARD_W + FIT_GAP - 1, c[0] - c[1]/2, WALL])
            cube([WALL + 3, c[1], depth]);
    // Left wall
    for (c = left_cuts)
        translate([-FIT_GAP - WALL - 2, c[0] - c[1]/2, WALL])
            cube([WALL + 3, c[1], depth]);
    // Top wall
    for (c = top_cuts)
        translate([c[0] - c[1]/2, BOARD_H + FIT_GAP - 1, WALL])
            cube([c[1], WALL + 3, depth]);
    // Bottom wall
    for (c = bottom_cuts)
        translate([c[0] - c[1]/2, -FIT_GAP - WALL - 2, WALL])
            cube([c[1], WALL + 3, depth]);
}

module shell() {
    difference() {
        union() {
            // Outer tray, positioned so board coordinates map directly.
            translate([-FIT_GAP - WALL, -FIT_GAP - WALL, 0])
                rounded_box(SHELL_W, SHELL_H, SHELL_D, CORNER_R);
            // Screw bosses rise to meet the board.
            for (h = holes)
                translate([h[0], h[1], WALL])
                    cylinder(d=BOSS_D, h=REAR_CLEARANCE, $fn=48);
            // Mounting pads for the feet.
            for (fx = [BOARD_W*0.25, BOARD_W*0.75])
                translate([fx - 20, 12, WALL]) cube([40, 16, 3]);
        }

        // Board cavity
        translate([-FIT_GAP, -FIT_GAP, WALL])
            cube([BOARD_W + 2*FIT_GAP, BOARD_H + 2*FIT_GAP, SHELL_D]);

        // Blind pilot holes in the bosses. Screws now enter from the FRONT
        // through the bezel and the board, so the outside of the shell stays
        // unbroken -- no screw heads on the back of a desk ornament.
        for (h = holes)
            translate([h[0], h[1], WALL + 1])
                cylinder(d=BOSS_HOLE, h=REAR_CLEARANCE, $fn=32);

        edge_cutouts();
        vent_grid();

        // Foot screw pilots
        for (fx = [BOARD_W*0.25, BOARD_W*0.75])
            for (dx = [-13, 13])
                translate([fx + dx, 20, -1])
                    cylinder(d=BOSS_HOLE, h=WALL + 6, $fn=24);
    }
}

// ---------------------------------------------------------------------------
// Front bezel
//
// Sandwiches the board against the shell. The SAME four M3 screws pass through
// the bezel, through the board's corner holes, and thread into the shell's
// bosses -- so no extra fixings and no widening the shell past the print bed.
//
// The inner edge is chamfered away from the screen so the frame does not cast
// a shadow line across the picture at an angle.
// ---------------------------------------------------------------------------

BEZEL_T    = 3.2;       // face thickness in front of the glass
BEZEL_LIP  = 1.2;       // how far the frame overlaps onto the glass

module bezel() {
    difference() {
        union() {
            // Outer plate, matching the shell footprint exactly.
            translate([-FIT_GAP - WALL, -FIT_GAP - WALL, 0])
                rounded_box(SHELL_W, SHELL_H, BEZEL_T + FRONT_GLASS, CORNER_R);
        }

        // Relief so the frame sits on the glass, not on the PCB.
        translate([-FIT_GAP, -FIT_GAP, BEZEL_T])
            cube([BOARD_W + 2*FIT_GAP, BOARD_H + 2*FIT_GAP, FRONT_GLASS + 1]);

        // The window, with a chamfer opening outward.
        translate([ACTIVE_X + BEZEL_LIP, ACTIVE_Y + BEZEL_LIP, -1])
            cube([ACTIVE_W - 2*BEZEL_LIP, ACTIVE_H - 2*BEZEL_LIP, BEZEL_T + 2]);
        translate([ACTIVE_X + BEZEL_LIP, ACTIVE_Y + BEZEL_LIP, -0.01])
            chamfer_frame(ACTIVE_W - 2*BEZEL_LIP, ACTIVE_H - 2*BEZEL_LIP, 1.6);

        // Screw clearance + counterbore at the four corners.
        for (h = holes) {
            translate([h[0], h[1], -1])
                cylinder(d=3.4, h=BEZEL_T + FRONT_GLASS + 2, $fn=32);
            translate([h[0], h[1], -0.1])
                cylinder(d1=6.6, d2=3.4, h=2.0, $fn=32);
        }
    }
}

// A 45-degree relief around a rectangular opening, widening toward the front.
module chamfer_frame(w, h, c) {
    difference() {
        translate([-c, -c, 0]) cube([w + 2*c, h + 2*c, c]);
        translate([0, 0, -0.01])
            hull() {
                cube([w, h, 0.01]);
                translate([-c, -c, c]) cube([w + 2*c, h + 2*c, 0.01]);
            }
    }
}

// ---------------------------------------------------------------------------
// Foot
//
// A right-angle wedge. Printed on its side it needs no support, and the long
// rear toe is what stops a 250 mm wide panel from tipping backwards.
// ---------------------------------------------------------------------------

FOOT_W     = 40;
FOOT_DEPTH = 92;
FOOT_H     = 74;

module foot() {
    // 2D profile, then extruded sideways.
    //   base   : (0,0) -> (FOOT_DEPTH,0)   sits on the desk
    //   mount  : (0,0) -> top              leans back TILT from vertical,
    //                                      so the shell bolted flat to it
    //                                      leans back by exactly TILT
    top = [FOOT_H * sin(TILT), FOOT_H * cos(TILT)];

    difference() {
        union() {
            linear_extrude(FOOT_W)
                polygon([[0, 0], [FOOT_DEPTH, 0], top]);
            // Toe pad: three broad contact patches beat a knife edge on a desk.
            translate([FOOT_DEPTH - 18, 0, 0]) cube([18, 4, FOOT_W]);
        }

        // Bolt holes driven perpendicular to the MOUNTING face, not to the
        // world -- otherwise the screws enter at an angle and split the wedge.
        for (u = [22, 52])                      // distance up the mount face
            for (z = [FOOT_W/2 - 13, FOOT_W/2 + 13])
                translate([u * sin(TILT), u * cos(TILT), z])
                    rotate([0, 0, -TILT])
                        rotate([0, 90, 0])
                            translate([0, 0, -14])
                                cylinder(d=3.4, h=20, $fn=32);

        // Pockets on BOTH outer faces rather than a hole straight through:
        // that keeps a solid web down the middle and full-thickness side walls
        // where the bolts land. Print at 15% infill and it is still light.
        for (zoff = [-1, FOOT_W - 5])
            translate([0, 0, zoff])
                linear_extrude(6)
                    offset(r = -8)
                        polygon([[0, 0], [FOOT_DEPTH, 0], top]);
    }
}

// ---------------------------------------------------------------------------
// Preview / export
//   Set PART to "shell", "foot", or "preview".
// ---------------------------------------------------------------------------

PART = "preview";

if (PART == "shell") shell();
else if (PART == "foot") foot();
else if (PART == "bezel") bezel();
else {
    // Assembled on a desk, seen from behind.
    //
    // Both transforms put the mounting plane in the same place:
    //   shell  rotate([90-TILT,0,0]) sends its back face (z=0) to a plane
    //          whose "up" direction is (0, sin TILT, cos TILT)
    //   foot   rotate([0,0,90]) rotate([90,0,0]) sends its mount face, which
    //          runs (sin TILT, cos TILT) in profile, to the same direction
    // so the two faces are coplanar and the panel leans back by exactly TILT.
    color("#8a97a6")
        for (fx = [BOARD_W*0.28, BOARD_W*0.72])
            translate([fx - FOOT_W/2, 0, 0])
                rotate([0, 0, 90]) rotate([90, 0, 0]) foot();

    color("#3a4453") rotate([90 - TILT, 0, 0]) shell();

    // Bezel, flipped to face the viewer and set forward by the board stack.
    color("#59636f")
        rotate([90 - TILT, 0, 0])
            translate([0, 0, WALL + REAR_CLEARANCE + BOARD_THICK])
                mirror([0, 0, 1]) bezel();

    // Desk, for scale.
    color("#20262f") translate([-30, -30, -3]) cube([310, 160, 3]);
}
