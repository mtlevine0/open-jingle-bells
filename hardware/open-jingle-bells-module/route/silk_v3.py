"""Place reference text on the v3 (shrunk) choir board so it clears pads and other silk.

Each reference is tried at spots around its part, nearest first, at 0 and 90 degrees, and
keeps the first spot that doesn't touch a pad's mask opening, other silkscreen or the board
edge. The UART pin labels on the back go above J3's pins.

usage: silk_v3.py in.kicad_pcb out.kicad_pcb
"""
import math
import sys

import pcbnew

MM, TO = pcbnew.FromMM, pcbnew.ToMM
SIZE, THICK = 0.8, 0.15           # board minimums: 0.8 mm tall, 0.127 mm stroke
GAP = 0.15                        # silk-to-pad / silk-to-silk margin
EDGE_GAP = 0.3

b = pcbnew.LoadBoard(sys.argv[1])
eb = b.GetBoardEdgesBoundingBox()
EDGE = (TO(eb.GetX()), TO(eb.GetY()), TO(eb.GetRight()), TO(eb.GetBottom()))


def box(bb, grow=0.0):
    return (TO(bb.GetX()) - grow, TO(bb.GetY()) - grow, TO(bb.GetRight()) + grow, TO(bb.GetBottom()) + grow)


def hit(a, c):
    return not (a[2] <= c[0] or c[2] <= a[0] or a[3] <= c[1] or c[3] <= a[1])


# --- UART pin labels: back side, just above J3's pins -------------------------
j3 = {p.GetNumber(): p.GetPosition() for p in b.FindFootprintByReference('J3').Pads()}
LABEL_PIN = {'3.3v': '1', 'TX': '2', 'RX': '3', 'GND': '4'}
for d in b.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetLayer() == pcbnew.B_SilkS and d.GetText() in LABEL_PIN:
        p = j3[LABEL_PIN[d.GetText()]]
        d.SetPosition(pcbnew.VECTOR2I(p.x, p.y - MM(1.75)))
        d.SetTextAngleDegrees(0)
        d.SetTextSize(pcbnew.VECTOR2I(MM(SIZE), MM(SIZE)))
        d.SetTextThickness(MM(THICK))

# --- obstacles per silk side ---------------------------------------------------
def obstacles(side):
    cu = pcbnew.F_Cu if side == pcbnew.F_SilkS else pcbnew.B_Cu
    obs = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.IsOnLayer(cu):
                obs.append(('pad', box(p.GetBoundingBox(), GAP)))
        for g in fp.GraphicalItems():
            if g.GetLayer() == side and g.GetClass() != 'PCB_FIELD':
                obs.append(('silk', box(g.GetBoundingBox(), GAP)))
    for d in b.GetDrawings():
        if d.GetLayer() == side:
            obs.append(('silk', box(d.GetBoundingBox(), GAP)))
    return obs


placed = {pcbnew.F_SilkS: [], pcbnew.B_SilkS: []}
OBS = {s: obstacles(s) for s in placed}
SKIP = {'U1', 'J2'}               # their text is fine where it is
order = sorted((fp for fp in b.GetFootprints() if fp.GetReference() not in SKIP),
               key=lambda f: (TO(f.GetPosition().y), TO(f.GetPosition().x)))
for fp in order:
    ref = fp.Reference()
    side = ref.GetLayer()
    ref.SetTextSize(pcbnew.VECTOR2I(MM(SIZE), MM(SIZE)))
    ref.SetTextThickness(MM(THICK))
    cx, cy = TO(fp.GetPosition().x), TO(fp.GetPosition().y)
    cands = []
    for r in [i * 0.25 for i in range(0, 29)]:
        for k in range(16):
            a = 2 * math.pi * k / 16
            cands.append((r, cx + r * math.cos(a), cy + r * math.sin(a)))
    cands.sort()
    done = False
    for _, x, y in cands:
        for ang in (0, 90):
            ref.SetTextAngleDegrees(ang)
            ref.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            tb = box(ref.GetBoundingBox())
            if (tb[0] < EDGE[0] + EDGE_GAP or tb[1] < EDGE[1] + EDGE_GAP
                    or tb[2] > EDGE[2] - EDGE_GAP or tb[3] > EDGE[3] - EDGE_GAP):
                continue
            if any(hit(tb, o) for kind, o in OBS[side]) or any(hit(tb, o) for o in placed[side]):
                continue
            placed[side].append(box(ref.GetBoundingBox(), GAP))
            done = True
            break
        if done:
            break
    print(fp.GetReference(), 'placed' if done else 'NO SPOT', round(TO(ref.GetPosition().x), 2),
          round(TO(ref.GetPosition().y), 2), ref.GetTextAngleDegrees())

pcbnew.SaveBoard(sys.argv[2], b)
