#!/usr/bin/python3
import sys, pcbnew as p
MM=p.FromMM
b=p.LoadBoard(sys.argv[1])
gnd=b.FindNet("GND")
for f in b.GetFootprints():
    for q in f.Pads():
        if (f.GetReference(),q.GetNumber()) in {("U2","6"),("U2","7"),("U2","8"),("U2","9"),("U3","11"),("U3","12"),("U3","13"),("U3","1"),("U4","13")}:
            q.SetZoneConnection(p.ZONE_CONNECTION_FULL)
for L in (p.F_Cu, p.B_Cu):
    z=p.ZONE(b)
    z.SetLayer(L); z.SetNet(gnd)
    z.SetLocalClearance(MM(0.3))
    z.SetMinThickness(MM(0.25))
    z.SetThermalReliefGap(MM(0.4))
    z.SetThermalReliefSpokeWidth(MM(0.4))
    z.SetPadConnection(p.ZONE_CONNECTION_THERMAL)
    z.SetIslandRemovalMode(p.ISLAND_REMOVAL_MODE_ALWAYS)
    ol=z.Outline(); ol.NewOutline()
    for x,y in [(0.4,0.4),(65.6,0.4),(65.6,79.4),(0.4,79.4)]:
        ol.Append(MM(x),MM(y))
    b.Add(z)
p.ZONE_FILLER(b).Fill(b.Zones())
b.Save(sys.argv[2])
