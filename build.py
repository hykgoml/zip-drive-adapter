#!/usr/bin/python3
# Zip PPA -> USB bridge board: Pico 2 + 3x SN74LVC245AN + DB25F
import os
import pcbnew as p

MM = p.FromMM
FP = os.environ.get("KICAD_FOOTPRINT_DIR",
                    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints") + "/"
b = p.BOARD()

# ---- design rules ----
ds = b.GetDesignSettings()
ds.m_TrackMinWidth = MM(0.25)
ds.m_MinClearance = MM(0.2)
ds.m_ViasMinSize = MM(0.6)
ds.m_MinThroughDrill = MM(0.3)
ns = ds.m_NetSettings
nc = ns.GetDefaultNetclass() if hasattr(ns, "GetDefaultNetclass") else ns.m_DefaultNetClass  # KiCad 9 / 7
nc.SetTrackWidth(MM(0.25))
nc.SetClearance(MM(0.2))
nc.SetViaDiameter(MM(0.6))
nc.SetViaDrill(MM(0.3))

nets = {}
def net(name):
    if name not in nets:
        n = p.NETINFO_ITEM(b, name)
        b.Add(n)
        nets[name] = n
    return nets[name]

def place(lib, name, ref, val, x, y, rot=0):
    fp = p.FootprintLoad(FP + lib + ".pretty", name)
    if fp is None and "_Female_" in name:  # KiCad 9 renamed DSUB Female -> Socket
        fp = p.FootprintLoad(FP + lib + ".pretty", name.replace("_Female_", "_Socket_"))
    assert fp is not None, (lib, name)
    fp.SetReference(ref)
    fp.SetValue(val)
    b.Add(fp)
    fp.SetPosition(p.VECTOR2I(MM(x), MM(y)))
    if rot:
        fp.SetOrientationDegrees(rot)
    return fp

def connect(fp, padnum, netname):
    hit = False
    for pad in fp.Pads():
        if pad.GetNumber() == str(padnum):
            pad.SetNet(net(netname))
            hit = True
    assert hit, (fp.GetReference(), padnum)

# ---- Pico 2 (custom TH footprint, USB pointing left) ----
PX, PY = 27.0, 13.0
pico = p.FOOTPRINT(b)
pico.SetReference("U4")
pico.SetValue("Pico 2 (RP2350)")
b.Add(pico)
pico.SetPosition(p.VECTOR2I(MM(PX), MM(PY)))
for n in range(1, 41):
    if n <= 20:   # bottom row, L->R pins 1..20
        x, y = -24.13 + (n - 1) * 2.54, 8.89
    else:         # top row, L->R pins 40..21
        x, y = -24.13 + (40 - n) * 2.54, -8.89
    pad = p.PAD(pico)
    pad.SetNumber(str(n))
    pad.SetAttribute(p.PAD_ATTRIB_PTH)
    pad.SetLayerSet(p.PAD.PTHMask())
    pad.SetShape(p.PAD_SHAPE_RECT if n == 1 else p.PAD_SHAPE_OVAL)
    pad.SetSize(p.VECTOR2I(MM(1.7), MM(1.7)))
    pad.SetDrillSize(p.VECTOR2I(MM(1.0), MM(1.0)))
    pico.Add(pad)
    if hasattr(pad, "SetPos0"):  # removed in KiCad 9; SetPosition below is enough
        pad.SetPos0(p.VECTOR2I(MM(x), MM(y)))
    pad.SetPosition(p.VECTOR2I(MM(PX + x), MM(PY + y)))
# outline on silk + courtyard
def rect(layer, x0, y0, x1, y1, w=0.15, parent=None):
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    for i in range(4):
        s = p.PCB_SHAPE(parent or b)
        s.SetShape(p.SHAPE_T_SEGMENT)
        s.SetStart(p.VECTOR2I(MM(pts[i][0]), MM(pts[i][1])))
        s.SetEnd(p.VECTOR2I(MM(pts[(i+1)%4][0]), MM(pts[(i+1)%4][1])))
        s.SetLayer(layer)
        s.SetWidth(MM(w))
        (parent or b).Add(s)
rect(p.F_SilkS, PX - 25.5, PY - 10.5, PX + 25.5, PY + 10.5)
rect(p.F_SilkS, PX - 25.5, PY - 4, PX - 21.5, PY + 4)   # USB marker

PICO = {}  # GPIO -> pin
order = {0:1,1:2,2:4,3:5,4:6,5:7,6:9,7:10,8:11,9:12,10:14,11:15,12:16,13:17,
         14:19,15:20,16:21,17:22,18:24,19:25,20:26,21:27,22:29}
for g, pin in order.items():
    PICO[g] = pin
for gnd in (3, 8, 13, 18, 23, 28, 33, 38):
    connect(pico, gnd, "GND")
connect(pico, 36, "+3V3")

# ---- DB25 female, right angle, mating face at bottom edge ----
BOARD_W, BOARD_H = 66.0, 79.8   # v0.4: edge moved in for a .318in (8.08mm) setback DB25
J = place("Connector_Dsub",
          "DSUB-25_Female_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm",
          "J1", "DB25 Female (to Zip)", 53.0, BOARD_H - 10.54)  # pin1 row = edge - 7.70 - 2.84
for g in range(18, 26):
    connect(J, g, "GND")
connect(J, 0, "GND")  # shell / mounting holes

# ---- buffers (DIP-20, vertical, pin1 top-left) ----
U3 = place("Package_DIP", "DIP-20_W7.62mm_Socket", "U3", "74LVC245 STATUS", 8.0, 30.0)
U1 = place("Package_DIP", "DIP-20_W7.62mm_Socket", "U1", "74LVC245 DATA", 27.0, 30.0)
U2 = place("Package_DIP", "DIP-20_W7.62mm_Socket", "U2", "74LVC245 CTRL", 46.0, 30.0)
for u in (U1, U2, U3):
    connect(u, 10, "GND")
    connect(u, 20, "+3V3")
B = {1: 18, 2: 17, 3: 16, 4: 15, 5: 14, 6: 13, 7: 12, 8: 11}  # Bn -> pin
A = {i: i + 1 for i in range(1, 9)}                         # An -> pin

# Status (U3): DIR=GND (B->A). DB25 10,11,12,13,15 -> GP0..GP4
status = [("ACK", 10), ("BUSY", 11), ("PE", 12), ("SEL", 13), ("ERR", 15)]
connect(U3, 1, "GND")
connect(U3, 19, "CTL_OE#")
for i, (nm, dpin) in enumerate(status):
    connect(U3, B[i + 1], "LPT_" + nm)
    connect(J, dpin, "LPT_" + nm)
    connect(U3, A[i + 1], f"GP{i}")
    connect(pico, PICO[i], f"GP{i}")
for i in (6, 7, 8):
    connect(U3, B[i], "GND")   # unused inputs tied low

# Data (U1): DIR=GP17 (high = Pico->cable). GP5..GP12 = D0..D7, series R1..R8
connect(U1, 1, "DATA_DIR")
connect(pico, PICO[17], "DATA_DIR")
connect(U1, 19, "DATA_OE#")
connect(pico, PICO[18], "DATA_OE#")
for i in range(8):
    g = 5 + i
    connect(U1, A[i + 1], f"GP{g}")
    connect(pico, PICO[g], f"GP{g}")
    connect(U1, B[i + 1], f"BUF_D{i}")

# Control (U2): DIR=3V3 (A->B). GP13..16 -> STROBE, AUTOFD, INIT, SELIN
ctrl = [("STROBE#", 1), ("AUTOFD#", 14), ("INIT#", 16), ("SELIN#", 17)]
connect(U2, 1, "+3V3")
connect(U2, 19, "CTL_OE#")
connect(pico, PICO[19], "CTL_OE#")
for i, (nm, dpin) in enumerate(ctrl):
    g = 13 + i
    connect(U2, A[i + 1], f"GP{g}")
    connect(pico, PICO[g], f"GP{g}")
    connect(U2, B[i + 1], "LPT_" + nm)
    connect(J, dpin, "LPT_" + nm)
for i in (5, 6, 7, 8):
    connect(U2, A[i], "GND")   # unused inputs tied low

# ---- resistors (vertical, 7.62mm pitch) ----
RLIB, RFP = "Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
RY = 55.5
def res(ref, val, x, n1, n2):
    r = place(RLIB, RFP, ref, val, x, RY, rot=-90)
    connect(r, 1, n1)
    connect(r, 2, n2)
    return r
for i in range(8):   # data series
    res(f"R{i+1}", "100", 26.0 + i * 3.0, f"BUF_D{i}", f"LPT_D{i}")
    connect(J, 2 + i, f"LPT_D{i}")
for i, (nm, _) in enumerate(status):  # status pull-ups
    res(f"R{9+i}", "10k", 9.0 + i * 3.0, "LPT_" + nm, "+3V3")
res("R14", "10k", 52.0, "DATA_OE#", "+3V3")
res("R15", "10k", 55.0, "CTL_OE#", "+3V3")

# ---- decoupling caps next to each VCC pin ----
CLIB, CFP = "Capacitor_THT", "C_Disc_D3.0mm_W1.6mm_P2.50mm"
for k, u in enumerate((U1, U2, U3)):
    ux = u.GetPosition().x / 1e6
    c = place(CLIB, CFP, f"C{k+1}", "100n", ux + 10.5, 28.0, rot=-90)
    connect(c, 1, "+3V3")
    connect(c, 2, "GND")

# ---- mounting holes (M3) ----
for i, (x, y) in enumerate([(62.0, 8.0), (62.5, 60.0), (3.5, 60.0)]):
    h = place("MountingHole", "MountingHole_3.2mm_M3", f"H{i+1}", "M3", x, y)

# ---- board outline ----
rect(p.Edge_Cuts, 0, 0, BOARD_W, BOARD_H, w=0.1)

# ---- silkscreen text ----
def text(s, x, y, size=1.0, layer=p.F_SilkS, mirror=False):
    t = p.PCB_TEXT(b)
    t.SetText(s)
    t.SetPosition(p.VECTOR2I(MM(x), MM(y)))
    t.SetLayer(layer)
    t.SetTextSize(p.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(size * 0.15))
    if mirror:
        t.SetMirrored(True)
    b.Add(t)
    return t
text("ZIP PPA->USB v0.5", 58.8, 1.7, 0.8)
tu=text("USB", 3.5, 13.0, 0.8); tu.SetTextAngleDegrees(90)
bt = p.B_SilkS
lines = ["GP0-4: ACK BUSY PE SEL ERR (in)",
         "GP5-12: D0-D7   GP17: DATA DIR",
         "GP13-16: STB AFD INIT SELIN (out)",
         "GP18: DATA OE#  GP19: CTL OE#",
         "U1-U3: SN74LVC245AN @3V3"]
for i, s in enumerate(lines):
    text(s, 27, 7.5 + i * 2.2, 1.1, bt, mirror=True)

# ---- status LEDs (v0.5): source -> 470R standing -> LED anode, cathode -> GND ----
# D1 PWR <- +3V3, D2 READY <- GP22, D3 READ <- GP20 (Zip->PC), D4 WRITE <- GP21 (PC->Zip)
LEDS = [("PWR", "+3V3", "green"), ("READY", 22, "yellow"), ("READ", 20, "green"), ("WRITE", 21, "red")]
LED_Y = [13.75, 18.25, 22.75, 27.25]
R_POS = [(54.8, 9.25), (56.0, 13.75), (56.0, 18.25), (56.0, 22.75)]
for i, (lab, src, colour) in enumerate(LEDS):
    d = place("LED_THT", "LED_D3.0mm", f"D{i+1}", f"{lab} {colour}", 64.14, LED_Y[i], rot=180)
    connect(d, 1, "GND"); connect(d, 2, f"LED_{lab}")
    r = place("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical", f"R{16+i}", "470", *R_POS[i])
    srcnet = src if isinstance(src, str) else f"GP{src}"
    connect(r, 1, srcnet); connect(r, 2, f"LED_{lab}")
    if not isinstance(src, str):
        connect(pico, PICO[src], srcnet)
    text(lab if lab != "READY" else "RDY", 59.95, LED_Y[i], 0.8).SetTextAngleDegrees(90)

pico.Value().SetVisible(False)
for u in (U1, U2, U3):
    r = u.Reference()
    r.SetPosition(p.VECTOR2I(u.GetPosition().x + MM(3.81), u.GetPosition().y + MM(11.43)))
    r.SetTextAngleDegrees(90)
b.Save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "zipbridge.kicad_pcb"))
print("nets:", len(nets), "footprints:", len(b.GetFootprints()))
