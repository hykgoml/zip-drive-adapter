# v0.4 from v0.3: board edge 82 -> 79.8mm so a 0.318" (8.08mm) setback DB25 sits flush; J1 -> 7.70mm-offset footprint variant.
import sys,pcbnew as p
SRC,OUT=sys.argv[1],sys.argv[2]
MM=p.FromMM; T=p.ToMM; NEWH=79.8
b=p.LoadBoard(SRC)
# outline
n=0
for d in b.GetDrawings():
    if d.GetLayer()==p.Edge_Cuts:
        for get,setf in ((d.GetStart,d.SetStart),(d.GetEnd,d.SetEnd)):
            v=get()
            if abs(T(v.y)-82.0)<1e-3: setf(p.VECTOR2I(v.x,MM(NEWH))); n+=1
    if isinstance(d,p.PCB_TEXT) and "v0.3" in d.GetText(): d.SetText(d.GetText().replace("v0.3","v0.4"))
print("edge points moved:",n)
# zones
for z in b.Zones():
    ol=z.Outline()
    for i in range(ol.TotalVertices()):
        v=ol.CVertex(i)
        if abs(T(v.y)-81.6)<1e-3: ol.SetVertex(i,p.VECTOR2I(v.x,MM(NEWH-0.4)))
# J1 footprint variant (identical pads/holes)
old=[f for f in b.GetFootprints() if f.GetReference()=="J1"][0]
new=p.FootprintLoad("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/Connector_Dsub.pretty",
    "DSUB-25_Socket_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm")
new.SetPosition(old.GetPosition()); new.SetReference("J1"); new.SetValue("DB25 F RA .318in (Connectors Pro B00C58IPSO)")
nets={}
for q in old.Pads(): nets.setdefault(q.GetNumber(),[]).append((q.GetNet(),q.GetPosition()))
for q in new.Pads():
    net,pos=nets[q.GetNumber()].pop(0)
    assert abs(T(pos.x)-T(q.GetPosition().x))<0.01 and abs(T(pos.y)-T(q.GetPosition().y))<0.01, q.GetNumber()
    q.SetNet(net)
b.Remove(old); b.Add(new)
p.ZONE_FILLER(b).Fill(b.Zones())
b.Save(OUT)
