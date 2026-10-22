"""Add the hand-routed, locked USB tracks to the choir rev 2 board.

ESD (U4) -> ESP32 (U1) runs as a coupled pair on B.Cu: 0.3 mm traces, 0.2 mm gap.
J1 -> U4 is short and fixed by the receptacle's interleaved pins: D- joins above
the pads, D+ joins below them and drops through one via.

usage: usb_pair.py board.kicad_pcb [out.kicad_pcb]   (skips if the tracks are there)
"""
import math
import sys

import pcbnew

MM = pcbnew.FromMM
W_PAIR, W_STUB = 0.3, 0.25
SEP = 0.5                      # centre-to-centre: 0.3 width + 0.2 gap
VIA_D, VIA_DRILL = 0.6, 0.3

b = pcbnew.LoadBoard(sys.argv[1])
out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
USB_NETS = ('/D+', '/D-', '/USB_D+', '/USB_D-')
if any(t.GetNetname() in USB_NETS and t.IsLocked() for t in b.GetTracks()):
    print('USB tracks already present')
    sys.exit(0)
for t in list(b.GetTracks()):
    if t.GetNetname() in USB_NETS:
        b.Delete(t)


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


F, B = pcbnew.F_Cu, pcbnew.B_Cu

# --- U4 -> U1: coupled pair on B.Cu ------------------------------------------
# U1 end: pins 13 (D-, y 77.73) and 14 (D+, y 79.0) at x 62.05; vias just inside the module edge.
VM, VP = (63.65, 77.73), (63.65, 78.95)
# U4 end: pins 4 (D-, x 74.95) and 6 (D+, x 73.05) at y 95.626; vias between U4 and C9.
UM, UP = (74.95, 94.3), (73.05, 94.3)
X = 74.25                      # D- vertical run; D+ runs SEP to its left
YS = 93.6                      # where the pair splits around U4's VCC pin
k = SEP * math.sqrt(2)         # 45-degree lines y = x + c, SEP apart: c+ - c- = k
a = 64.6                       # D- turns 45 degrees here
c_m = VM[1] - a
c_p = c_m + k
b_p = VP[1] - c_p              # D+ turn point on its horizontal
track('/USB_D-', B, [VM, (a, VM[1]), (X, X + c_m), (X, YS), UM], W_PAIR)
track('/USB_D+', B, [VP, (b_p, VP[1]), (X - SEP, X - SEP + c_p), (X - SEP, YS), UP], W_PAIR)
for net, v in (('/USB_D-', VM), ('/USB_D+', VP), ('/USB_D-', UM), ('/USB_D+', UP)):
    via(net, *v)
# F.Cu stubs to the pads
track('/USB_D-', F, [(62.05, 77.73), VM], W_PAIR)
track('/USB_D+', F, [(62.05, 79.0), VP], W_PAIR)
track('/USB_D-', F, [UM, (74.95, 95.626)], W_PAIR)
track('/USB_D+', F, [UP, (73.05, 95.626)], W_PAIR)

# --- J1 -> U4 ----------------------------------------------------------------
# D-: B7 (73.73) and A7 (74.73) up to a bar above the pads, then to U4 pin 3.
track('/D-', F, [(73.73, 100.216), (73.73, 99.3), (74.73, 99.3)], W_STUB)
track('/D-', F, [(74.73, 100.216), (74.73, 99.3), (74.95, 99.08), (74.95, 97.9)], W_STUB)
# D+: A6 (74.23) and B6 (75.23) down to a bar below the pads, via to B.Cu, up to U4 pin 1.
track('/D+', F, [(74.23, 100.216), (74.23, 101.114), (75.23, 101.114), (75.23, 100.216)], W_STUB)
track('/D+', F, [(74.467, 101.114), (74.467, 101.291)], W_STUB)
via('/D+', 74.467, 101.291)
track('/D+', B, [(74.467, 101.291), (73.05, 99.874), (73.05, 98.95)], W_STUB)
via('/D+', 73.05, 98.95)
track('/D+', F, [(73.05, 98.95), (73.05, 97.9)], W_STUB)

pcbnew.SaveBoard(out, b)
print('USB tracks added')
