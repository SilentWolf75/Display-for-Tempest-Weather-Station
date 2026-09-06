// Desk stand + back cover for the Elecrow CrowPanel Advance 10.1" (ESP32-P4).
//
// Three printed parts:
//   housing() - front tray: window, walls, insert posts. Board drops in from
//               the BACK. Print face-down.
//   cover()   - back plate: vents, grilles, feet, sensor. Screws to the
//               housing posts. Does NOT go through the board.
//   foot()    - angled leg, x2, screws to the cover
//
// The board's own M3 corner holes sit 1.25 mm from the LCD module -- there is
// nowhere for a boss or an insert next to the glass. So those holes are not
// used. Four M3 screws pass through the cover into brass heat-set inserts in
// the 8 mm side walls. They never go through the board.
//
// Every dimension below that starts "MEASURED" came out of Elecrow's own Eagle
// PCB file for board revision V1.2:
//   Eagle_SCH&PCB/1.2/ESP32-P4 Display 10.1 inch V1.2.brd
// Board outline and every connector position were read from that file rather
// than estimated, so they are trustworthy to about 0.1 mm.
//
// Units: millimetres. Render with $fn=48 for a clean preview, 96 to export.
// Command line: openscad -D 'PART="housing"' -o housing.stl tempest_stand.scad
// -D wins over this default, which is what check_geometry.py relies on.
PART = "preview";

// ---------------------------------------------------------------------------
// Board stack -- MEASURED, from Elecrow's own STEP model of the assembly
// (ESP32-P4-10_1-inch-20251230.stp, board representation 247.00 x 147.00).
// Read off the model, with the PCB's front face as the reference:
//
//     glass front surface      +2.00
//     active area plane        -0.20     222.7 x 125.3
//     LCD module footprint     -4.80     235.5 x 143.5, centred on the board
//     PCB front face           -4.90
//     PCB back face            -6.50     PCB is 1.60 mm
//     rear-most extent        -19.62     199.0 x 76.5
// ---------------------------------------------------------------------------

REAR_CLEARANCE = 13.5;      // model says 13.12; 0.4 mm of air on top
FRONT_GLASS    = 6.9;       // glass +2.00 over PCB front -4.90
BOARD_THICK    = 8.5;       // glass +2.00 to PCB back -6.50

// ---------------------------------------------------------------------------
// Window -- left is the reference.
//
// The first print used LIT_DY = +3, assuming +y was the top of the screen.
// A later print proved that wrong: raising ACTIVE_Y cropped the status bar
// (BAT / SKY) and left the black bar under the alert ticker. So y = 0 is
// the status-bar edge. Open toward y = 0 for more room at the top; drop
// the high-y edge to hide the unlit strip at the bottom. Left stays.
// ---------------------------------------------------------------------------

LIT_W = 222.7;
LIT_H = 125.3;
LIT_DX = 0;
LIT_DY = 0;
ACTIVE_MARGIN = 2.5;

BOARD_W = 247.04;
BOARD_H = 147.01;

// Glass-level opening, board coordinates.
// Left  9.67  -- unchanged, the reference
// Bottom of hole (status-bar edge) 4.5  -- was 11.4, then wrongly 18
// Top of hole (alert-ticker edge) 135.0 -- was 141.7, then wrongly 144.5
ACTIVE_X = (BOARD_W - LIT_W) / 2 + LIT_DX - ACTIVE_MARGIN;
ACTIVE_Y = 4.5;
ACTIVE_W = LIT_W + 2 * ACTIVE_MARGIN;
ACTIVE_H = 130.5;
// right of hole = 9.67 + 227.7 = 237.37 (same as the left inset)

MODULE_W = 235.5;
MODULE_H = 143.5;
MODULE_X = (BOARD_W - MODULE_W) / 2;
MODULE_Y = (BOARD_H - MODULE_H) / 2;

// M3.2 through-holes, 3.1 mm in from each edge. Origin is the bottom-left
// corner of the board as seen from the FRONT. Not used: they sit 1.25 mm
// from the LCD, and the cover pads that used to land on them hit the
// alignment lip plus SD / BOOT / Grove / USB parts on the back.
HOLE_D       = 3.2;
HOLE_INSET_X = 3.1;
HOLE_INSET_Y = 3.0;

// Spacer pads on the COVER, pressing on the PCB back. Front-view x,y.
// Lower pair sit in the centre of each foot pad (between the 4 bolt holes).
// Upper pair is empty pour, clear of speakers / CSI / silkscreen.
PAD_D = 8;
pcb_pads = [
    [ 70,  40],     // centre of the left foot pad
    [178,  40],     // centre of the right foot pad
    [ 80, 108],     // empty pour under the CrowPanel silkscreen
    [168, 108],     // empty pour below CSI-CAM, inboard of the left speaker
];
PAD0_X = pcb_pads[0][0];
PAD0_Y = pcb_pads[0][1];
PAD2_X = pcb_pads[2][0];
PAD2_Y = pcb_pads[2][1];

// ---------------------------------------------------------------------------
// Room in the back
//
// Cover stacking, from the outside back plate forward: BAY_DEPTH of air,
// then the board's rear components in REAR_CLEARANCE, then the PCB.
// ---------------------------------------------------------------------------

BAY_DEPTH = 2;

// Was 12, for an internal LiPo. Dropped to 2 to take 10 mm off the depth.
BATTERY_BAY = false;

BAT_W = 132; BAT_H = 75;
BAT_FENCE = 8;
BAT_X = (BOARD_W - BAT_W) / 2;
BAT_Y = 66;

// microSD (TF-SMD). From ESP32-P4-10_1-inch-20251230.stp, product TF-SMD
// at (104.125, -6.50, 61.306) with origin at board centre. USB-C in that
// file sits at x≈4, y=63/84 -- already front-view, matching the left edge
// facing the screen -- so do not mx() this. Mouth is the TOP edge, BOOT side.
SD_X = 227.6;
SD_W = 17;
SD_DROP = 3.5;

// Indoor sensor module (AHT20/DHT20), on the inside of the cover, grille
// through a window so it reads room air rather than panel waste heat.
// One flat seat at SENSOR_FACE (the old 2.4 mm plate) under the whole
// module -- a small well under just the hole left the header sitting on
// the 3.6 mm cover, so the board rocked. The hole is only as big as the
// sensor body so the PCB has a ledge and cannot fall in.
SENSOR_WIN_L    = 16;
SENSOR_WIN_W    = 10;
SENSOR_WIN_X    = 58.0;     // 1 mm closer to the post than 57
SENSOR_WIN_Y    = 97;
SENSOR_SCREW_DX = 11.75;    // post stays at 69.75
SENSOR_POST_D   = 7;
SENSOR_POST_H   = 5;
SENSOR_PILOT    = 2.9;
SENSOR_PILOT_DP = 4.6;
SENSOR_FACE     = 2.4;      // local plate thickness; rest of cover is WALL
SENSOR_WALL_H   = 3.0;      // heat shield around the hole, three sides
SENSOR_WALL_T   = 1.5;      // open toward the screw / DuPonts
SENSOR_SEAT_W   = 20;       // full-module bed, Y
SENSOR_SEAT_PAD = 3;        // extra XY around hole / post
// Right edge of the bed. The 4-pin DuPont and the first run of
// wire land here -- past the post, just shy of the vent column at x=100.
SENSOR_SEAT_X1  = 96;

// Speakers are stuck to the BACK OF THE PCB. Grilles in the cover only.
// Board-data (back-view) coordinates; measure the real centres if sound is
// muffled -- being a few mm out only costs volume.
SPK_W = 46; SPK_H = 36;
speakers = [[25, 58], [216, 58]];

// ---------------------------------------------------------------------------
// Case
// ---------------------------------------------------------------------------

WALL     = 3.6;         // cover plate; was 2.4. Nine perimeters at 0.4 mm.
FRAME    = 10.0;        // housing wall. 8 swallowed the inserts, then the
                        // cover well stole 1.5 mm and left them on the rim;
                        // 10 restores ~2 mm of plastic around each knurl.
FIT_GAP  = 0.6;         // slack around the board so it drops in
CORNER_R = 6;
SEAM     = 0.2;         // housing-to-cover gap at the seat
COVER_RECESS = WALL;    // well as deep as the plate so the back sits flush
RECESS_LIP   = 1.5;     // housing wall wrapping the cover
COVER_CLEAR  = 0.3;     // air around the cover in the well
COVER_IN     = RECESS_LIP + COVER_CLEAR;

SHELL_W = BOARD_W + 2 * FIT_GAP + 2 * FRAME;
SHELL_H = BOARD_H + 2 * FIT_GAP + 2 * FRAME;

// Cover: z = 0 is the outside back (on the bed). PCB back sits at PCB_Z.
PCB_Z = WALL + BAY_DEPTH + REAR_CLEARANCE;

// M3 button head, ISO 7380: 5.7 mm across, 1.65 mm tall.
SCREW_CLR       = 3.4;
SCREW_HEAD_D    = 6.2;
SCREW_HEAD_SEAT = 2.0;

// Housing face. No longer has to hide a 30 mm screw, so it can be a face
// instead of a block.
FACE_T     = 3.0;
BEZEL_LIP  = 0;         // visible opening IS ACTIVE_*; margin is ACTIVE_MARGIN

// Touch ramp: the opening is larger at the front than at the glass, so a
// finger reaches the pixels instead of hitting a square 3 mm well. Printed
// face-down this is an inward overhang of atan(CHAMFER/FACE_T) from vertical
// -- 45° if they match, ~59° at 5 mm. The old 45° ring printed badly because
// it was a knife-edge hull on a 7 mm face; this stops at a short flat lip
// against the glass so the edge is a face, not a point.
CHAMFER    = 5.0;       // how far the slope runs out from the glass opening
LIP_FLAT   = 0.8;       // vertical land at the glass before the slope starts

// Housing depth, z = 0 at the front face. The cover lands on SEAT_Z;
// the outer walls continue COVER_RECESS past that so the plate drops in.
SEAT_Z    = FACE_T + BOARD_THICK + REAR_CLEARANCE + BAY_DEPTH - SEAM;
HOUSING_D = SEAT_Z + COVER_RECESS;

LIP_H     = 2.0;        // cover alignment lip, tucks into the housing
LIP_CLEAR = 0.35;

TILT = 18;

// ---------------------------------------------------------------------------
// Case screws -- in the FRAME, not in nubs
//
// The board's own holes are 1.25 mm from the LCD, so they are unused. Four
// M3s stay in the left/right walls (clear of USB-C at y = 63 and 84). Six
// more clamp the long edges -- three along the top, three along the bottom
// -- so the cover can hold the board without the sides bowing. Nothing
// shows on the front.
// ---------------------------------------------------------------------------

// M3 knurled heat-set inserts, 4.2 mm OD. You have 4 mm, 6 mm and 8 mm
// lengths (M3x4x4.2, M3x6x4.2, M3x8x4.2). 6 mm is the default -- enough
// thread, and a 6 mm well under the insert means an M3x12 cannot bottom.
// Set INSERT_LEN to 4 or 8 if you fit those instead. Same 4.2 mm knurl,
// so the melt diameter does not change.
INSERT_LEN    = 6;      // 4, 6 or 8
INSERT_D      = 4.0;    // melt pocket, slightly under the 4.2 mm knurl
INSERT_D_TOP  = 4.2;    // mouth matches the insert OD so it starts square
INSERT_H      = INSERT_LEN + 0.2;
INSERT_RELIEF = 6;      // M3 clearance below the insert, for leftover screw
INSERT_CHAM   = 0.7;    // 45-degree lead-in for the iron
INSERT_BOSS_D = FRAME;  // pad on the cover, fills the rim, does not bulge out
// Centre of the remaining seat (FRAME minus the wrapping lip).
CASE_SCREW_XL = -FIT_GAP - (FRAME - RECESS_LIP) / 2;
CASE_SCREW_XR = BOARD_W + FIT_GAP + (FRAME - RECESS_LIP) / 2;
CASE_SCREW_Y0 = 18;
CASE_SCREW_Y1 = BOARD_H - 18;
CASE_SCREW_YB = -FIT_GAP - (FRAME - RECESS_LIP) / 2;
CASE_SCREW_YT = BOARD_H + FIT_GAP + (FRAME - RECESS_LIP) / 2;
CASE_SCREW_X0 = BOARD_W * 0.20;
CASE_SCREW_X1 = BOARD_W * 0.50;
CASE_SCREW_X2 = BOARD_W * 0.78;     // inboard of the top-edge SD slot at 228
COVER_PAD_H   = 0;      // no inner boss: it sat proud of the plate, same
                        // height as the alignment lip, and stopped the
                        // cover seating. The hole and counterbore remain.

USB_H = 10;             // compact slot, same idea as the microSD opening
                        // rather than a gash the full height of the wall

case_screws = [
    [CASE_SCREW_XL, CASE_SCREW_Y0],
    [CASE_SCREW_XL, CASE_SCREW_Y1],
    [CASE_SCREW_XR, CASE_SCREW_Y0],
    [CASE_SCREW_XR, CASE_SCREW_Y1],
    [CASE_SCREW_X0, CASE_SCREW_YB],
    [CASE_SCREW_X1, CASE_SCREW_YB],
    [CASE_SCREW_X2, CASE_SCREW_YB],
    [CASE_SCREW_X0, CASE_SCREW_YT],
    [CASE_SCREW_X1, CASE_SCREW_YT],
    [CASE_SCREW_X2, CASE_SCREW_YT],
];

// ---------------------------------------------------------------------------
// Connector cutouts (MEASURED, board coordinates, from the PCB file)
// ---------------------------------------------------------------------------

ALL_PORTS = false;

// Every position below was read from Elecrow's Eagle PCB file, whose top view
// is the board's COMPONENT side -- which on a display board is the BACK.
// This file assembles with the screen facing +z, so its x,y is a FRONT view.
// mx() mirrors board-data x before cutting.
//
// FIVE-SECOND CHECK: stand the panel up facing you. The two USB-C ports should
// be on your LEFT. If they are on your right, set this false and re-export.
BOARD_DATA_IS_BACK_VIEW = true;

function mx(x) = BOARD_DATA_IS_BACK_VIEW ? BOARD_W - x : x;

usb_cuts = [
    [ 63.0, 14],    // J16  USB-C
    [ 84.0, 14],    // J1   USB-C
];

right_cuts = [
    [ 41.3, 14],    // J10  XH2.54-4P
    [ 63.0, 14],    // J16  USB-C
    [ 84.0, 14],    // J1   USB-C
    [100.9, 16],    // SW1  power switch
];

top_cuts = [
    [ 18.1, 16],    // J2   Grove / Crowtail
    [ 40.1, 16],    // J13  Grove / Crowtail
    [215.5, 24],    // J9   GPIO header
    [231.0, 24],    // J11  GPIO header
];

bottom_cuts = [
    [ 35.8, 12],    // J4   PH2.0 2-pin
    [123.9, 38],    // J7   2x12 header
    [162.4, 12],    // J8   test points
    [215.1, 12],    // J3   PH2.0 2-pin
    [231.0, 12],    // J6   PH2.0 2-pin
];

back_access = [
    [   7.0,  25.0, 11, 30],    // K3 + K4
    [ 240.5, 100.9, 13, 15],    // SW1
];

BOOT_SLOT_X = mx(7.0);
SPK0_X      = mx(speakers[1][0]);
SW_SLOT_X   = mx(240.5);

left_cuts = [
    [135.1, 8],     // U176 microphone
];

// ---------------------------------------------------------------------------
// Feet -- shared by cover(), foot(), and the preview so they cannot drift.
// ---------------------------------------------------------------------------

FOOT_X          = [BOARD_W * 0.28, BOARD_W * 0.72];
FOOT_BOLT_U     = [22, 52];
FOOT_BOLT_DZ    = [-13, 13];
FOOT_BOLT_DEPTH = 8;
FOOT_W          = 40;
FOOT_DEPTH      = 92;
FOOT_H          = 74;

// ---------------------------------------------------------------------------

module rounded_box(w, h, d, r) {
    hull() for (x = [r, w - r], y = [r, h - r])
        translate([x, y, 0]) cylinder(r = r, h = d, $fn = 48);
}

// Outer silhouette of housing and cover. Screws live inside FRAME, so this
// is a plain rounded rectangle -- no nubs.
module case_outline(d) {
    translate([-FIT_GAP - FRAME, -FIT_GAP - FRAME, 0])
        rounded_box(SHELL_W, SHELL_H, d, CORNER_R);
}

module cover_outline(d) {
    translate([-FIT_GAP - FRAME + COVER_IN, -FIT_GAP - FRAME + COVER_IN, 0])
        rounded_box(SHELL_W - 2 * COVER_IN, SHELL_H - 2 * COVER_IN, d,
                    max(1, CORNER_R - COVER_IN));
}

module vent_grid() {
    for (x = [100 : 22 : 150])
        for (y = [28 : 18 : BOARD_H - 28])
            if (!(min([for (fx = FOOT_X) abs(x - fx)]) < 24
                  && y < FOOT_BOLT_U[1] + 14)
                && !in_bay(x, y)
                && !on_pcb_pad(x, y)
                && !(x > SENSOR_WIN_X - 16
                     && x < SENSOR_SEAT_X1 + 4
                     && abs(y - SENSOR_WIN_Y) < SENSOR_SEAT_W / 2 + 4))
            translate([x, y, -1])
                hull() {
                    translate([0, -5, 0]) cylinder(d = 4, h = WALL + 2, $fn = 24);
                    translate([0,  5, 0]) cylinder(d = 4, h = WALL + 2, $fn = 24);
                }
}

module slot_through_x(x0, y_c, z0, w, h, t, r) {
    translate([x0, y_c, z0 + h / 2])
        rotate([0, 90, 0])
            hull()
                for (a = [-(h / 2 - r), h / 2 - r], b = [-(w / 2 - r), w / 2 - r])
                    translate([a, b, 0]) cylinder(r = r, h = t, $fn = 24);
}

module slot_through_y(x_c, y0, z0, w, h, t, r) {
    translate([x_c, y0, z0 + h / 2])
        rotate([-90, 0, 0])
            hull()
                for (a = [-(w / 2 - r), w / 2 - r], b = [-(h / 2 - r), h / 2 - r])
                    translate([a, b, 0]) cylinder(r = r, h = t, $fn = 24);
}

// USB (and optional extra ports) in the HOUSING walls. Compact rounded
// slots at the connector, like the microSD opening -- not a gash the full
// height of the wall.
module housing_edge_cutouts() {
    z_pcb_back = FACE_T + BOARD_THICK;
    z0 = z_pcb_back - 2;
    x_lo = -FIT_GAP - FRAME - 2;
    x_hi = BOARD_W + FIT_GAP - 1;
    t = FRAME + 3;

    hi = !BOARD_DATA_IS_BACK_VIEW;
    for (c = ALL_PORTS ? right_cuts : usb_cuts)
        slot_through_x(hi ? x_hi : x_lo,
                       c[0], z0, c[1], USB_H, t, 1.5);

    for (c = ALL_PORTS ? left_cuts : [])
        slot_through_x(hi ? x_lo : x_hi,
                       c[0], z0, c[1], USB_H, t, 1.5);

    for (c = ALL_PORTS ? top_cuts : [])
        slot_through_y(mx(c[0]), BOARD_H + FIT_GAP - 1, z0,
                       c[1], USB_H, t, 1.5);
    for (c = ALL_PORTS ? bottom_cuts : [])
        slot_through_y(mx(c[0]), -FIT_GAP - FRAME - 2, z0,
                       c[1], USB_H, t, 1.5);
}

module back_access_slots() {
    for (a = back_access) {
        w = a[2]; h = a[3]; r = 3;
        translate([mx(a[0]), a[1], -1])
            hull()
                for (dx = [-(w / 2 - r), w / 2 - r], dy = [-(h / 2 - r), h / 2 - r])
                    translate([dx, dy, 0]) cylinder(r = r, h = WALL + 2, $fn = 32);
    }
}

module fence(x, y, w, h, tall, gap_at_x) {
    t = 2.4;
    difference() {
        translate([x - t, y - t, WALL])
            cube([w + 2 * t, h + 2 * t, tall]);
        translate([x, y, WALL - 1])
            cube([w, h, tall + 2]);
        translate([gap_at_x, y - t - 1, WALL + tall - 6])
            cube([14, t + 2, 7]);
    }
}

module battery_bay() { fence(BAT_X, BAT_Y, BAT_W, BAT_H, BAT_FENCE, BAT_X + 8); }

module sensor_mount() {
    translate([SENSOR_WIN_X + SENSOR_SCREW_DX, SENSOR_WIN_Y, SENSOR_FACE])
        cylinder(d = SENSOR_POST_D, h = SENSOR_POST_H, $fn = 40);
}

function sensor_seat_x0() = SENSOR_WIN_X - SENSOR_WIN_L / 2
    - SENSOR_WALL_T - SENSOR_SEAT_PAD;
function sensor_seat_y0() = SENSOR_WIN_Y - SENSOR_SEAT_W / 2;
function sensor_seat_x1() = SENSOR_SEAT_X1;
function sensor_seat_y1() = SENSOR_WIN_Y + SENSOR_SEAT_W / 2;

// One flat 2.4 mm bed under the whole module, not a well under the hole.
module sensor_sink() {
    x0 = sensor_seat_x0();
    y0 = sensor_seat_y0();
    translate([x0, y0, SENSOR_FACE])
        rounded_box(sensor_seat_x1() - x0, sensor_seat_y1() - y0,
                    WALL - SENSOR_FACE + 0.05, 2);
}

// U-shaped fence around the hole, open toward the screw. Blocks display
// waste heat from washing over the sensing end.
module sensor_hood() {
    t = SENSOR_WALL_T;
    l = SENSOR_WIN_L;
    w = SENSOR_WIN_W;
    difference() {
        translate([SENSOR_WIN_X - l / 2 - t,
                   SENSOR_WIN_Y - w / 2 - t,
                   SENSOR_FACE])
            cube([l + t, w + 2 * t, SENSOR_WALL_H]);
        translate([SENSOR_WIN_X - l / 2 - 0.05,
                   SENSOR_WIN_Y - w / 2,
                   SENSOR_FACE - 0.1])
            cube([l + t + 1, w, SENSOR_WALL_H + 0.2]);
    }
}

module sensor_window() {
    translate([SENSOR_WIN_X, SENSOR_WIN_Y, -1])
        hull()
            for (dx = [-(SENSOR_WIN_L / 2 - 2), SENSOR_WIN_L / 2 - 2],
                 dy = [-(SENSOR_WIN_W / 2 - 2), SENSOR_WIN_W / 2 - 2])
                translate([dx, dy, 0])
                    cylinder(r = 2, h = WALL + SENSOR_WALL_H + 2, $fn = 24);

    translate([SENSOR_WIN_X + SENSOR_SCREW_DX, SENSOR_WIN_Y,
               SENSOR_FACE + SENSOR_POST_H - SENSOR_PILOT_DP])
        cylinder(d = SENSOR_PILOT, h = SENSOR_PILOT_DP + 0.1, $fn = 32);
}

function on_foot_pad(x, y) =
    min([for (fx = FOOT_X) abs(x - fx)]) < 21
    && y > FOOT_BOLT_U[0] - 12 && y < FOOT_BOLT_U[1] + 12;

function on_pcb_pad(x, y) =
    min([for (h = pcb_pads)
         sqrt((x - h[0]) * (x - h[0]) + (y - h[1]) * (y - h[1]))])
    < PAD_D / 2 + 5;

function pad_over_sensor_seat(x, y) =
    x > sensor_seat_x0() - PAD_D / 2 && x < sensor_seat_x1() + PAD_D / 2
    && y > sensor_seat_y0() - PAD_D / 2 && y < sensor_seat_y1() + PAD_D / 2;

module speaker_grilles() {
    for (c = speakers) {
        cx = mx(c[0]);
        /* BOOT-side grille (front-view x ~222): the outer slot sat past
         * the speaker. Shift that bank 6 mm inboard so the edge slot
         * moves into the gap next to the foot pad. */
        i0 = (cx > BOARD_W / 2) ? -3 : -2;
        for (i = [i0 : 1 : i0 + 4])
            if (!on_foot_pad(cx + i * 6, c[1]))
            translate([cx + i * 6, c[1], -1])
                hull() {
                    translate([0, -SPK_H / 2 + 6, 0]) cylinder(d = 3.4, h = WALL + 2, $fn = 20);
                    translate([0,  SPK_H / 2 - 6, 0]) cylinder(d = 3.4, h = WALL + 2, $fn = 20);
                }
    }
}

module housing_sd_slot() {
    z_pcb_back = FACE_T + BOARD_THICK;
    slot_through_y(SD_X, BOARD_H + FIT_GAP - 1, z_pcb_back - 1,
                   SD_W, SD_DROP + 1, FRAME + 3, 1.5);
}

function in_bay(x, y) =
    (BATTERY_BAY &&
     x > BAT_X - 6 && x < BAT_X + BAT_W + 6 &&
     y > BAT_Y - 6 && y < BAT_Y + BAT_H + 6)
    || max([for (c = speakers)
            (abs(x - mx(c[0])) < SPK_W / 2 + 8
             && abs(y - c[1]) < SPK_H / 2 + 8) ? 1 : 0]) > 0;

// ---------------------------------------------------------------------------
// Cover -- back plate. z = 0 is the outside (on the bed).
// ---------------------------------------------------------------------------

module cover() {
    difference() {
        union() {
            difference() {
                cover_outline(WALL);
                sensor_sink();
                // Plate only -- the alignment lip is unioned on afterwards
                // so BOOT / SW1 cannot punch a hole in it.
                back_access_slots();
            }

            // Spacer pads meet empty pour on the PCB back so the stack
            // cannot rattle. No screw goes through them. A pad that lands
            // on the sensor well is grown from SENSOR_FACE so it is not
            // a floating stub on the 3.6 mm plate.
            for (h = pcb_pads) {
                z0 = pad_over_sensor_seat(h[0], h[1]) ? SENSOR_FACE : WALL;
                translate([h[0], h[1], z0])
                    cylinder(d = PAD_D, h = PCB_Z - z0, $fn = 48);
            }

            if (BATTERY_BAY) battery_bay();
            sensor_mount();
            sensor_hood();

            for (fx = FOOT_X)
                translate([fx - 20, FOOT_BOLT_U[0] - 11, WALL])
                    cube([40, FOOT_BOLT_U[1] - FOOT_BOLT_U[0] + 22, 3]);

            // Alignment lip tucks into the board cavity past the seat.
            difference() {
                translate([-FIT_GAP + LIP_CLEAR, -FIT_GAP + LIP_CLEAR, WALL])
                    cube([BOARD_W + 2 * FIT_GAP - 2 * LIP_CLEAR,
                          BOARD_H + 2 * FIT_GAP - 2 * LIP_CLEAR,
                          LIP_H]);
                translate([LIP_CLEAR, LIP_CLEAR, WALL - 1])
                    cube([BOARD_W - 2 * LIP_CLEAR,
                          BOARD_H - 2 * LIP_CLEAR,
                          LIP_H + 2]);
            }
        }

        for (s = case_screws) {
            translate([s[0], s[1], -1])
                cylinder(d = SCREW_CLR, h = WALL + COVER_PAD_H + 2, $fn = 32);
            translate([s[0], s[1], -0.01])
                cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_SEAT, $fn = 40);
        }

        vent_grid();
        speaker_grilles();
        sensor_window();

        for (fx = FOOT_X)
            for (u = FOOT_BOLT_U)
                for (dz = FOOT_BOLT_DZ)
                    translate([fx + dz, u, -1])
                        cylinder(d = 3.4, h = WALL + 6, $fn = 24);
    }
}

// ---------------------------------------------------------------------------
// Housing -- front tray. z = 0 is the visible face (on the bed).
// Board drops in from the open back; glass lands on the window lip.
// ---------------------------------------------------------------------------

module housing() {
    difference() {
        case_outline(HOUSING_D);

        // Board cavity, open at the seat. Starts behind the face so the
        // glass sits on the inner surface around the window.
        translate([-FIT_GAP, -FIT_GAP, FACE_T])
            cube([BOARD_W + 2 * FIT_GAP, BOARD_H + 2 * FIT_GAP,
                  SEAT_Z - FACE_T + 0.1]);

        // Cover well: the plate drops COVER_RECESS into the back, wrapped
        // by RECESS_LIP of wall so the rim takes shear, not just screw tension.
        translate([-FIT_GAP - FRAME + RECESS_LIP,
                   -FIT_GAP - FRAME + RECESS_LIP,
                   SEAT_Z])
            rounded_box(SHELL_W - 2 * RECESS_LIP, SHELL_H - 2 * RECESS_LIP,
                        COVER_RECESS + 0.2, max(1, CORNER_R - RECESS_LIP));

        // Window + touch ramp. Large at the front (on the bed), tight at the
        // glass. A separate short land at the glass keeps that edge printable.
        hull() {
            translate([ACTIVE_X - CHAMFER, ACTIVE_Y - CHAMFER, -1])
                cube([ACTIVE_W + 2 * CHAMFER, ACTIVE_H + 2 * CHAMFER, 0.02]);
            translate([ACTIVE_X, ACTIVE_Y, FACE_T - LIP_FLAT])
                cube([ACTIVE_W, ACTIVE_H, 0.02]);
        }
        translate([ACTIVE_X, ACTIVE_Y, FACE_T - LIP_FLAT - 0.01])
            cube([ACTIVE_W, ACTIVE_H, LIP_FLAT + 0.12]);

        // Brass heat-set inserts (M3 x INSERT_LEN x 4.2), melted in from the
        // BACK after printing face-down. Tapered pocket so the knurl grabs;
        // chamfer so the iron starts square; M3 well below so a long screw
        // cannot bottom. Blind toward the front -- nothing shows.
        for (s = case_screws) {
            translate([s[0], s[1], SEAT_Z - INSERT_H - INSERT_RELIEF])
                cylinder(d = SCREW_CLR, h = INSERT_RELIEF + 0.1, $fn = 32);
            translate([s[0], s[1], SEAT_Z - INSERT_H])
                cylinder(d1 = INSERT_D, d2 = INSERT_D_TOP,
                         h = INSERT_H + 0.1, $fn = 32);
            translate([s[0], s[1], SEAT_Z - INSERT_CHAM])
                cylinder(d1 = INSERT_D_TOP,
                         d2 = INSERT_D_TOP + 2 * INSERT_CHAM,
                         h = INSERT_CHAM + 0.1, $fn = 32);
        }

        housing_edge_cutouts();
        housing_sd_slot();
    }
}

// ---------------------------------------------------------------------------
// Foot
// ---------------------------------------------------------------------------

module foot() {
    top = [FOOT_H * sin(TILT), FOOT_H * cos(TILT)];

    difference() {
        union() {
            linear_extrude(FOOT_W)
                polygon([[0, 0], [FOOT_DEPTH, 0], top]);
            translate([FOOT_DEPTH - 18, 0, 0]) cube([18, 4, FOOT_W]);
        }

        for (u = FOOT_BOLT_U)
            for (dz = FOOT_BOLT_DZ)
                translate([u * sin(TILT), u * cos(TILT), FOOT_W / 2 + dz])
                    rotate([0, 0, -TILT])
                        rotate([0, 90, 0])
                            translate([0, 0, -14])
                                cylinder(d = 2.9, h = 14 + FOOT_BOLT_DEPTH, $fn = 32);

        for (zoff = [-1, FOOT_W - 5])
            translate([0, 0, zoff])
                linear_extrude(6)
                    offset(r = -8)
                        polygon([[0, 0], [FOOT_DEPTH, 0], top]);
    }
}

// Preview / export. PART is set at the top; -D overrides it.

PCB_ONLY = BOARD_THICK - FRONT_GLASS;

// Cover screws: through the plate into the insert. Extra length
// disappears into INSERT_RELIEF instead of bottoming.
SCREW_REACH = WALL - SCREW_HEAD_SEAT + COVER_PAD_H + SEAM;
SCREW_MIN   = SCREW_REACH;
SCREW_MAX   = SCREW_REACH + INSERT_H + INSERT_RELIEF;

echo(str("window ", ACTIVE_W, " x ", ACTIVE_H,
         " at (", ACTIVE_X, ", ", ACTIVE_Y, "); margin ", ACTIVE_MARGIN,
         " mm; LIT_DY ", LIT_DY, "; chamfer ", CHAMFER));
echo(str("window inside module: x ",
         ACTIVE_X - MODULE_X, " .. ",
         (MODULE_X + MODULE_W) - (ACTIVE_X + ACTIVE_W),
         "  y ",
         ACTIVE_Y - MODULE_Y, " .. ",
         (MODULE_Y + MODULE_H) - (ACTIVE_Y + ACTIVE_H)));
echo(str("outline ", SHELL_W, " x ", SHELL_H, " -- keep under 270"));
echo(str("M3 BUTTON head (under-head length): at least ", SCREW_REACH,
         " to reach the insert, at most ", SCREW_MAX,
         " before it bottoms. M3 x 12 is the fit with INSERT_LEN=",
         INSERT_LEN, " mm inserts."));
echo(str("cover ", WALL, " mm plate, recessed ", COVER_RECESS,
         " mm into the housing; ", WALL - COVER_RECESS, " mm stays proud"));
echo(str("housing face in front of each insert: ",
         SEAT_Z - INSERT_H - INSERT_RELIEF, " mm of wall, not the screen"));

if (PART == "cover" || PART == "shell") cover();
else if (PART == "foot") foot();
else if (PART == "housing" || PART == "bezel") housing();
else if (PART == "none") ;
else {
    color("#8a97a6")
        for (fx = FOOT_X)
            translate([fx - FOOT_W / 2, 0, 0])
                rotate([0, 0, 90]) rotate([90, 0, 0]) foot();

    color("#3a4453") rotate([90 - TILT, 0, 0]) cover();

    // Housing is modelled face at local z = 0. After mirror, that face sits
    // at glass-front + FACE_T. The back walls wrap COVER_RECESS around the
    // plate; WALL - COVER_RECESS of the cover stays proud.
    color("#59636f")
        rotate([90 - TILT, 0, 0])
            translate([0, 0, PCB_Z + BOARD_THICK + FACE_T])
                mirror([0, 0, 1]) housing();

    color("#20262f") translate([-30, -30, -3]) cube([310, 160, 3]);
}
