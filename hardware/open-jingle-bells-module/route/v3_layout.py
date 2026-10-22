"""Shrink the choir rev 2 board: R/C parts to the back, J1 and the rest pulled up under U1.

Strips all routing; route again with the DSN pipeline afterwards.

usage: /usr/bin/python3 v3_layout.py in.kicad_pcb out.kicad_pcb
"""
import sys

import pcbnew

MM = pcbnew.FromMM
SRC, DST = sys.argv[1], sys.argv[2]

# Outline. Left/top/right as rev 2; the bottom edge comes up from 108 to Y1. J2's lowest
# pins sit at y 94.55, so the edge can't go much higher.
X0, Y0, X1, Y1 = 53.7, 61.0, 80.5, 98.0
ANTENNA_KEEPOUT_Y = 61.0

b = pcbnew.LoadBoard(SRC)


def pt(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


for t in list(b.GetTracks()):
    b.Delete(t)

# J1 keeps its rev 2 offset from the edge: courtyard 0.065 mm inside it.
J1_Y = Y1 - 0.065 - 4.074

TOP = {
    # ref: (x, y, rot)
    # row A, under U1: status LEDs, then J3 on its side along the module's bottom edge
    'D2': (65.6, 83.4, 90),
    'D3': (67.9, 83.4, 90),
    'J3': (71.0, 83.4, 90),
    # row B: buttons, then the USB ESD array right above J1's data pins
    'S2': (63.3, 87.3, 0),
    'S1': (69.6, 87.3, 0),
    'U4': (75.0, 87.3, 0),
    # row C on the bottom edge: regulator and USB-C
    'U2': (64.0, 93.7, 0),
    'J1': (75.0, J1_Y, 0),
}
BACK = {
    # 3V3 decoupling behind U1 pin 2, EN RC behind U1 pin 3
    'C8': (61.9, 63.3, 90),
    'C7': (64.1, 63.3, 90),
    'R1': (61.7, 67.4, 90),
    'C3': (63.75, 67.4, 90),
    # LED resistors behind their LEDs, GPIO0 pull-up behind BOOT
    'R2': (65.6, 83.4, 90),
    'R5': (67.9, 83.4, 90),
    'R6': (69.6, 87.3, 0),
    # VBUS cap behind U4, CC pull-downs behind J1
    'C9': (75.0, 87.3, 0),
    'R3': (73.3, J1_Y + 0.6, 90),
    'R4': (76.1, J1_Y + 0.6, 90),
    # LDO in/out caps behind U2
    'C2': (62.4, 93.5, 90),
    'C1': (66.4, 93.5, 90),
}

for ref, (x, y, rot) in TOP.items():
    fp = b.FindFootprintByReference(ref)
    if fp.IsFlipped():
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
    fp.SetPosition(pt(x, y))
    fp.SetOrientationDegrees(rot)
for ref, (x, y, rot) in BACK.items():
    fp = b.FindFootprintByReference(ref)
    if not fp.IsFlipped():
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
    fp.SetPosition(pt(x, y))
    fp.SetOrientationDegrees(rot)

# Reference text: centred on small back-side parts (they're tiny and dense), above the
# top-side parts. Fine-tuned after routing.
for fp in b.GetFootprints():
    if fp.GetReference() in BACK:
        r = fp.Reference()
        r.SetPosition(fp.GetPosition())
        r.SetTextAngleDegrees(fp.GetOrientationDegrees())
        r.SetTextSize(pcbnew.VECTOR2I(MM(0.6), MM(0.6)))
        r.SetTextThickness(MM(0.1))

# --- outline and pours -------------------------------------------------------
for d in list(b.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        b.Delete(d)
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
for d in list(b.GetDrawings()):
    if d.GetClass().startswith('PCB_DIM'):
        s, e = d.GetStart(), d.GetEnd()
        if s.y == e.y:                     # width, along the bottom edge
            d.SetStart(pt(X0, Y1))
            d.SetEnd(pt(X1, Y1))
        else:                              # height
            d.SetStart(pt(X0, Y0))
            d.SetEnd(pt(X0, Y1))
        d.Update()

b.BuildConnectivity()
pcbnew.SaveBoard(DST, b)
print('saved', DST)
