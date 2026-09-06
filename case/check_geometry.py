"""Geometry self-test for tempest_stand.scad.

Slabs each part at a known height and asserts the result is solid or empty.
It exists because of a bug class this design keeps inviting: a pocket cut in
the wrong CSG order silently erases a boss, and the part still renders and
still exports -- just without the feature that locates the board.

Heights are read back OUT of the model rather than written down here, so
changing a constant re-aims the probes instead of breaking them.

Run it after changing any constant:  python check_geometry.py
"""
import struct, subprocess, sys, os, re, shutil

os.chdir(os.path.dirname(os.path.abspath(__file__)))
OS = shutil.which("openscad") or r"C:\Program Files\OpenSCAD\openscad.exe"
if not os.path.exists(OS):
    sys.exit("openscad not found at %s -- set OS in this script" % OS)

WANT = [
    "WALL", "FRAME", "BAY_DEPTH", "REAR_CLEARANCE", "PCB_Z", "FACE_T", "HOUSING_D",
    "INSERT_H", "INSERT_D", "INSERT_BOSS_D", "BOARD_W", "BOARD_H", "FIT_GAP",
    "SEAT_Z", "COVER_RECESS", "RECESS_LIP", "COVER_IN",
    "BAT_X", "BAT_Y", "BAT_W", "BAT_H", "BOOT_SLOT_X", "SW_SLOT_X",
    "SENSOR_WIN_X", "SENSOR_WIN_Y", "SENSOR_SCREW_DX", "SENSOR_WIN_L",
    "SENSOR_WIN_W", "SENSOR_FACE", "SENSOR_WALL_H", "SENSOR_WALL_T",
    "SENSOR_SEAT_W", "SENSOR_SEAT_PAD", "SENSOR_SEAT_X1", "SENSOR_POST_H",
    "LIP_H", "LIP_CLEAR", "SPK0_X", "CASE_SCREW_XL", "CASE_SCREW_XR",
    "CASE_SCREW_Y0", "CASE_SCREW_Y1", "CASE_SCREW_X1", "CASE_SCREW_YT",
    "ACTIVE_X", "ACTIVE_Y", "ACTIVE_W",
    "ACTIVE_H", "CHAMFER", "LIP_FLAT", "COVER_PAD_H", "PAD_D",
    "PAD0_X", "PAD0_Y", "PAD2_X", "PAD2_Y",
    "MODULE_X", "MODULE_Y", "MODULE_W", "MODULE_H", "BOARD_THICK",
]
NL = chr(10)
open('_chk.scad', 'w').write(
    'include <tempest_stand.scad>' + NL
    + "".join('echo("K %s=", %s);' % (k, k) + NL for k in WANT))
r = subprocess.run([OS, '-o', '_chk.stl', '--export-format', 'binstl',
                    '-D', 'PART="none"', '_chk.scad'], capture_output=True, text=True)
K = {m.group(1): float(m.group(2))
     for m in re.finditer(r'ECHO: "K (\w+)=", ([-\d.e+]+)', r.stderr)}
missing = [k for k in WANT if k not in K]
if missing:
    sys.exit("could not read %s from the model: %s" % (missing, r.stderr[-800:]))
print("constants loaded (%d)" % len(K), flush=True)

def probe(label, body, expect):
    open('_chk.scad','w').write('include <tempest_stand.scad>\n' + body)
    r = subprocess.run([OS,'-o','_chk.stl','--export-format','binstl',
                        '-D','PART="none"','_chk.scad'],
                       capture_output=True, text=True)
    if 'Ignoring unknown module' in r.stderr:
        sys.exit("tempest_stand.scad did not load: " + r.stderr)
    empty = 'is empty' in r.stderr or not os.path.exists('_chk.stl')
    got = 'EMPTY' if empty else 'SOLID'
    bbox = ''
    if not empty:
        d=open('_chk.stl','rb').read(); n=struct.unpack('<I',d[80:84])[0]
        v=[struct.unpack('<3f',d[84+i*50+12+j*12:84+i*50+24+j*12])
           for i in range(n) for j in range(3)]
        bbox = "  z %.2f..%.2f" % (min(p[2] for p in v), max(p[2] for p in v))
    ok = 'ok  ' if got == expect else 'FAIL'
    print("%s %-58s %s%s" % (ok, label, got, bbox), flush=True)
    if os.path.exists('_chk.stl'): os.remove('_chk.stl')
    return got == expect

def slab(part, x, y, w, z):
    return "intersection(){%s();translate([%f,%f,%f])cube([%f,%f,0.2]);}" % (
        part, x, y, z - 0.1, w, w)

ok = True

# ---------------------------------------------------------------------------
# Housing: window, touch ramp, insert pockets, board cavity
# ---------------------------------------------------------------------------
AX, AY, AW, AH = K["ACTIVE_X"], K["ACTIVE_Y"], K["ACTIVE_W"], K["ACTIVE_H"]
CH, FT, LF = K["CHAMFER"], K["FACE_T"], K["LIP_FLAT"]
HD, IH, SZ = K["HOUSING_D"], K["INSERT_H"], K["SEAT_Z"]
SX, SY = K["CASE_SCREW_XL"], K["CASE_SCREW_Y0"]

ok &= probe("housing window is open at the glass (z=%.1f)" % (FT - LF / 2),
    slab("housing", AX + AW / 2 - 0.8, AY + AH / 2 - 0.8, 1.6, FT - LF / 2),
    "EMPTY")
ok &= probe("touch ramp is open at the front, outside the glass opening",
    slab("housing", AX - CH * 0.45, AY + AH / 2 - 0.8, 1.6, 0.4),
    "EMPTY")
ok &= probe("ramp does not eat the outer frame",
    slab("housing", AX - CH - 3.0, AY + AH / 2 - 0.8, 1.6, 0.4),
    "SOLID")
ok &= probe("front face is solid beside the window (z=%.1f)" % (FT / 2),
    slab("housing", AX - 4.0, AY + AH / 2 - 0.8, 1.6, FT / 2),
    "SOLID")

ok &= probe("board cavity is open behind the face (z=%.1f)" % (FT + 2),
    slab("housing", K["BOARD_W"] / 2 - 1, K["BOARD_H"] / 2 - 1, 2, FT + 2),
    "EMPTY")
ok &= probe("posts do not block the board drop-in at y=%.0f" % SY,
    slab("housing", 1.0, SY - 0.8, 1.6, FT + 2),
    "EMPTY")

INSERT_MID = SZ - IH / 2
INSERT_FRONT = (SZ - IH) / 2
ok &= probe("insert pocket is open at the back (z=%.1f)" % INSERT_MID,
    slab("housing", SX - 0.7, SY - 0.7, 1.4, INSERT_MID),
    "EMPTY")
ok &= probe("insert is BLIND -- nub in front of it is solid (z=%.1f)" % INSERT_FRONT,
    slab("housing", SX - 0.7, SY - 0.7, 1.4, INSERT_FRONT),
    "SOLID")
ok &= probe("frame is solid at the screw below the insert (z=%.1f)" % (SZ * 0.4),
    slab("housing", SX - 0.7, SY - 0.7, 1.4, SZ * 0.4),
    "SOLID")
ok &= probe("cover well is open at the back (z=%.1f)" % (HD - 0.4),
    slab("housing", SX - 0.7, SY - 0.7, 1.4, HD - 0.4),
    "EMPTY")
RIM_X = -K["FIT_GAP"] - K["FRAME"] + 0.4
ok &= probe("housing rim wraps the cover at the back (z=%.1f)" % (HD - 0.4),
    slab("housing", RIM_X, 40, 0.8, HD - 0.4),
    "SOLID")
ok &= probe("cover is inset -- empty where the housing rim is",
    slab("cover", RIM_X, 40, 0.8, K["WALL"] / 2),
    "EMPTY")

# Window stays inside the LCD module so no PCB shows.
if AX <= K["MODULE_X"] + 0.2 or AY <= K["MODULE_Y"] + 0.2 \
        or AX + AW >= K["MODULE_X"] + K["MODULE_W"] - 0.2 \
        or AY + AH >= K["MODULE_Y"] + K["MODULE_H"] - 0.2:
    print("FAIL window %s,%s %sx%s is not inside module %s,%s %sx%s" % (
        AX, AY, AW, AH, K["MODULE_X"], K["MODULE_Y"], K["MODULE_W"], K["MODULE_H"]))
    ok = False
else:
    print("ok   window sits inside the LCD module                          SOLID")

# ---------------------------------------------------------------------------
# Cover: spacer pads, case-screw path, sensor, slots, feet
# ---------------------------------------------------------------------------
PX, PY = K["PAD0_X"], K["PAD0_Y"]
ok &= probe("cover spacer pad present mid-height (z=%.1f)" % (K["PCB_Z"] / 2),
    slab("cover", PX - 1.5, PY - 1.5, 3, K["PCB_Z"] / 2), "SOLID")
ok &= probe("sensor-well spacer pad is planted on the 2.4 mm floor",
    slab("cover", K["PAD2_X"] - 1.1, K["PAD2_Y"] - 1.1, 2.2,
         K["SENSOR_FACE"] + 0.3), "SOLID")
ok &= probe("spacer pad has NO screw hole (plate solid at z=%.1f)" % (K["WALL"] / 2),
    slab("cover", PX - 1.1, PY - 1.1, 2.2, K["WALL"] / 2), "SOLID")
ok &= probe("spacer pad stops at the PCB plane (z=%.1f)" % (K["PCB_Z"] + 0.2),
    slab("cover", PX - 1.5, PY - 1.5, 3, K["PCB_Z"] + 0.2), "EMPTY")
ok &= probe("old corner has no pad (alignment lip must stay clear)",
    slab("cover", 0.5, 0.5, 3, K["PCB_Z"] / 2), "EMPTY")

ok &= probe("cover case-screw clearance is open (z=%.1f)" % (K["WALL"] / 2),
    slab("cover", SX - 0.8, SY - 0.8, 1.6, K["WALL"] / 2), "EMPTY")
ok &= probe("no inner boss around the case screw (cover can seat)",
    slab("cover", SX - 0.8, SY - 0.8, 1.6, K["WALL"] + 1.0), "EMPTY")
ok &= probe("cover screw counterbore is open at the back (z=0.1)",
    "intersection(){cover();translate([%f,%f,0.05])cube([2.2,2.2,0.1]);}"
    % (SX - 1.1, SY - 1.1), "EMPTY")
TX, TY = K["CASE_SCREW_X1"], K["CASE_SCREW_YT"]
ok &= probe("cover top-edge screw is open (z=%.1f)" % (K["WALL"] / 2),
    slab("cover", TX - 0.8, TY - 0.8, 1.6, K["WALL"] / 2), "EMPTY")
ok &= probe("housing top-edge insert pocket is open at the back",
    slab("housing", TX - 0.7, TY - 0.7, 1.4, INSERT_MID), "EMPTY")

ok &= probe("sensor window is open through the plate",
    slab("cover", K["SENSOR_WIN_X"] - 1, K["SENSOR_WIN_Y"] - 1, 2,
         K["SENSOR_FACE"] / 2), "EMPTY")
FACE, PH = K["SENSOR_FACE"], K["SENSOR_POST_H"]
POST_MID = FACE + PH / 2
ok &= probe("sensor post stands INSIDE at z=%.1f" % POST_MID,
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"] + 2.2,
         K["SENSOR_WIN_Y"] - 0.6, 1.2, POST_MID), "SOLID")
ok &= probe("post is drilled, not solid (pilot open at z=%.1f)" % POST_MID,
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"] - 0.8,
         K["SENSOR_WIN_Y"] - 0.8, 1.6, POST_MID), "EMPTY")
ok &= probe("pilot is BLIND -- plate under it is unbroken",
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"] - 0.8,
         K["SENSOR_WIN_Y"] - 0.8, 1.6, FACE / 2), "SOLID")
ok &= probe("sensor seat is one flat 2.4 mm bed (header end)",
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"] + 5.5,
         K["SENSOR_WIN_Y"] - 0.4, 0.8, K["WALL"] - 0.2), "EMPTY")
ok &= probe("sensor seat reaches the DuPont / wire landing",
    slab("cover", K["SENSOR_SEAT_X1"] - 2.0,
         K["SENSOR_WIN_Y"] - 0.4, 0.8, K["WALL"] - 0.2), "EMPTY")
ok &= probe("sensor seat is the same height beside the hole",
    slab("cover", K["SENSOR_WIN_X"] - 0.4,
         K["SENSOR_WIN_Y"] + K["SENSOR_SEAT_W"] / 2 - 1.2,
         0.8, K["WALL"] - 0.2), "EMPTY")
ok &= probe("nothing projects outside the back plate",
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"],
         K["SENSOR_WIN_Y"], 3, -1.0), "EMPTY")
ok &= probe("post stops at 5 mm (nothing above z=%.1f)"
            % (FACE + PH + 0.3),
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_SCREW_DX"] - 3,
         K["SENSOR_WIN_Y"] - 3, 6, FACE + PH + 0.3),
    "EMPTY")
HOOD_Z = FACE + K["SENSOR_WALL_H"] / 2
ok &= probe("heat wall on the sensing end of the hole",
    slab("cover", K["SENSOR_WIN_X"] - K["SENSOR_WIN_L"] / 2 - K["SENSOR_WALL_T"] / 2 - 0.3,
         K["SENSOR_WIN_Y"] - 0.4, 0.8, HOOD_Z), "SOLID")
ok &= probe("heat wall is open toward the screw / DuPonts",
    slab("cover", K["SENSOR_WIN_X"] + K["SENSOR_WIN_L"] / 2 + 0.4,
         K["SENSOR_WIN_Y"] - K["SENSOR_WIN_W"] / 2 + 1.0,
         0.8, HOOD_Z), "EMPTY")
ok &= probe("BOOT/RESET slot is open through the back plate",
    slab("cover", K["BOOT_SLOT_X"] - 2, 23, 4, K["WALL"] / 2), "EMPTY")
ok &= probe("power switch slot is open through the back plate",
    slab("cover", K["SW_SLOT_X"] - 2, 98.9, 4, K["WALL"] / 2), "EMPTY")
ok &= probe("alignment lip is intact at the power switch",
    slab("cover", -K["FIT_GAP"] + K["LIP_CLEAR"],
         100.9 - 0.4, 0.3, K["WALL"] + K["LIP_H"] / 2), "SOLID")
ok &= probe("back plate is solid between the two slots",
    slab("cover", K["BOOT_SLOT_X"] - 2, 60, 4, K["WALL"] / 2), "SOLID")
ok &= probe("speaker grille is open through the plate (z=%.1f)" % (K["WALL"] / 2),
    slab("cover", K["SPK0_X"] - 0.8, 57.2, 1.6, K["WALL"] / 2), "EMPTY")

FOOT_X = [K["BOARD_W"] * 0.28, K["BOARD_W"] * 0.72]
for fx in FOOT_X:
    for u in (22, 52):
        for dz in (-13, 13):
            ok &= probe("cover foot hole at (%.0f, %d)" % (fx + dz, u),
                "intersection(){cover();translate([%f,%f,-0.5])cube([2,2,1]);}"
                % (fx + dz - 1, u - 1), "EMPTY")
ok &= probe("cover is solid between the foot holes",
    "intersection(){cover();translate([%f,21,-0.5])cube([2,2,1]);}"
    % (FOOT_X[0] - 1), "SOLID")

# ---------------------------------------------------------------------------
# Which housing walls are actually open. USB-C lives on the housing now.
# ---------------------------------------------------------------------------
W, H, FIT, FRAME = K["BOARD_W"], K["BOARD_H"], K["FIT_GAP"], K["FRAME"]

subprocess.run([OS, '-o', '_chk.stl', '--export-format', 'binstl',
                '-D', 'PART="housing"', 'tempest_stand.scad'],
               capture_output=True, text=True)
d = open('_chk.stl', 'rb').read()
tris = [[struct.unpack('<3f', d[84+i*50+12+j*12 : 84+i*50+24+j*12]) for j in range(3)]
        for i in range(struct.unpack('<I', d[80:84])[0])]
os.remove('_chk.stl')

z_pcb = K["FACE_T"] + K["BOARD_THICK"]
ZLO, ZHI = z_pcb - 2.0, z_pcb + 8.0

def openings(plane_axis, plane_val, along, lo, hi, zlo=ZLO, zhi=ZHI):
    iv = sorted((min(p[along] for p in v), max(p[along] for p in v))
                for v in tris
                if all(abs(p[plane_axis] - plane_val) < 0.05 for p in v)
                and not (max(p[2] for p in v) < zlo or min(p[2] for p in v) > zhi))
    merged = []
    for a, b in iv:
        if merged and a <= merged[-1][1] + 0.01:
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    gaps, cur = [], lo
    for a, b in merged:
        if a > cur + 0.05:
            gaps.append((cur, a))
        cur = max(cur, b)
    if cur < hi - 0.05:
        gaps.append((cur, hi))
    return [g for g in gaps if g[1] - g[0] > 1.0]

print()
for name, ax, val, al, lo, hi in [
        ("right",  0,  W + FIT + FRAME,  1, 6.0, 141.0),
        ("left",   0, -FIT - FRAME,      1, 6.0, 141.0),
        ("top",    1,  H + FIT + FRAME,  0, 6.0, 241.0),
        ("bottom", 1, -FIT - FRAME,      0, 6.0, 241.0)]:
    g = openings(ax, val, al, lo, hi)
    print("%-7s wall openings: %s" % (name,
          ", ".join("%.1f-%.1f" % (a, b) for a, b in g) or "none"))

print("\n" + ("all probes passed" if ok else "SOME PROBES FAILED"))
sys.exit(0 if ok else 1)
