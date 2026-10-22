"""Greedy GND track removal, keeping a removal only if kicad-cli DRC shows no new
unconnected items, starved thermals or connection-width warnings.
usage: gnd_cleanup_drc.py in.kicad_pcb out.kicad_pcb"""
import json, os, shutil, subprocess, sys, tempfile
import pcbnew
src, dst = sys.argv[1], sys.argv[2]
tmpd = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(dst)))
tmp = os.path.join(tmpd, 't.kicad_pcb')
shutil.copy(os.path.splitext(src)[0] + '.kicad_pro', os.path.join(tmpd, 't.kicad_pro'))
b = pcbnew.LoadBoard(src)
filler = pcbnew.ZONE_FILLER(b)
WATCH = ('starved_thermal', 'connection_width')

def score():
    filler.Fill(b.Zones())
    pcbnew.SaveBoard(tmp, b)
    out = os.path.join(tmpd, 'd.json')
    subprocess.run(['kicad-cli', 'pcb', 'drc', '--format', 'json', '--severity-all', '-o', out, tmp],
                   capture_output=True)
    d = json.load(open(out))
    return (len(d['unconnected_items']),) + tuple(sum(v['type'] == w for v in d['violations']) for w in WATCH)

base = score()
print('base', base, flush=True)
segs = sorted((t for t in b.GetTracks() if t.GetNetname() == 'GND' and t.GetClass() == 'PCB_TRACK'),
              key=lambda t: -t.GetLength())
removed = 0
for t in segs:
    b.Remove(t)
    s = score()
    if s[0] == 0 and all(x <= y for x, y in zip(s[1:], base[1:])):
        removed += 1
    else:
        b.Add(t)
print('removed', removed, 'of', len(segs), 'final', score(), flush=True)
pcbnew.SaveBoard(dst, b)
shutil.rmtree(tmpd)
