// Desk stand + back cover for the Elecrow CrowPanel Advance 10.1" (ESP32-P4).
//
// Three printed parts:
//   shell()  - back cover
//   bezel()  - front frame, carries four M3x6x4.2 brass heat-set inserts
//   foot()   - angled leg, x2, screws to the shell
//
// The four main screws enter from the BACK, pass through the shell, through
// the board's own M3 corner holes, and thread into the brass inserts in the
// bezel. Shell and bezel therefore clamp the board between them, and the
// threads are in brass rather than in printed plastic.
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
// Board stack -- MEASURED, from Elecrow's own STEP model of the assembly
// (ESP32-P4-10_1-inch-20251230.stp, board representation 247.00 x 147.00).
// These four were guesses until that model turned up, and all four were wrong.
// Read off the model, with the PCB's front face as the reference:
//
//     glass front surface      +2.00
//     active area plane        -0.20     222.7 x 125.3
//     LCD module footprint     -4.80     235.5 x 143.5, centred on the board
//     PCB front face           -4.90
//     PCB back face            -6.50     PCB is 1.60 mm
//     rear-most extent        -19.62     199.0 x 76.5
// ---------------------------------------------------------------------------

// Distance from the back surface of the PCB to the highest thing standing on
// it -- usually the ESP32-C6 module can, an electrolytic cap, or the tallest
// connector body. Put a straight edge across the back and measure the gap.
// Add 1.5 mm of air on top of whatever you read.
//
// If it is wrong the shell will either bow the board (too small) or stand
// proud of it (too large).
REAR_CLEARANCE = 13.5;      // model says 13.12; 0.4 mm of air on top

// How far the display surface stands PROUD of the PCB's front face. The bezel
// is recessed by this much so it lies flat on the glass instead of on the PCB
// and rocking.
FRONT_GLASS = 6.9;          // glass +2.00 over PCB front -4.90

// Total board thickness, front glass to back of PCB. Sets screw length.
BOARD_THICK = 8.5;          // glass +2.00 to PCB back -6.50

// Visible active area of the panel and where its bottom-left corner sits
// relative to the board's bottom-left corner, seen from the FRONT.
//
// The STEP model puts the lit area at 222.7 x 125.3, and says it is NOT quite
// centred in the LCD module -- off by 0.65 mm one way and 3.0 mm the other.
// The model's in-plane axes cannot be tied to a left/right/up/down without an
// anchor I do not have, so the SIGN of that 3 mm offset is unknown.
//
// So the window is sized to be right either way: 226 x 135, centred. That
// clears the lit area with margin whichever way the offset runs, while still
// staying inside the LCD module (235.5 x 143.5, which IS centred), so no bare
// PCB shows through. The error is deliberately one-sided -- a window slightly
// too big shows a sliver of black module border, one too small covers pixels
// and cannot be undone once printed.
//
// Power the panel on, measure the lit rectangle, and tighten these if you
// want a narrower frame.
ACTIVE_W = 226;
ACTIVE_H = 135;
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
// Room in the back for a battery and the two speakers
//
// The whole shell is deepened by BAY_DEPTH rather than growing a hump: the
// back stays flat for the feet, the vents stay usable, and there is somewhere
// to put the speakers wherever their wires happen to reach.
//
// Stacking, from the back plate forward: battery and speakers sit on the
// inside of the plate and occupy BAY_DEPTH; the board's own rear components
// occupy REAR_CLEARANCE above that.
// ---------------------------------------------------------------------------

BAY_DEPTH = 2;              // clear depth over the back plate

// Was 12, for an internal LiPo. Dropped to 2 to take 10 mm off the depth of
// the shell, which you measured as safe. That removes the battery bay -- a
// flat pack no longer fits -- so the panel is USB-powered. Put this back to 12
// and re-enable battery_bay() below if you ever want the battery.
BATTERY_BAY = false;

// Flat 3.7 V LiPo. Sized for a 10000 mAh pack, which also swallows a 5000.
// Common 10000 mAh packs run about 130 x 65 x 10; 5000 mAh about 100 x 55 x 8.
BAT_W = 132; BAT_H = 75;    // footprint, with slack
BAT_FENCE = 8;              // rib height holding it in place
BAT_X = (BOARD_W - BAT_W) / 2;
BAT_Y = 66;                 // clear of the foot pads, which end at y = 63

// microSD, on the BOTTOM edge near the BOOT/RESET corner. Board-data coords.
// Estimated off the photos; my two readings came out 16.6 and 19.9 mm, so
// CHECK THIS with a ruler before printing -- a slot a few mm out means the
// card will not go in at all, unlike the grilles where it only costs volume.
SD_X = 18;                  // <-- MEASURE from the board corner
SD_W = 17;                  // 1.5 mm of MATERIAL added each side of the 20
                            // that printed, so the opening gets smaller, not
                            // larger. I read "add 1.5 mm to the left and right
                            // side" as widening it and went to 23 -- wrong way.
                            // Card is 11 mm and the socket mouth ~15, so 17
                            // still clears both.
SD_DROP = 3.5;              // how far below the PCB plane the opening reaches.
                            // 7 was too deep, 3 a touch shallow.

// Indoor sensor module (AHT20/DHT20 on a narrow breakout), mounted on the
// inside of the back plate with its grille poking out through a 17 x 13
// opening. SENSOR_X/Y place it; move them if it fouls anything.
// The module screws down through its own M3 hole rather than sitting in a
// fence, so all that is needed is a window and one screw boss.
//
// It mounts sensor-side DOWN: the PCB lies flat on the inside of the back
// plate and the DHT20's grille pokes out through the window, which is the
// whole point -- sealed inside, it would read the panel's waste heat.
//
// The module stands off the plate on a single 5 mm post rather than lying on
// it, so the sensor body hangs in the window with its grille at the opening.
// Nothing projects outside the case.
SENSOR_WIN_L = 19;          // 17 plus the 1 mm each side you asked for
SENSOR_WIN_W = 15;          // 13 plus 1 mm each side
// 56, up from 54: you asked for the window closer to the screw post. The post
// is a 7 mm cylinder centred at SENSOR_WIN_X + SENSOR_SCREW_DX, so its near
// face sits at 66.25 and the window's edge now lands 0.75 mm short of it --
// about as close as it can go without the opening biting into the post.
SENSOR_WIN_X = 56.5;        // window centre -- where the grille comes through
SENSOR_WIN_Y = 97;

// Screw boss, offset from the window centre toward the pin-header end. The
// module's hole sits about 2.75 mm clear of the sensor body, and the body is
// roughly 15 mm long, so the hole lands ~10.5 mm from the body's centre.
// If your module measures differently this is the one number to change.
// Offset of the post from the window centre, toward the pin-header end.
// 14.5 is where you circled it on the render. It disagrees with the other way
// of arriving at the number -- a hole 2.75 mm clear of a ~15 mm body works out
// at 10.5 -- so if the screw misses the module's hole, change this one value.
// 15.75, up from 14.5: you asked for the post about 1.25 mm further "left"
// in a photo that is upside down, so in this model's coordinates that is +x.
//
// If the sensor body ends up MORE off-centre in the window rather than less,
// I read the flip backwards -- set this to 13.25 and it moves the other way by
// the same amount. It is the only value involved.
// 13.75, down from 15.75, purely to keep the POST where it is. The offset is
// measured from the window, so moving the window from 54 to 56 would have
// dragged the post 2 mm with it -- and the post is the one thing now confirmed
// correct, since the module screws down on it. Post stays at x = 69.75.
// 13.25: the window moved another 0.5 mm toward the post, so the offset drops
// by the same amount to leave the post exactly where it is at x = 69.75. The
// opening's edge now sits 0.25 mm off the post's face -- as close as it goes
// before the cut starts eating the post's 2.05 mm wall.
SENSOR_SCREW_DX = 13.25;
SENSOR_POST_D   = 7;
SENSOR_POST_H   = 5;        // INSIDE the case; the module sits on top of it
SENSOR_PILOT    = 2.9;      // self-tapping M3
SENSOR_PILOT_DP = 4.6;      // blind, so nothing breaks through the back

// Speakers. Photographs of the assembled board settle what the STEP model
// could not: the two speakers are stuck to the BACK OF THE PCB and wired to
// the SPK-L / SPK-R sockets. They are not case-mounted at all, which is why
// fences moulded into the back plate never lined up with them.
//
// So there are no fences any more -- just grilles in the back plate over
// wherever the speakers actually sit, giving the sound somewhere to go.
//
// These positions are BOARD-DATA (back-view) coordinates and are the one
// thing here I could not pin down: my estimates off the photos disagreed
// between shots by more than 20 mm, which is worse than useless for a grille.
// MEASURE the centre of each speaker from the board corners and put the real
// numbers here. Being a few mm out only muffles the sound; the grille is
// oversized to absorb that.
SPK_W = 46; SPK_H = 36;     // <-- MEASURE the speaker body (oversized grille)
// Five slots a side, and both grilles now sit clear of the foot pads so the
// guard suppresses nothing -- the count is the same on both sides by design
// rather than by accident.
//
// The left grille's five slots land at model x = 19..43, exactly where they
// already print, so that side is unchanged. The right one is its mirror at
// 204..228: clear of the pad, which ends at 198, and clear of the wall.
speakers = [[25, 58], [216, 58]];   // <-- MEASURE, board-data coords


// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

WALL      = 2.4;        // 6 perimeters at 0.4 mm
FIT_GAP   = 0.6;        // slack around the board so it drops in
CORNER_R  = 4;

SHELL_W = BOARD_W + 2*FIT_GAP + 2*WALL;     // ~253.8 -- fits a 270 mm bed
SHELL_H = BOARD_H + 2*FIT_GAP + 2*WALL;     // ~153.8
SHELL_D = REAR_CLEARANCE + BAY_DEPTH + WALL;
PCB_Z   = WALL + BAY_DEPTH + REAR_CLEARANCE;   // where the PCB's back sits

BOSS_D    = 10;         // spacer boss outside diameter; taller now, so wider
SCREW_CLR = 3.4;        // M3 clearance -- the screw passes THROUGH the shell

// M3 button head, ISO 7380: 5.7 mm across the head, 1.65 mm tall.
SCREW_HEAD_D    = 6.2;  // 5.7 plus clearance
SCREW_HEAD_SEAT = 2.0;  // bore depth; head finishes 0.35 mm below the surface

// M3 x 6 x 4.2 brass heat-set insert, threaded into the BEZEL.
// The screw enters from the back of the shell, passes through the board's own
// corner hole, and threads into brass rather than into printed plastic.
INSERT_D  = 4.2;        // outside diameter of the insert
INSERT_L  = 6.0;        // length
INSERT_FIT = -0.1;      // hole is slightly UNDER size; the brass melts its
                        // own seat. Go to 0 if your printer runs tight.

// Clearance past the end of the insert for the screw tip. Without it the only
// button-head length that fits is M3 x 20, which engages barely 2.5 mm of
// brass. With it, M3 x 25 fits and takes the full 6 mm of the insert -- and
// 25 is a size the assortment kit actually contains.
INSERT_CLEAR = 2.2;

TILT = 18;              // degrees off vertical

// ---------------------------------------------------------------------------
// Connector cutouts (MEASURED, board coordinates, from the PCB file)
//
// Each entry is [centre_position_along_that_edge, opening_width]. Openings are
// cut through the full depth of the wall so connector height cannot be wrong.
// ---------------------------------------------------------------------------

// ONLY the two USB-C ports are cut by default. Everything else on this board
// -- Grove, GPIO headers, PH2.0, test points, the microphone port, the power
// switch -- keeps its measured position below but is switched off, so the
// shell comes out as a clean back with two openings on the right edge.
//
// Set this true to open all of them again. The positions are read from the
// PCB file and are correct either way; this only decides which get cut.
ALL_PORTS = false;

// ---------------------------------------------------------------------------
// WHICH WAY ROUND THE BOARD DATA IS
//
// Every position below was read from Elecrow's Eagle PCB file, whose top view
// is the board's COMPONENT side -- which on a display board is the BACK. A
// photo of the model confirms it: the Grove pair (22 mm apart) sits on the
// left and the GPIO pair (15 mm apart) on the right, exactly as the file says,
// with the buttons and the power switch visible. So these are BACK-view
// coordinates.
//
// This file, though, assembles with the screen facing +z -- bezel on top, back
// plate at z=0 -- so its own x,y is a FRONT view. The two are mirror images:
// looking at the finished case from behind, this file's +x is on your LEFT
// while the board's +x is on your RIGHT.
//
// So every position taken from the PCB file gets mirrored through mx() before
// it is cut. Without this, all the openings come out on the wrong side.
//
// FIVE-SECOND CHECK: stand the panel up facing you. The two USB-C ports should
// be on your LEFT. If they are on your right, set this false and re-export.
BOARD_DATA_IS_BACK_VIEW = true;

function mx(x) = BOARD_DATA_IS_BACK_VIEW ? BOARD_W - x : x;

// Right edge of the BOARD DATA, positioned by Y (height up the board)
usb_cuts = [
    [ 63.0, 14],    // J16  USB-C
    [ 84.0, 14],    // J1   USB-C
];

// BOOT, RESET and the power switch are NOT on this edge -- they face out of
// the back of the board, so they get openings in the back plate instead. See
// back_access below.
//
// An earlier version of this file put them on this edge, on the strength of a
// STEP transform chain that turned out to be wrong. It also "corrected" the
// designators below; that correction was itself wrong and has been reverted.
// SW1 at y = 100.9 is the power switch, as the PCB file always said.

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

// ---------------------------------------------------------------------------
// Back-plate access for the controls that face backwards
//
// [x, y, w, h], centres in BOARD-DATA (back-view) coordinates, mirrored by
// mx() like everything else.
//
// These are SLOTS, not close-fitting holes, on purpose. The button positions
// come off a screenshot rather than a dimensioned drawing and are good to
// perhaps 3 mm; the switch comes from the PCB file and is good to 0.1 mm. A
// slot that is a few mm oversize costs nothing, and a hole in the wrong place
// cannot be undone. Tighten them once you can measure the real board.
back_access = [
    [   7.0,  25.0, 11, 30],    // K3 + K4, one slot covering both buttons.
                                // 13 x 34 printed a touch generous; both
                                // buttons still clear at 11 x 30.
    [ 240.5, 100.9, 13, 15],    // SW1 power slide switch, room to slide it
];

// Where those slots actually land in this file's coordinates, after the
// mirror. Exported so the geometry self-test can aim at them.
BOOT_SLOT_X = mx(7.0);
SPK0_X      = mx(speakers[1][0]);
SW_SLOT_X   = mx(240.5);

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
    // A deliberate block in the middle rather than a grid spanning the whole
    // plate. The plate has four things carved out of it now -- two foot pads,
    // two speaker grilles and the sensor mount -- and a full-width grid just
    // lost columns to all of them and came out lopsided. This range is the
    // strip that is clear of every one of them, so nothing gets clipped.
    for (x = [100 : 22 : 150])
        for (y = [28 : 18 : BOARD_H - 28])
            // Skip anything a foot pad would sit on top of and plug anyway.
            if (!(min([for (fx = FOOT_X) abs(x - fx)]) < 24
                  && y < FOOT_BOLT_U[1] + 14)
                && !in_bay(x, y)
                && !(x > SENSOR_WIN_X - 14
                     && x < SENSOR_WIN_X + SENSOR_SCREW_DX + 10
                     && abs(y - SENSOR_WIN_Y) < 16))
            translate([x, y, -1])
                hull() {
                    translate([0, -5, 0]) cylinder(d=4, h=WALL+2, $fn=24);
                    translate([0,  5, 0]) cylinder(d=4, h=WALL+2, $fn=24);
                }
}

/* Rounded rectangular openings through a wall.
 *
 * Square-cornered cuts print with a sharp internal corner that chips and
 * catches on a cable; a small radius costs nothing and looks deliberate.
 * Where an opening runs past the top of a wall the upper corners fall outside
 * the material, so only the bottom two are ever visible.
 *
 * _x is extruded along x for the side walls, _y along y for top and bottom.
 */
module slot_through_x(x0, y_c, z0, w, h, t, r) {
    translate([x0, y_c, z0 + h / 2])
        rotate([0, 90, 0])
            hull()
                for (a = [-(h/2 - r), h/2 - r], b = [-(w/2 - r), w/2 - r])
                    translate([a, b, 0]) cylinder(r=r, h=t, $fn=24);
}

module slot_through_y(x_c, y0, z0, w, h, t, r) {
    translate([x_c, y0, z0 + h / 2])
        rotate([-90, 0, 0])
            hull()
                for (a = [-(w/2 - r), w/2 - r], b = [-(h/2 - r), h/2 - r])
                    translate([a, b, 0]) cylinder(r=r, h=t, $fn=24);
}

module edge_cutouts() {
    // Openings start just under the PCB rather than at the back plate -- the
    // shell is now deep enough that cutting the full height would leave a
    // slot most of the way down the side for no reason.
    z0 = PCB_Z - 6;             // was -3; the plug body wanted more room under
                                // the connector than that left it
    depth = SHELL_D + 2;

    // The board data's own x = BOARD_W edge. Mirrored, it lands on this
    // model's LOW-x wall, which is why the ternary is here and not a typo.
    hi = !BOARD_DATA_IS_BACK_VIEW;
    for (c = ALL_PORTS ? right_cuts : usb_cuts)
        slot_through_x(hi ? BOARD_W + FIT_GAP - 1 : -FIT_GAP - WALL - 2,
                       c[0], z0, c[1], depth, WALL + 3, 2);

    // The board data's x = 0 edge, on the opposite wall.
    for (c = ALL_PORTS ? left_cuts : [])
        slot_through_x(hi ? -FIT_GAP - WALL - 2 : BOARD_W + FIT_GAP - 1,
                       c[0], z0, c[1], depth, WALL + 3, 2);

    // Top and bottom walls keep their edge; only x is mirrored.
    for (c = ALL_PORTS ? top_cuts : [])
        slot_through_y(mx(c[0]), BOARD_H + FIT_GAP - 1, z0,
                       c[1], depth, WALL + 3, 2);
    for (c = ALL_PORTS ? bottom_cuts : [])
        slot_through_y(mx(c[0]), -FIT_GAP - WALL - 2, z0,
                       c[1], depth, WALL + 3, 2);
}

// Slots through the back plate for BOOT, RESET and the power switch.
module back_access_slots() {
    for (a = back_access) {
        w = a[2]; h = a[3]; r = 3;
        translate([mx(a[0]), a[1], -1])
            hull()
                for (dx = [-(w/2 - r), w/2 - r], dy = [-(h/2 - r), h/2 - r])
                    translate([dx, dy, 0]) cylinder(r=r, h=WALL + 2, $fn=32);
    }
}

// A rib fence rather than a closed box: it locates the part, uses almost no
// plastic, and leaves the wire somewhere to go.
module fence(x, y, w, h, tall, gap_at_x) {
    t = 2.4;
    difference() {
        translate([x - t, y - t, WALL])
            cube([w + 2*t, h + 2*t, tall]);
        translate([x, y, WALL - 1])
            cube([w, h, tall + 2]);
        translate([gap_at_x, y - t - 1, WALL + tall - 6])
            cube([14, t + 2, 7]);          // lead-out notch
    }
}

module battery_bay()  { fence(BAT_X, BAT_Y, BAT_W, BAT_H, BAT_FENCE, BAT_X + 8); }

// Mount for the AHT20/DHT20 module on the back of the case, with an opening
// the sensor's grille pokes through so it reads room air rather than the
// warm air inside a sealed box next to the panel.
//
// The module is a narrow strip with the sensor at one end and a 4-pin header
// at the other. The fence locates the strip; the opening is at the sensor end.
module sensor_mount() {
    // A single post inside the case for one M3. No fence, nothing outside.
    translate([SENSOR_WIN_X + SENSOR_SCREW_DX, SENSOR_WIN_Y, WALL])
        cylinder(d=SENSOR_POST_D, h=SENSOR_POST_H, $fn=40);
}

module sensor_window() {
    // Window the grille sits in.
    translate([SENSOR_WIN_X, SENSOR_WIN_Y, -1])
        hull()
            for (dx = [-(SENSOR_WIN_L/2 - 2), SENSOR_WIN_L/2 - 2],
                 dy = [-(SENSOR_WIN_W/2 - 2), SENSOR_WIN_W/2 - 2])
                translate([dx, dy, 0])
                    cylinder(r=2, h=WALL + 2, $fn=24);

    // Blind pilot down the post, stopping short of the plate so the screw
    // cannot break through and show on the back.
    translate([SENSOR_WIN_X + SENSOR_SCREW_DX, SENSOR_WIN_Y,
               WALL + SENSOR_POST_H - SENSOR_PILOT_DP])
        cylinder(d=SENSOR_PILOT, h=SENSOR_PILOT_DP + 0.1, $fn=32);
}

/* No speaker fences: the speakers are on the PCB, not on the case. */

// Slots in the back plate over each PCB-mounted speaker, so it is not firing
// into a sealed box. Mirrored like every other board-derived position.
function on_foot_pad(x, y) =
    min([for (fx = FOOT_X) abs(x - fx)]) < 21
    && y > FOOT_BOLT_U[0] - 12 && y < FOOT_BOLT_U[1] + 12;

module speaker_grilles() {
    for (c = speakers)
        for (i = [-2 : 1 : 2])   // five a side; see the comment on speakers
            // Never perforate a foot pad: those four screws pull against it.
            // With the speaker positions still estimated this actually bites --
            // the right-hand grille lands squarely on the right pad.
            if (!on_foot_pad(mx(c[0]) + i * 6, c[1]))
            translate([mx(c[0]) + i * 6, c[1], -1])
                hull() {
                    translate([0, -SPK_H/2 + 6, 0]) cylinder(d=3.4, h=WALL+2, $fn=20);
                    translate([0,  SPK_H/2 - 6, 0]) cylinder(d=3.4, h=WALL+2, $fn=20);
                }
}

// microSD slot in the BOTTOM wall. The socket sits on the back of the PCB and
// the card goes in edge-on, so this cut sits just under the PCB plane rather
// than running the full height of the wall like the USB-C openings do.
module sd_slot() {
    slot_through_y(mx(SD_X), -FIT_GAP - WALL - 2, PCB_Z - SD_DROP,
                   SD_W, SD_DROP + 1, WALL + 3, 1.5);
}

// True where a bay sits, so the vent grid can stay out of the way.
function in_bay(x, y) =
    (BATTERY_BAY &&
     x > BAT_X - 6 && x < BAT_X + BAT_W + 6 &&
     y > BAT_Y - 6 && y < BAT_Y + BAT_H + 6)
    || max([for (c = speakers)
            (abs(x - mx(c[0])) < SPK_W/2 + 8
             && abs(y - c[1]) < SPK_H/2 + 8) ? 1 : 0]) > 0;

module shell() {
    difference() {
        union() {
            // Tray first, hollowed on its own. The cavity has to be subtracted
            // BEFORE the bosses go in -- the bosses stand inside the board
            // footprint, so a cavity cut afterwards would erase them.
            difference() {
                translate([-FIT_GAP - WALL, -FIT_GAP - WALL, 0])
                    rounded_box(SHELL_W, SHELL_H, SHELL_D, CORNER_R);
                translate([-FIT_GAP, -FIT_GAP, WALL])
                    cube([BOARD_W + 2*FIT_GAP, BOARD_H + 2*FIT_GAP, SHELL_D]);
                edge_cutouts();
                vent_grid();
            }

            // Bosses rise off the back plate to meet the PCB. They set the
            // rear clearance and the screw passes straight through them.
            for (h = holes)
                translate([h[0], h[1], WALL])
                    cylinder(d=BOSS_D, h=BAY_DEPTH + REAR_CLEARANCE, $fn=48);

            // Retention for the speakers, standing on the inside of the
            // back plate. The battery fence is off -- see BATTERY_BAY.
            if (BATTERY_BAY) battery_bay();
            sensor_mount();

            // Pads inside the back plate that the foot screws pull against.
            // Long enough to carry BOTH bolt rows, not just the lower one.
            for (fx = FOOT_X)
                translate([fx - 20, FOOT_BOLT_U[0] - 11, WALL])
                    cube([40, FOOT_BOLT_U[1] - FOOT_BOLT_U[0] + 22, 3]);
        }

        // Clearance right through: the screw enters here, at the back.
        for (h = holes)
            translate([h[0], h[1], -1])
                cylinder(d=SCREW_CLR, h=SHELL_D + 4, $fn=32);

        // Counterbore on the OUTSIDE for a BUTTON head (ISO 7380), which has a
        // domed top and a FLAT underside. A conical countersink would leave the
        // dome perched on the rim instead of seating, so this is a
        // flat-bottomed pocket, not a cone.
        //
        // Only WALL - SCREW_HEAD_SEAT of plate is left under the bore, but the
        // 10 mm boss sits directly above it, so the head bears on solid
        // material rather than on a thin membrane.
        for (h = holes)
            translate([h[0], h[1], -0.01])
                cylinder(d=SCREW_HEAD_D, h=SCREW_HEAD_SEAT, $fn=40);

        speaker_grilles();
        back_access_slots();
        sensor_window();
        sd_slot();

        // Foot screws: four per foot, matching foot()'s four holes. These are
        // CLEARANCE -- the screw drops in from inside the shell, through the
        // plate and pad, and taps into the foot. The pilot is in the foot.
        for (fx = FOOT_X)
            for (u = FOOT_BOLT_U)
                for (dz = FOOT_BOLT_DZ)
                    translate([fx + dz, u, -1])
                        cylinder(d=3.4, h=WALL + 6, $fn=24);
    }
}

// ---------------------------------------------------------------------------
// Front bezel
//
// Sandwiches the board against the shell. Four M3 x 6 x 4.2 brass heat-set
// inserts live in bosses on the BACK of this part; the screws come the other
// way, up through the shell and the board's own corner holes, into the brass.
// So: no extra fixings, no widening the shell past the print bed, no threads
// cut in printed plastic, and nothing visible from the front.
//
// The inner edge is chamfered away from the screen so the frame does not cast
// a shadow line across the picture at an angle.
// ---------------------------------------------------------------------------

BEZEL_T    = 4.5;       // face thickness in front of the glass. Deep enough
                        // that a 6 mm insert still leaves 2 mm of solid skin.
BEZEL_LIP  = 1.2;       // how far the frame overlaps onto the glass

module bezel() {
    difference() {
        union() {
            // Plate first, with the glass relief already taken out of it. The
            // relief has to be cut BEFORE the bosses are added -- the bosses
            // stand inside the relief, so cutting it afterwards erases them.
            difference() {
                translate([-FIT_GAP - WALL, -FIT_GAP - WALL, 0])
                    rounded_box(SHELL_W, SHELL_H, BEZEL_T + FRONT_GLASS, CORNER_R);
                translate([-FIT_GAP, -FIT_GAP, BEZEL_T])
                    cube([BOARD_W + 2*FIT_GAP, BOARD_H + 2*FIT_GAP,
                          FRONT_GLASS + 1]);
            }

            // Bosses carrying the inserts. They stand down through the glass
            // relief and land on the PCB at the corners, well clear of the
            // active area, so they also set how hard the glass is squeezed.
            for (h = holes)
                translate([h[0], h[1], BEZEL_T])
                    cylinder(d=INSERT_D + 3.2, h=FRONT_GLASS, $fn=48);
        }

        // The window, with a chamfer opening outward.
        translate([ACTIVE_X + BEZEL_LIP, ACTIVE_Y + BEZEL_LIP, -1])
            cube([ACTIVE_W - 2*BEZEL_LIP, ACTIVE_H - 2*BEZEL_LIP, BEZEL_T + 2]);
        translate([ACTIVE_X + BEZEL_LIP, ACTIVE_Y + BEZEL_LIP, -0.01])
            chamfer_frame(ACTIVE_W - 2*BEZEL_LIP, ACTIVE_H - 2*BEZEL_LIP, 1.6);

        // Blind pockets for the heat-set inserts, opened from the BACK. The
        // pocket stops BEZEL_T + FRONT_GLASS - INSERT_L above the front face,
        // so nothing breaks through and the front stays unmarked.
        for (h = holes)
            translate([h[0], h[1],
                       BEZEL_T + FRONT_GLASS - INSERT_L - INSERT_CLEAR])
                cylinder(d=INSERT_D + INSERT_FIT,
                         h=INSERT_L + INSERT_CLEAR + 0.2, $fn=32);
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

// Where the feet stand, and where their bolt holes are. The shell, the foot
// and the preview all read these, so the three cannot drift apart -- which is
// exactly what happened when the shell drilled at 0.25/0.75 and the assembly
// stood the feet at 0.28/0.72.
//
// A hole "u" up the foot's mounting face and "dz" across its width lands, in
// shell coordinates, at exactly (foot_x + dz, u). That falls out of the two
// rotations cancelling: the shell is bolted flat to the mount face, so
// distance up that face becomes distance up the shell.
FOOT_X       = [BOARD_W * 0.28, BOARD_W * 0.72];
FOOT_BOLT_U  = [22, 52];        // up the mounting face
FOOT_BOLT_DZ = [-13, 13];       // across the foot's width
FOOT_BOLT_DEPTH = 8;            // blind depth into the wedge

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
        for (u = FOOT_BOLT_U)                   // distance up the mount face
            for (dz = FOOT_BOLT_DZ)
                translate([u * sin(TILT), u * cos(TILT), FOOT_W/2 + dz])
                    rotate([0, 0, -TILT])
                        rotate([0, 90, 0])
                            translate([0, 0, -14])
                                cylinder(d=2.9, h=14 + FOOT_BOLT_DEPTH, $fn=32);

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
//   Set PART to "shell", "bezel", "foot", or "preview".
// ---------------------------------------------------------------------------

PART = "preview";

// Fastener report. These follow the constants above, so if you change a
// measurement the numbers change with it -- read them off the console rather
// than trusting the README.
PCB_ONLY   = BOARD_THICK - FRONT_GLASS;             // PCB without the glass
// Button-head length is measured UNDER the head, so the usable span starts at
// the bottom of the counterbore rather than at the face of the shell.
SCREW_MIN  = PCB_Z + PCB_ONLY - SCREW_HEAD_SEAT;    // just reaches the insert
SCREW_MAX  = SCREW_MIN + INSERT_L + INSERT_CLEAR + 0.2;   // bottoms out
echo(str("M3 BUTTON head (length under the head): at least ", SCREW_MIN,
         " mm to reach the insert, at most ", SCREW_MAX,
         " mm before it bottoms out."));
echo(str("Bezel front skin over each insert: ",
         BEZEL_T + FRONT_GLASS - INSERT_L - INSERT_CLEAR, " mm"));

if (PART == "shell") shell();
else if (PART == "foot") foot();
else if (PART == "bezel") bezel();
else if (PART == "none") ;   // render nothing; for scripts that include this file
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
        for (fx = FOOT_X)
            translate([fx - FOOT_W/2, 0, 0])
                rotate([0, 0, 90]) rotate([90, 0, 0]) foot();

    color("#3a4453") rotate([90 - TILT, 0, 0]) shell();

    // Bezel, flipped to face the viewer and set forward by the whole board
    // stack PLUS its own face thickness: after mirror([0,0,1]) a bezel point
    // at local z lands at (T - z), so T must be the height of the bezel's
    // OUTER face, not of the glass it rests on.
    color("#59636f")
        rotate([90 - TILT, 0, 0])
            translate([0, 0, WALL + REAR_CLEARANCE + BOARD_THICK + BEZEL_T])
                mirror([0, 0, 1]) bezel();

    // Desk, for scale.
    color("#20262f") translate([-30, -30, -3]) cube([310, 160, 3]);
}
