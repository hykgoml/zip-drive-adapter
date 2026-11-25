# v0.5 from v0.4: status LEDs on the right edge beside the Pico.
#   D1 PWR   <- +3V3 (always on)        D2 READY <- GP22 (firmware)
#   D3 READ  <- GP20 (Zip->PC, firmware) D4 WRITE <- GP21 (PC->Zip, firmware)
# Each: source -> 470R (standing) -> LED anode; LED cathode -> GND (pour).
# Usage: $KPY art/add_leds_v05.py zipbridge_v0.4.kicad_pcb placed.kicad_pcb
import sys, pcbnew as p
SRC, OUT = sys.argv[1], sys.argv[2]
MM = p.FromMM
FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
b = p.LoadBoard(SRC)
PICO = {20: 26, 21: 27, 22: 29}   # GPIO -> Pico pin
def net(name):
    n = b.FindNet(name)
    if n is None or n.GetNetCode() <= 0:
        n = p.NETINFO_ITEM(b, name); b.Add(n)
    return n
def place(lib, name, ref, val, x, y, rot=0):
    f = p.FootprintLoad(FP + lib + ".pretty", name); assert f, name
    f.SetReference(ref); f.SetValue(val); b.Add(f)
    f.SetPosition(p.VECTOR2I(MM(x), MM(y)))
    if rot: f.SetOrientationDegrees(rot)
    return f
def connect(f, num, netname):
    hit = False
    for q in f.Pads():
        if q.GetNumber() == str(num): q.SetNet(net(netname)); hit = True
    assert hit, (f.GetReference(), num)
fps = {f.GetReference(): f for f in b.GetFootprints()}
U4 = fps["U4"]
leds = [("PWR", "+3V3", "green"), ("READY", "GP22", "yellow"), ("READ", "GP20", "green"), ("WRITE", "GP21", "red")]
LED_Y = [13.75, 18.25, 22.75, 27.25]          # 4.5mm pitch (LED courtyard), below H1, right of C2
R_POS = [(54.8, 9.25), (56.0, 13.75), (56.0, 18.25), (56.0, 22.75)]  # staggered: clear of H1 and C2
for i, (lab, src, colour) in enumerate(leds):
    d = place("LED_THT", "LED_D3.0mm", f"D{i+1}", f"{lab} {colour}", 64.14, LED_Y[i], rot=180)  # K right, A left
    connect(d, 1, "GND"); connect(d, 2, f"LED_{lab}")
    for q in d.Pads():
        if q.GetNumber() == "1": q.SetLocalZoneConnection(p.ZONE_CONNECTION_FULL)
    r = place("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical", f"R{16+i}", "470", *R_POS[i])
    connect(r, 1, src); connect(r, 2, f"LED_{lab}")
    if src.startswith("GP"):
        connect(U4, PICO[int(src[2:])], src)
    for f in (d, r):
        f.Reference().SetVisible(False); f.Value().SetVisible(False)
    t = p.PCB_TEXT(b); t.SetText(lab if lab != "READY" else "RDY"); t.SetLayer(p.F_SilkS)
    t.SetTextSize(p.VECTOR2I(MM(0.8), MM(0.8))); t.SetTextThickness(MM(0.15))
    t.SetPosition(p.VECTOR2I(MM(59.95), MM(LED_Y[i]))); t.SetTextAngleDegrees(90); b.Add(t)
# move board title to the top-right and bump version
for dr in b.GetDrawings():
    if isinstance(dr, p.PCB_TEXT) and "ZIP PPA->USB" in dr.GetText():
        dr.SetText("ZIP PPA->USB v0.5"); dr.SetTextAngleDegrees(0)
        dr.SetTextSize(p.VECTOR2I(MM(0.8), MM(0.8))); dr.SetTextThickness(MM(0.15))
        dr.SetPosition(p.VECTOR2I(MM(58.8), MM(1.7)))
b.Save(OUT)
print("added", len(leds), "LEDs +", len(leds), "resistors")
