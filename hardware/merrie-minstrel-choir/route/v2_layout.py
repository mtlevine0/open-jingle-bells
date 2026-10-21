"""Turn the v1 choir board into v2: new nets, J2 on the bottom, smaller outline, unrouted.

usage: /usr/bin/python3 v2_layout.py board.kicad_pcb netlist.net
"""
import re
import sys

import pcbnew

MM = pcbnew.FromMM
BOARD, NETLIST = sys.argv[1], sys.argv[2]

# New outline. Left edge and bottom edge stay put (J2 mounting holes, USB-C opening).
X0, Y0, X1, Y1 = 47.0, 55.0, 80.5, 108.0
ANTENNA_KEEPOUT_Y = 61.0  # no pour above this (module antenna)

b = pcbnew.LoadBoard(BOARD)


def pt(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


# --- 1. strip routing -------------------------------------------------------
for t in list(b.GetTracks()):
    b.Delete(t)
print('tracks left', len(b.GetTracks()), flush=True)

# --- 2. pad nets from the v2 schematic netlist -----------------------------
text = open(NETLIST).read()
pad_net = {}
for blk in re.split(r'\n\t\t\(net\b', text[text.index('(nets'):])[1:]:
    name = re.search(r'\(name "([^"]*)"\)', blk).group(1)
    for ref, pin in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk):
        pad_net[(ref, pin)] = name

nets = {n: b.FindNet(n) for n in set(pad_net.values())}
for name, net in nets.items():
    if net is None:
        net = pcbnew.NETINFO_ITEM(b, name)
        b.Add(net)
        nets[name] = net

changed = []
for fp in b.GetFootprints():
    for pad in fp.Pads():
        key = (fp.GetReference(), pad.GetNumber())
        if key in pad_net and pad.GetNetname() != pad_net[key]:
            changed.append(f'{key[0]}.{key[1]}: {pad.GetNetname()} -> {pad_net[key]}')
            pad.SetNet(nets[pad_net[key]])
print('\n'.join(changed))

# --- 3. J2 to the bottom side, same slot position ---------------------------
j2 = b.FindFootprintByReference('J2')
before = {p.GetNumber(): p.GetPosition() for p in j2.Pads()}
cx = sum(v.x for v in before.values()) // len(before)
cy = sum(v.y for v in before.values()) // len(before)
j2.Flip(pcbnew.VECTOR2I(cx, cy), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
after = {p.GetNumber(): p.GetPosition() for p in j2.Pads()}
ax = sum(v.x for v in after.values()) // len(after)
ay = sum(v.y for v in after.values()) // len(after)
j2.Move(pcbnew.VECTOR2I(cx - ax, cy - ay))
# Sanity: the pad set must occupy the same holes, mirrored top-to-bottom.
for n, p in ((n, p.GetPosition()) for n, p in ((p.GetNumber(), p) for p in j2.Pads())):
    o = before[n]
    assert abs(p.x - o.x) < 10 and abs(p.y - (2 * cy - o.y)) < 10, (n, p, o)

# --- 4. placement -----------------------------------------------------------
PLACE = {
    # ref: (x, y, rot)
    'U1': (70.8, 67.75, 0),
    # 3V3 decoupling + EN RC, tucked under the J2 mounting hole above the pin field
    'C7': (58.6, 62.1, 180),
    'C8': (58.6, 64.2, 180),
    'C3': (54.6, 62.0, 0),
    'R1': (54.7, 64.2, 0),
    # buttons and their caps keep their v1 spots
    'C5': (65.5, 86.9, -90),
    'C6': (70.0, 86.8, 90),
    'S2': (65.5, 93.9, 90),
    'S1': (70.0, 93.9, 90),
    # USB side, pulled in 1.3-1.5 mm for the narrower board
    'C9': (73.9, 92.1, -90),
    'U4': (74.0, 96.763, 90),
    'R5': (77.6, 94.0, 180),
    'D3': (78.1, 96.3, 180),
    'J3': (78.7, 90.98, 180),
    'J1': (74.48, 103.886, 0),
    'R3': (68.0, 105.7, -90),
    'R4': (65.0, 105.7, -90),
    # regulator corner, shifted left 1.1 mm to make room for R4
    'U2': (57.4, 103.7, 90),
    'C1': (64.963, 101.7, 0),
    'C2': (62.9, 104.938, -90),
    'D2': (56.6, 97.4, 0),
    'R2': (60.9, 97.4, 180),
}
j3_old = b.FindFootprintByReference('J3').GetPosition()
for ref, (x, y, rot) in PLACE.items():
    fp = b.FindFootprintByReference(ref)
    fp.SetPosition(pt(x, y))
    fp.SetOrientationDegrees(rot)
j3_delta = b.FindFootprintByReference('J3').GetPosition() - j3_old

# reference/value text that the moves pushed onto pads or other silk
FIELDS = {
    # (ref, field): (x, y, angle, size)
    ('C3', 'Reference'): (51.5, 62.0, 0, 0.8),
    ('R1', 'Reference'): (51.5, 64.2, 0, 0.8),
    ('C8', 'Reference'): (56.9, 66.0, 0, 0.8),
    ('J3', 'Reference'): (76.4, 87.2, 90, 1.0),
    ('U4', 'Reference'): (76.3, 99.0, 0, 0.8),
    ('S1', 'Value'): (72.5, 92.9, 90, 0.8),
    ('R4', 'Reference'): (64.9, 103.35, 0, 0.8),
    ('C2', 'Reference'): (62.9, 107.35, 0, 0.8),
}
for (ref, field), (x, y, ang, size) in FIELDS.items():
    fp = b.FindFootprintByReference(ref)
    f = fp.Reference() if field == 'Reference' else fp.Value()
    f.SetPosition(pt(x, y))
    f.SetTextAngleDegrees(ang)
    f.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    f.SetTextThickness(MM(max(size * 0.15, 0.15)))

# --- 5. outline, pour, documentation ---------------------------------------
for d in list(b.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        b.Remove(d)
rect = pcbnew.PCB_SHAPE(b, pcbnew.SHAPE_T_RECTANGLE)
rect.SetLayer(pcbnew.Edge_Cuts)
rect.SetStart(pt(X0, Y0))
rect.SetEnd(pt(X1, Y1))
rect.SetWidth(MM(0.05))
b.Add(rect)

for z in b.Zones():
    o = z.Outline()
    o.RemoveAllContours()
    o.NewOutline()
    for x, y in ((X1, ANTENNA_KEEPOUT_Y), (X0, ANTENNA_KEEPOUT_Y), (X0, Y1), (X1, Y1)):
        o.Append(MM(x), MM(y))

for d in b.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetLayer() == pcbnew.B_SilkS:
        d.Move(j3_delta)  # UART pin labels follow J3
    elif d.GetClass().startswith('PCB_DIM'):
        # these were drawn on F.Cu; they are documentation, not copper
        d.SetLayer(pcbnew.Dwgs_User)
        s, e = d.GetStart(), d.GetEnd()
        if s.y == e.y and s.y == MM(108):      # width
            d.SetEnd(pt(X1, Y1))
        elif s.x == e.x:                       # height
            d.SetStart(pt(X0, Y0))
        else:                                  # 17.5 mm datum along the top
            d.SetStart(pt(X0, Y0))
            d.SetEnd(pt(X0 + 17.5, Y0))
        d.Update()

tb = b.GetTitleBlock()
tb.SetRevision('2')
tb.SetDate('2026-10-21')

b.BuildConnectivity()
pcbnew.SaveBoard(BOARD, b)
print('saved', BOARD)
