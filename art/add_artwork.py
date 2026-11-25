# Build board v0.3 from v0.2: shift bottom legend, bump version, add bottom-silk logo + QR.
# Usage: $KPY art/add_artwork.py art zipbridge_v0.2.kicad_pcb zipbridge_v0.3.kicad_pcb
# Bitmaps are for BLACK silkscreen on a light (lime) mask: 1 = ink. Logo is 14mm at (44.2,13); QR 29x0.25mm at (57.6,39.4). Both bottom side, mirrored.
import sys,json,pcbnew as p
S,SRC,OUT=sys.argv[1],sys.argv[2],sys.argv[3]
MM=p.FromMM
b=p.LoadBoard(SRC)
for d in b.GetDrawings():
    if isinstance(d,p.PCB_TEXT):
        if d.GetLayer()==p.B_SilkS: d.Move(p.VECTOR2I(MM(-6.5),0))
        if "v0.2" in d.GetText(): d.SetText(d.GetText().replace("v0.2","v0.3"))
def add_cells(cells, px, x_left, y_top, width):
    """cells: (row, col0, col1) runs; mirrored in X for bottom side."""
    ps=p.SHAPE_POLY_SET()
    for r,c0,c1 in cells:
        X0=x_left+width-c1*px; X1=x_left+width-c0*px
        Y0=y_top+r*px; Y1=Y0+px
        ps.NewOutline()
        for x,y in ((X0,Y0),(X1,Y0),(X1,Y1),(X0,Y1)): ps.Append(MM(x),MM(y))
    ps.Simplify(); ps.Fracture()
    for i in range(ps.OutlineCount()):
        one=p.SHAPE_POLY_SET(); one.AddOutline(ps.Outline(i))
        sh=p.PCB_SHAPE(b); sh.SetShape(p.SHAPE_T_POLY); sh.SetPolyShape(one)
        sh.SetFilled(True); sh.SetWidth(0); sh.SetLayer(p.B_SilkS); b.Add(sh)
    return ps.OutlineCount()
# logo: 14mm circle centred (44.2, 13)
L=json.load(open(S+"/logo_bitmap_dark.json")); N=L["N"]; D=14.0
print("logo shapes", add_cells(L["runs"], D/N, 44.2-D/2, 13.0-D/2, D))
# QR: 29 modules x 0.25mm, top-left (57.6, 39.4); silk = dark modules (black silk), 1mm quiet zone unprinted
M=json.load(open(S+"/qr_bitmap_dark.json"))["M"]; n=len(M); mod=0.25
runs=[]
for r,row in enumerate(M):
    c=0
    while c<n:
        if row[c]:
            s=c
            while c<n and row[c]: c+=1
            runs.append((r,s,c))
        else: c+=1
print("qr shapes", add_cells(runs, mod, 57.6, 39.4, n*mod))
b.Save(OUT)
