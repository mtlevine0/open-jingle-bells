"""Hand-route the J1 / U4 / C9 corner of the v3 (shrunk) choir board as locked tracks.

Freerouting left this corner incomplete: J1's left VBUS pin unconnected and U4's GND pin
cut off from C9, which sits behind U4 on the back. Fixed here:
  - D-: B7 and A7 join on a bar above the pads, then straight up to U4 pin 3.
  - D+: A6 and B6 join on a bar below the pads, drop through a via, run up B.Cu between
        C9's pads and come back to F.Cu above U4 to reach pin 1.
  - GND: a via beside U4 pin 2 ties it to C9 pad 1 on the back.
  - VBUS: J1's left VBUS pin gets a via; Freerouting joins it to the rest of VCC.

usage: usb_v3.py board.kicad_pcb [out.kicad_pcb]
"""
import sys

import pcbnew

MM = pcbnew.FromMM
VIA_D, VIA_DRILL = 0.6, 0.3
F, B = pcbnew.F_Cu, pcbnew.B_Cu

b = pcbnew.LoadBoard(sys.argv[1])
out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]


def pads(ref):
    fp = b.FindFootprintByReference(ref)
    return {p.GetNumber(): (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
            for p in fp.Pads() if p.GetNumber()}


def track(net, layer, pts, w):
    n = b.FindNet(net)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(MM(x0), MM(y0)))
        t.SetEnd(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        t.SetWidth(MM(w))
        t.SetLayer(layer)
        t.SetNet(n)
        t.SetLocked(True)
        b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    v.SetWidth(MM(VIA_D))
    v.SetDrill(MM(VIA_DRILL))
    v.SetNet(b.FindNet(net))
    v.SetLocked(True)
    b.Add(v)


J, U, C = pads('J1'), pads('U4'), pads('C9')
ROW = J['A6'][1]                 # J1 pad row; pads are 1.14 mm tall
TOP, BOT = ROW - 0.898, ROW + 0.898  # bars clear of the pads by 0.2 mm

# D-: B7, A7 -> bar above -> U4 pin 3 (directly above B7)
xb7, xa7 = J['B7'][0], J['A7'][0]
track('/D-', F, [J['B7'], (xb7, TOP)], 0.25)
track('/D-', F, [J['A7'], (xa7, TOP), (xb7, TOP)], 0.25)
track('/D-', F, [(xb7, TOP), (xb7, U['3'][1] + 0.388), U['3']], 0.25)

# D+: A6, B6 -> bar below -> via -> B.Cu up between C9's pads -> via above U4 -> pin 1
xa6, xb6 = J['A6'][0], J['B6'][0]
xmid = (C['1'][0] + C['2'][0]) / 2            # centre of C9's pad gap (back side)
v1 = (xmid, BOT + 0.66)
v2 = (xmid, U['1'][1] - 1.05)
track('/D+', F, [J['A6'], (xa6, BOT), (xb6, BOT), J['B6']], 0.25)
track('/D+', F, [(xmid, BOT), v1], 0.25)
via('/D+', *v1)
track('/D+', B, [v1, v2], 0.25)
via('/D+', *v2)
px, py = U['1']
track('/D+', F, [v2, (px + 0.44, v2[1]), (px, v2[1] + 0.44), (px, py)], 0.25)

# GND: via just left of U4 pin 2, stubs to pin 2 (F) and C9 pad 1 (B)
g = (U['2'][0] - 0.912, U['2'][1])
via('GND', *g)
track('GND', F, [g, U['2']], 0.3)
track('GND', B, [g, C['1']], 0.3)

# VBUS: J1's left VBUS pin up to a via above the pad row
vb = (J['A4-B9'][0], ROW - 1.24)
track('VCC', F, [J['A4-B9'], vb], 0.45)
via('VCC', *vb)

pcbnew.SaveBoard(out, b)
print('J1/U4 corner routed', 'v1', v1, 'v2', v2, 'gnd via', g, 'vbus via', vb)
