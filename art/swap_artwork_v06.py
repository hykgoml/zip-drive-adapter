# v0.6 from v0.5: black solder mask + light (green/white) silkscreen.
# Replaces the bottom-silk logo/QR polygons with light-ink versions and bumps the version text.
#   Logo: 14mm at (44.2, 13), ink = sticker's green parts.
#   QR:   33 cells x 0.25mm (29 modules + 2-cell quiet zone) at (57.0, 39.0), ink = light cells.
# Both mirrored in X so they read correctly from below.
# Usage: $KPY art/swap_artwork_v06.py art zipbridge_v0.5.kicad_pcb zipbridge_v0.6.kicad_pcb
import sys, json, pcbnew as p
A, SRC, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
MM = p.FromMM
b = p.LoadBoard(SRC)
old = [d for d in b.GetDrawings() if isinstance(d, p.PCB_SHAPE) and d.GetLayer() == p.B_SilkS and d.GetShape() == p.SHAPE_T_POLY]
for d in old: b.Remove(d)
print("removed old artwork polygons:", len(old))
for d in b.GetDrawings():
    if isinstance(d, p.PCB_TEXT) and "v0.5" in d.GetText(): d.SetText(d.GetText().replace("v0.5", "v0.6"))
def add_cells(cells, px, x_left, y_top, width):
    ps = p.SHAPE_POLY_SET()
    for r, c0, c1 in cells:
        X0 = x_left + width - c1 * px; X1 = x_left + width - c0 * px
        Y0 = y_top + r * px; Y1 = Y0 + px
        ps.NewOutline()
        for x, y in ((X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)): ps.Append(MM(x), MM(y))
    ps.Simplify(); ps.Fracture()          # simple hole-free polygons (PCBPA import needs this)
    for i in range(ps.OutlineCount()):
        one = p.SHAPE_POLY_SET(); one.AddOutline(ps.Outline(i))
        sh = p.PCB_SHAPE(b); sh.SetShape(p.SHAPE_T_POLY); sh.SetPolyShape(one)
        sh.SetFilled(True); sh.SetWidth(0); sh.SetLayer(p.B_SilkS); b.Add(sh)
    return ps.OutlineCount()
L = json.load(open(A + "/logo_bitmap_light.json")); D = 14.0
print("logo shapes", add_cells(L["runs"], D / L["N"], 44.2 - D / 2, 13.0 - D / 2, D))
M = json.load(open(A + "/qr_bitmap_light.json"))["M"]; n = len(M); mod = 0.25
runs = []
for r, row in enumerate(M):
    c = 0
    while c < n:
        if row[c]:
            s = c
            while c < n and row[c]: c += 1
            runs.append((r, s, c))
        else: c += 1
print("qr shapes", add_cells(runs, mod, 57.0, 39.0, n * mod))
b.Save(OUT)
