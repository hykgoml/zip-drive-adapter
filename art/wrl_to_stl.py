#!/usr/bin/env python3
"""Convert a KiCad VRML (.wrl) 3D model to a binary STL for PCBPA's MODEL3D.

KiCad's .wrl files use 0.1-inch units (1 unit = 2.54 mm); the STL is written in mm.
Each facet's 2-byte attribute carries the shape's colour in the VisCAM/SolidView
convention (bit 15 clear = colour valid, 5 bits each of R, G, B), for viewers
that read it.

Usage: python3 wrl_to_stl.py in.wrl out.stl

Then, in PCBPA (with the STL in models/ next to the .pcbpa):
    MODEL3D J1 'models/<file>.stl' UNITS mm ROT 0 0 0 OFFSET 0 0 0
Always give ROT 0 0 0 explicitly. Without it PCBPA guesses the up axis from the
model's proportions, and a model deeper than it is tall (like the right-angle
DB25) gets tipped 90° and cut off at the board edge. Check placement with
export_3d_model rather than by eye: the 3D renders are perspective views.
PCBPA saves the model's path as an absolute one; rewrite it to models/... in
the copy that goes in the repo.
"""
import re
import struct
import sys

SCALE = 2.54  # VRML unit -> mm


def parse(text):
    materials = {}
    for m in re.finditer(r"DEF\s+(\S+)\s+Material\s*\{[^}]*?diffuseColor\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", text):
        materials[m.group(1)] = tuple(float(v) for v in m.group(2, 3, 4))
    tris = []
    for shape in re.split(r"(?m)^Shape\s*\{", text)[1:]:
        if "IndexedFaceSet" not in shape:
            continue
        mat = re.search(r"USE\s+(\S+)", shape) or re.search(r"DEF\s+(\S+)\s+Material", shape)
        rgb = materials.get(mat.group(1), (0.7, 0.7, 0.7)) if mat else (0.7, 0.7, 0.7)
        idx = [int(v) for v in re.search(r"coordIndex\s*\[([^\]]*)\]", shape).group(1).replace(",", " ").split()]
        pts = [float(v) * SCALE for v in re.search(r"point\s*\[([^\]]*)\]", shape).group(1).replace(",", " ").split()]
        pts = [tuple(pts[i:i + 3]) for i in range(0, len(pts), 3)]
        face = []
        for i in idx:
            if i < 0:
                for k in range(1, len(face) - 1):  # fan-triangulate
                    tris.append((pts[face[0]], pts[face[k]], pts[face[k + 1]], rgb))
                face = []
            else:
                face.append(i)
    return tris


def normal(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = sum(x * x for x in n) ** 0.5 or 1.0
    return tuple(x / length for x in n)


def write_stl(tris, path):
    with open(path, "wb") as f:
        f.write(b"KiCad wrl converted to STL (mm)".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c, rgb in tris:
            r, g, bl = (min(31, int(x * 31 + 0.5)) for x in rgb)
            attr = (r << 10) | (g << 5) | bl  # bit 15 clear: colour valid
            f.write(struct.pack("<12fH", *normal(a, b, c), *a, *b, *c, attr))


if __name__ == "__main__":
    tris = parse(open(sys.argv[1]).read())
    write_stl(tris, sys.argv[2])
    xs = [p[0] for t in tris for p in t[:3]]
    ys = [p[1] for t in tris for p in t[:3]]
    zs = [p[2] for t in tris for p in t[:3]]
    print(f"{len(tris)} triangles; x {min(xs):.2f}..{max(xs):.2f}, y {min(ys):.2f}..{max(ys):.2f}, z {min(zs):.2f}..{max(zs):.2f} mm")
