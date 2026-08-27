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

WANT = ["WALL", "BAY_DEPTH", "REAR_CLEARANCE", "PCB_Z", "SHELL_D", "BEZEL_T",
        "FRONT_GLASS", "INSERT_L", "BOARD_W", "BOARD_H", "FIT_GAP",
        "BAT_X", "BAT_Y", "BAT_W", "BAT_H", "BOOT_SLOT_X", "SW_SLOT_X"]
NL = chr(10)
open('_chk.scad', 'w').write(
    'PART="none";' + NL + 'include <tempest_stand.scad>' + NL
    + "".join('echo("K %s=", %s);' % (k, k) + NL for k in WANT))
r = subprocess.run([OS, '-o', '_chk.stl', '--export-format', 'binstl',
                    '-D', 'PART="none"', '_chk.scad'], capture_output=True, text=True)
K = {m.group(1): float(m.group(2))
     for m in re.finditer(r'ECHO: "K (\w+)=", ([-\d.e+]+)', r.stderr)}
missing = [k for k in WANT if k not in K]
if missing:
    sys.exit("could not read %s from the model: %s" % (missing, r.stderr[-800:]))

def probe(label, body, expect):
    open('_chk.scad','w').write('PART="none";\ninclude <tempest_stand.scad>\n' + body)
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
    print("%s %-52s %s%s" % (ok, label, got, bbox))
    if os.path.exists('_chk.stl'): os.remove('_chk.stl')
    return got == expect

ok = True
# The bezel's insert boss, and the bore up its middle.
BOSS_MID  = K["BEZEL_T"] + K["FRONT_GLASS"] / 2          # inside the boss
SKIN      = K["BEZEL_T"] + K["FRONT_GLASS"] - K["INSERT_L"]   # solid front skin
def slab(part, x, y, w, z):
    return "intersection(){%s();translate([%f,%f,%f])cube([%f,%f,0.2]);}" % (
        part, x, y, z - 0.1, w, w)

ok &= probe("bezel boss present mid-height (z=%.1f)" % BOSS_MID,
    slab("bezel", 0.5, 0.5, 9, BOSS_MID), "SOLID")
ok &= probe("bezel insert bore is open (z=%.1f)" % BOSS_MID,
    slab("bezel", 2.1, 2.0, 2, BOSS_MID), "EMPTY")
ok &= probe("bezel front skin unbroken (z=%.1f)" % (SKIN / 2),
    slab("bezel", 2.1, 2.0, 2, SKIN / 2), "SOLID")
ok &= probe("bezel pocket has opened (z=%.1f)" % (SKIN + 0.5),
    slab("bezel", 2.1, 2.0, 2, SKIN + 0.5), "EMPTY")
# The shell's spacer boss, and the screw clearance through it.
MID = K["PCB_Z"] / 2
ok &= probe("shell boss present mid-height (z=%.1f)" % MID,
    slab("shell", 0.5, 0.5, 9, MID), "SOLID")
ok &= probe("shell screw clearance is open (z=%.1f)" % MID,
    slab("shell", 2.0, 1.9, 2.2, MID), "EMPTY")
ok &= probe("shell screw hole breaks out of the back (z=0.1)",
    "intersection(){shell();translate([2.0,1.9,0.05])cube([2.2,2.2,0.1]);}", "EMPTY")
ok &= probe("shell boss stops at the PCB plane (z=%.1f)" % (K["PCB_Z"] + 0.2),
    slab("shell", 0.5, 0.5, 9, K["PCB_Z"] + 0.2), "EMPTY")
ok &= probe("battery fence stands on the plate (z=%.1f)" % (K["WALL"] + 4),
    slab("shell", K["BAT_X"] - 2.0, K["BAT_Y"] + 20, 1.5, K["WALL"] + 4), "SOLID")
ok &= probe("battery bay floor is clear (z=%.1f)" % (K["WALL"] + 4),
    slab("shell", K["BAT_X"] + 20, K["BAT_Y"] + 20, 4, K["WALL"] + 4), "EMPTY")
ok &= probe("BOOT/RESET slot is open through the back plate",
    slab("shell", K["BOOT_SLOT_X"] - 2, 23, 4, K["WALL"] / 2), "EMPTY")
ok &= probe("power switch slot is open through the back plate",
    slab("shell", K["SW_SLOT_X"] - 2, 98.9, 4, K["WALL"] / 2), "EMPTY")
ok &= probe("back plate is solid between the two slots",
    slab("shell", K["BOOT_SLOT_X"] - 2, 60, 4, K["WALL"] / 2), "SOLID")
ok &= probe("speaker grille is open through the plate (z=%.1f)" % (K["WALL"] / 2),
    slab("shell", 33.2, 121.2, 1.6, K["WALL"] / 2), "EMPTY")

# All four foot bolts per foot must be drilled, and drilled where the foot
# actually presents its holes. FOOT_X is 0.28/0.72 of the board width; the
# shell used to drill two holes at 0.25/0.75, so nothing lined up.
FOOT_X = [K["BOARD_W"] * 0.28, K["BOARD_W"] * 0.72]
for fx in FOOT_X:
    for u in (22, 52):
        for dz in (-13, 13):
            ok &= probe("shell foot hole at (%.0f, %d)" % (fx + dz, u),
                "intersection(){shell();translate([%f,%f,-0.5])cube([2,2,1]);}"
                % (fx + dz - 1, u - 1), "EMPTY")
# ...and the plate between a pair must still be there.
ok &= probe("shell is solid between the foot holes",
    "intersection(){shell();translate([%f,21,-0.5])cube([2,2,1]);}"
    % (FOOT_X[0] - 1), "SOLID")


# ---------------------------------------------------------------------------
# Which walls are actually open. Reads the exported shell directly rather than
# round-tripping through OpenSCAD, so it costs one export instead of hundreds.
# ---------------------------------------------------------------------------
W, H, FIT, WALL = K["BOARD_W"], K["BOARD_H"], K["FIT_GAP"], K["WALL"]

subprocess.run([OS, '-o', '_chk.stl', '--export-format', 'binstl',
                '-D', 'PART="shell"', 'tempest_stand.scad'],
               capture_output=True, text=True)
d = open('_chk.stl', 'rb').read()
tris = [[struct.unpack('<3f', d[84+i*50+12+j*12 : 84+i*50+24+j*12]) for j in range(3)]
        for i in range(struct.unpack('<I', d[80:84])[0])]
os.remove('_chk.stl')

ZLO, ZHI = K["PCB_Z"] - 2.0, K["PCB_Z"] - 0.5
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
        ("right",  0,  W + FIT + WALL,  1, 6.0, 141.0),
        ("left",   0, -FIT - WALL,      1, 6.0, 141.0),
        ("top",    1,  H + FIT + WALL,  0, 6.0, 241.0),
        ("bottom", 1, -FIT - WALL,      0, 6.0, 241.0)]:
    g = openings(ax, val, al, lo, hi)
    print("%-7s wall openings: %s" % (name,
          ", ".join("%.1f-%.1f" % (a, b) for a, b in g) or "none"))

print("\n" + ("all probes passed" if ok else "SOME PROBES FAILED"))
sys.exit(0 if ok else 1)
