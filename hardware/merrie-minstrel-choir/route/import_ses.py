import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1])
ses = open(sys.argv[2]).read()
for a in ('A1-B12', 'A4-B9', 'B1-A12', 'B4-A9'):
    ses = ses.replace(a.replace('-', '_'), a)
tmp = sys.argv[2] + '.kicad.ses'
open(tmp, 'w').write(ses)
print('import', pcbnew.ImportSpecctraSES(b, tmp))
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())
b.BuildConnectivity()
pcbnew.SaveBoard(sys.argv[3], b)
