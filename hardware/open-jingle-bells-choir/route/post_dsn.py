"""Prepare a KiCad DSN for Freerouting (see player-pcb-routing-workflow notes)."""
import re, sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
# planes out: GND is routed as a net, the pours are refilled in KiCad afterwards
s = re.sub(r'\n\s*\(plane GND \(polygon [^)]*\)\)', '', s)
# inset the boundary 0.5 mm (Freerouting ignores copper-to-edge)
def inset(m):
    nums = list(map(float, m.group(2).split()))
    xs, ys = nums[0::2], nums[1::2]
    x0, x1, y0, y1 = min(xs) + 500, max(xs) - 500, min(ys) + 500, max(ys) - 500
    return f'{m.group(1)}{x1:g} {y0:g}  {x0:g} {y0:g}  {x0:g} {y1:g}  {x1:g} {y1:g}  {x1:g} {y0:g})'
s = re.sub(r'(\(boundary\s*\(path pcb 0\s+)([-\d.\s]+)\)', inset, s, count=1)
# J1's hyphenated pin names confuse Freerouting
for a in ('A1-B12', 'A4-B9', 'B1-A12', 'B4-A9'):
    s = s.replace(f'"{a}"', a.replace('-', '_'))
# net classes: power wide, signals 0.25 so the 75 % neck-down stays above 0.1524
power = ['GND', 'VCC', '+3.3V']
m = re.search(r'\(class kicad_default (.*?)\(circuit', s, re.S)
names = m.group(1).split()
sig = [n for n in names if n not in power]
cls = ('(class kicad_default ' + ' '.join(sig) + '\n      (circuit\n        (use_via "Via[0-1]_600:300_um")\n      )\n'
       '      (rule\n        (width 250)\n        (clearance 200)\n      )\n    )\n'
       '    (class power ' + ' '.join(power) + '\n      (circuit\n        (use_via "Via[0-1]_600:300_um")\n      )\n'
       '      (rule\n        (width 450)\n        (clearance 200)\n      )\n    )')
s = re.sub(r'\(class kicad_default .*?\n    \)', cls, s, count=1, flags=re.S)
s = re.sub(r'\(wiring\s*\)', '(wiring\n  )', s)
open(dst, 'w').write(s)
