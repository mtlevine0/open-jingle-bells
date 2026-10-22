import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
ses = open(sys.argv[2]).read()
for a in ('A1-B12', 'A4-B9', 'B1-A12', 'B4-A9'):
    ses = ses.replace(a.replace('-', '_'), a)
tmp = sys.argv[2] + '.kicad.ses'
open(tmp, 'w').write(ses)
print('import', pcbnew.ImportSpecctraSES(b, tmp))
# Freerouting leaves zero-length stubs at the pads of fixed (pre-routed) nets
for t in list(b.GetTracks()):
    if t.GetClass() == 'PCB_TRACK' and t.GetLength() < pcbnew.FromMM(0.01):
        b.Delete(t)
# ... and can neck a track below the board minimum (0.1524 mm)
for t in b.GetTracks():
    if t.GetClass() == 'PCB_TRACK' and t.GetWidth() < pcbnew.FromMM(0.1524):
        t.SetWidth(pcbnew.FromMM(0.187))
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())
b.BuildConnectivity()
pcbnew.SaveBoard(sys.argv[3], b)
