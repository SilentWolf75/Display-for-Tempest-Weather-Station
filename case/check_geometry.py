"""Geometry self-test for tempest_stand.scad.

Slabs each part at a known height and asserts the result is solid or empty.
It exists because of a bug class this design keeps inviting: a pocket cut in
the wrong CSG order silently erases a boss, and the part still renders and
still exports -- just without the feature that locates the board.

Run it after changing any constant:  python check_geometry.py
"""
import struct, subprocess, sys, os, shutil

# Scratch files and the SCAD's own include both resolve relative to the
# working directory, so pin it here rather than trusting where we were run.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

OS = shutil.which("openscad") or r"C:\Program Files\OpenSCAD\openscad.exe"
if not os.path.exists(OS):
    sys.exit("openscad not found at %s -- set OS in this script" % OS)

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
ok &= probe("bezel boss present at z=6",
    "intersection(){bezel();translate([0.5,0.5,5.9])cube([9,9,0.2]);}", "SOLID")
ok &= probe("bezel insert bore is open at z=6",
    "intersection(){bezel();translate([2.1,2.0,5.9])cube([2,2,0.2]);}", "EMPTY")
ok &= probe("bezel front skin is unbroken at z=1.5",
    "intersection(){bezel();translate([2.1,2.0,1.4])cube([2,2,0.2]);}", "SOLID")
ok &= probe("bezel pocket has opened by z=2.5",
    "intersection(){bezel();translate([2.1,2.0,2.4])cube([2,2,0.2]);}", "EMPTY")
# The shell's spacer boss, and the screw clearance through it.
ok &= probe("shell boss present at z=8",
    "intersection(){shell();translate([0.5,0.5,7.9])cube([9,9,0.2]);}", "SOLID")
ok &= probe("shell screw clearance is open at z=8",
    "intersection(){shell();translate([2.0,1.9,7.9])cube([2.2,2.2,0.2]);}", "EMPTY")
ok &= probe("shell screw hole breaks out of the back at z=0.1",
    "intersection(){shell();translate([2.0,1.9,0.05])cube([2.2,2.2,0.1]);}", "EMPTY")
# The boss tops must meet the PCB, i.e. reach WALL+REAR_CLEARANCE = 13.4.
ok &= probe("shell boss stops at the PCB plane (nothing above 13.4)",
    "intersection(){shell();translate([0.5,0.5,13.5])cube([9,9,0.2]);}", "EMPTY")

# ---------------------------------------------------------------------------
# Which walls are actually open. Reads the exported shell directly rather than
# round-tripping through OpenSCAD, so it costs one export instead of hundreds.
# ---------------------------------------------------------------------------
W, H, FIT, WALL = 247.04, 147.01, 0.6, 2.4

subprocess.run([OS, '-o', '_chk.stl', '--export-format', 'binstl',
                '-D', 'PART="shell"', 'tempest_stand.scad'],
               capture_output=True, text=True)
d = open('_chk.stl', 'rb').read()
tris = [[struct.unpack('<3f', d[84+i*50+12+j*12 : 84+i*50+24+j*12]) for j in range(3)]
        for i in range(struct.unpack('<I', d[80:84])[0])]
os.remove('_chk.stl')

def openings(plane_axis, plane_val, along, lo, hi, zlo=3.0, zhi=13.0):
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
