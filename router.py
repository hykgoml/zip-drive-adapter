#!/usr/bin/python3
"""Simple 2-layer grid A* router with exact-geometry clearance fields."""
import heapq, math, sys, random
import numpy as np
import pcbnew as p

IN, OUT = sys.argv[1], sys.argv[2]
# Optional: `router.py in out --only NET [NET ...]` keeps all existing tracks/vias as fixed
# copper and routes only the listed nets (existing copper of those nets counts as connected).
ONLY = sys.argv[sys.argv.index("--only") + 1:] if "--only" in sys.argv else None
G = 0.254                     # grid pitch (mm)
X0, Y0 = 0.076, 0.028         # grid origin aligned to DIP/Pico pin gaps
TW, CL, VD, VDR = 0.25, 0.2, 0.6, 0.3
MARG = 0.03
T_TRK = TW / 2 + CL + MARG    # min dist from track centre to foreign copper
T_VIA = VD / 2 + CL + MARG
EDGE = 0.5
R_WIN = 1.2                   # field cap

b = p.LoadBoard(IN)
W, H = 66.0, 79.8
NX = int((W - X0) / G) + 1
NY = int((H - Y0) / G) + 1
xs = X0 + np.arange(NX) * G
ys = Y0 + np.arange(NY) * G
GX, GY = np.meshgrid(xs, ys, indexing="ij")
LAYERS = [p.F_Cu, p.B_Cu]

# ---- copper objects: (net, layers(set of 0/1), kind, params) ----
objs = []
pads_by_net = {}
for fp in b.GetFootprints():
    for pad in fp.Pads():
        c = p.ToMM(pad.GetPosition())
        sz = p.ToMM(pad.GetSize())
        n = pad.GetNetname() or None
        if pad.GetAttribute() == p.PAD_ATTRIB_NPTH:
            objs.append((None, {0, 1}, "circ", (c, sz[0] / 2)))
            continue
        if pad.GetShape() == p.PAD_SHAPE_RECT:
            o = (n, {0, 1}, "rect", (c, sz[0] / 2, sz[1] / 2))
        else:
            o = (n, {0, 1}, "circ", (c, sz[0] / 2))
        objs.append(o)
        if n:
            pads_by_net.setdefault(n, []).append(o)

def window(c, ext):
    i0 = max(0, int((c[0] - ext - X0) / G)); i1 = min(NX, int((c[0] + ext - X0) / G) + 2)
    j0 = max(0, int((c[1] - ext - Y0) / G)); j1 = min(NY, int((c[1] + ext - Y0) / G) + 2)
    return slice(i0, i1), slice(j0, j1)

def obj_dist(o):
    """returns (slices, dist array) of distance from grid pts to object edge."""
    kind, prm = o[2], o[3]
    if kind == "circ":
        c, r = prm
        si, sj = window(c, r + R_WIN)
        d = np.hypot(GX[si, sj] - c[0], GY[si, sj] - c[1]) - r
    elif kind == "rect":
        c, hx, hy = prm
        si, sj = window(c, max(hx, hy) + R_WIN)
        qx = np.abs(GX[si, sj] - c[0]) - hx
        qy = np.abs(GY[si, sj] - c[1]) - hy
        d = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0)
    else:  # seg
        a, bb, r = prm
        cx, cy = (a[0] + bb[0]) / 2, (a[1] + bb[1]) / 2
        ext = math.hypot(bb[0] - a[0], bb[1] - a[1]) / 2 + r + R_WIN
        si, sj = window((cx, cy), ext)
        px, py = GX[si, sj] - a[0], GY[si, sj] - a[1]
        vx, vy = bb[0] - a[0], bb[1] - a[1]
        L2 = vx * vx + vy * vy
        t = np.clip((px * vx + py * vy) / L2, 0, 1) if L2 > 0 else 0
        d = np.hypot(px - t * vx, py - t * vy) - r
    return si, sj, d

cache = {}
def dist_of(idx):
    if idx not in cache:
        cache[idx] = obj_dist(objs[idx])
    return cache[idx]

edge_d = np.minimum.reduce([GX, W - GX, GY, H - GY])

def fields(net):
    F = [np.full((NX, NY), R_WIN) for _ in LAYERS]
    own = [np.zeros((NX, NY), bool) for _ in LAYERS]
    for k, o in enumerate(objs):
        si, sj, d = dist_of(k)
        for L in o[1]:
            if o[0] == net and net is not None:
                own[L][si, sj] |= d <= 0
            else:
                F[L][si, sj] = np.minimum(F[L][si, sj], d)
    for L in (0, 1):
        F[L] = np.minimum(F[L], edge_d - EDGE + T_TRK)
    return F, own

DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]
VIA_COST = 6.0

def astar(F, own, sources, target_mask, tgt_c):
    okT = [F[L] >= T_TRK for L in (0, 1)]
    okD = [F[L] >= T_TRK + 0.06 for L in (0, 1)]
    for L in (0, 1):
        okT[L] |= own[L]; okD[L] |= own[L]
    okV = (F[0] >= T_VIA) & (F[1] >= T_VIA)
    tx, ty = (tgt_c[0] - X0) / G, (tgt_c[1] - Y0) / G
    def h(i, j):
        dx, dy = abs(i - tx), abs(j - ty)
        return (max(dx, dy) + 0.4142 * min(dx, dy))
    best = {}
    prev = {}
    pq = []
    for s in sources:
        best[s] = 0.0
        heapq.heappush(pq, (h(s[1], s[2]), 0.0, s))
    while pq:
        f, g, s = heapq.heappop(pq)
        if g > best.get(s, 1e18) + 1e-9:
            continue
        L, i, j = s
        if target_mask[L][i, j]:
            path = [s]
            while path[-1] in prev:
                path.append(prev[path[-1]])
            return path[::-1]
        for di, dj, c in DIRS:
            ni, nj = i + di, j + dj
            if not (0 <= ni < NX and 0 <= nj < NY):
                continue
            ok = okD[L] if c > 1 else okT[L]
            if not ok[ni, nj]:
                continue
            if c > 1 and not (okT[L][i + di, j] and okT[L][i, j + dj]):
                continue
            # layer direction preference: F vertical, B horizontal
            pen = 1.0
            if L == 0 and dj == 0: pen = 1.5
            if L == 1 and di == 0: pen = 1.5
            ng = g + c * pen
            t = (L, ni, nj)
            if ng < best.get(t, 1e18) - 1e-9:
                best[t] = ng; prev[t] = s
                heapq.heappush(pq, (ng + h(ni, nj), ng, t))
        if okV[i, j]:
            t = (1 - L, i, j)
            ng = g + VIA_COST
            if ng < best.get(t, 1e18) - 1e-9:
                best[t] = ng; prev[t] = s
                heapq.heappush(pq, (ng + h(i, j), ng, t))
    return None

def pt(i, j):
    return (X0 + i * G, Y0 + j * G)

def foreign_arrays(net, L):
    C, S = [], []
    for o in objs:
        if o[0] == net and net is not None: continue
        if L not in o[1]: continue
        if o[2] == "circ": C.append((o[3][0][0], o[3][0][1], o[3][1]))
        elif o[2] == "rect": C.append((o[3][0][0], o[3][0][1], math.hypot(o[3][1], o[3][2])))
        else: S.append((o[3][0][0], o[3][0][1], o[3][1][0], o[3][1][1], o[3][2]))
    return np.array(C).reshape(-1, 3), np.array(S).reshape(-1, 5)

def seg_clear(a, bb, C, S, thr):
    L = math.hypot(bb[0]-a[0], bb[1]-a[1])
    n = max(2, int(L / 0.05) + 1)
    t = np.linspace(0, 1, n)
    px = a[0] + t * (bb[0]-a[0]); py = a[1] + t * (bb[1]-a[1])
    if len(C):
        d = np.hypot(px[:, None]-C[None, :, 0], py[:, None]-C[None, :, 1]) - C[None, :, 2]
        if d.min() < thr: return False
    if len(S):
        ax, ay, bx, by, r = (S[:, k][None, :] for k in range(5))
        vx, vy = bx-ax, by-ay
        L2 = np.maximum(vx*vx+vy*vy, 1e-12)
        qx, qy = px[:, None]-ax, py[:, None]-ay
        tt = np.clip((qx*vx+qy*vy)/L2, 0, 1)
        d = np.hypot(qx-tt*vx, qy-tt*vy) - r
        if d.min() < thr: return False
    return True

def smooth(net, pts):
    if len(pts) < 3: return pts
    L = pts[0][0]
    C, S = foreign_arrays(net, L)
    thr = TW/2 + CL + 0.03
    out = [pts[0]]; i = 0
    while i < len(pts) - 1:
        j = len(pts) - 1
        while j > i + 1 and not seg_clear(pt(pts[i][1], pts[i][2]), pt(pts[j][1], pts[j][2]), C, S, thr):
            j -= 1
        out.append(pts[j]); i = j
    return out

def path_to_objs(net, path):
    new = []
    # vias
    for a, bb in zip(path, path[1:]):
        if a[0] != bb[0]:
            new.append((net, {0, 1}, "circ", (pt(a[1], a[2]), VD / 2), "via"))
    # segments: split runs by layer and direction
    run = [path[0]]
    def flush(run):
        if len(run) < 2: return
        pts = [run[0]]
        for k in range(1, len(run) - 1):
            d1 = (run[k][1] - pts[-1][1], run[k][2] - pts[-1][2])
            d2 = (run[k + 1][1] - run[k][1], run[k + 1][2] - run[k][2])
            n1 = (np.sign(d1[0]), np.sign(d1[1]))
            if n1 != (np.sign(d2[0]), np.sign(d2[1])):
                pts.append(run[k])
        pts.append(run[-1])
        pts = smooth(net, pts)
        for a, bb in zip(pts, pts[1:]):
            new.append((net, {a[0]}, "seg", (pt(a[1], a[2]), pt(bb[1], bb[2]), TW / 2), "trk"))
    for s in path[1:]:
        if s[0] != run[-1][0]:
            flush(run); run = [s]
        else:
            run.append(s)
    flush(run)
    return new

def route_all(order):
    global objs, cache
    base = len(base_objs)
    objs = list(base_objs); cache = {k: v for k, v in base_cache.items()}
    failed = []
    for net in order:
        pads = pads_by_net[net]
        conn = [pads[0]]
        todo = pads[1:]
        while todo:
            F, own = fields(net)
            # connected copper mask = cells inside any connected object of this net
            src_mask = [np.zeros((NX, NY), bool) for _ in (0, 1)]
            conn_idx = [k for k, o in enumerate(objs) if o[0] == net and (o in conn or (len(o) > 4))]
            for k in conn_idx:
                si, sj, d = dist_of(k)
                for L in objs[k][1]:
                    src_mask[L][si, sj] |= d <= 0
            # nearest pad
            cs = [o[3][0] for o in conn if o[2] != "seg"]
            def dmin(o):
                c = o[3][0]
                return min(math.hypot(c[0] - q[0], c[1] - q[1]) for q in cs)
            todo.sort(key=dmin)
            tgt = todo[0]
            k = objs.index(tgt)
            si, sj, d = dist_of(k)
            tmask = [np.zeros((NX, NY), bool) for _ in (0, 1)]
            for L in (0, 1):
                tmask[L][si, sj] = d <= -0.15
            sources = [(L, int(i), int(j)) for L in (0, 1) for i, j in zip(*np.nonzero(src_mask[L]))
                       if F[L][i, j] >= T_TRK or own[L][i, j]]
            path = astar(F, own, sources, tmask, tgt[3][0])
            if path is None:
                failed.append(net)
                break
            objs.extend(path_to_objs(net, path))
            conn.append(tgt)
            todo.pop(0)
    return failed

if ONLY is not None:
    for t in b.GetTracks():
        n = t.GetNetname() or None
        if isinstance(t, p.PCB_VIA):
            objs.append((n, {0, 1}, "circ", (p.ToMM(t.GetPosition()), p.ToMM(t.GetWidth(p.F_Cu)) / 2), "via"))
        elif t.GetLayer() in LAYERS:
            objs.append((n, {LAYERS.index(t.GetLayer())}, "seg",
                         (p.ToMM(t.GetStart()), p.ToMM(t.GetEnd()), p.ToMM(t.GetWidth()) / 2), "trk"))
base_objs = list(objs)
base_cache = {}
for k in range(len(base_objs)):
    base_cache[k] = obj_dist(k) if False else None
base_cache = {k: obj_dist(base_objs[k]) for k in range(len(base_objs))}

nets = list(pads_by_net)
def span(n):
    cs = [o[3][0] for o in pads_by_net[n]]
    return max(c[0] for c in cs) - min(c[0] for c in cs) + max(c[1] for c in cs) - min(c[1] for c in cs)
power = [n for n in nets if n in ("+3V3", "GND")]
sig = sorted([n for n in nets if n not in power], key=span)
order = sig + ["+3V3", "GND"]
if ONLY is not None:
    order = [n for n in ONLY if n in pads_by_net]
    print("routing only:", order, flush=True)

for attempt in range(12):
    failed = route_all(order)
    print(f"attempt {attempt}: failed {failed}", flush=True)
    if not failed:
        break
    order = failed + [n for n in order if n not in failed]

# ---- write to board ----
netmap = {n: b.FindNet(n) for n in nets}
for o in objs[len(base_objs):]:
    if o[4] == "via":
        v = p.PCB_VIA(b)
        b.Add(v)
        c = o[3][0]
        v.SetPosition(p.VECTOR2I(p.FromMM(c[0]), p.FromMM(c[1])))
        v.SetWidth(p.FromMM(VD)); v.SetDrill(p.FromMM(VDR))
        v.SetNet(netmap[o[0]])
    else:
        t = p.PCB_TRACK(b)
        b.Add(t)
        a, bb, _ = o[3]
        t.SetStart(p.VECTOR2I(p.FromMM(a[0]), p.FromMM(a[1])))
        t.SetEnd(p.VECTOR2I(p.FromMM(bb[0]), p.FromMM(bb[1])))
        t.SetWidth(p.FromMM(TW))
        t.SetLayer(LAYERS[list(o[1])[0]])
        t.SetNet(netmap[o[0]])
b.Save(OUT)
print("tracks/vias:", len(objs) - len(base_objs), "failed:", failed)
