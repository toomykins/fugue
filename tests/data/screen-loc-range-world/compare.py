"""Compare native Fugue marker origins to independently captured stock observations."""
import json
import sys
from pathlib import Path

corpus=json.loads(Path(__file__).with_name('stock-positions.json').read_text())
path=Path(sys.argv[1]); rows=json.loads(path.read_text())['rows']
failures=[]; exclusions=[]; passed=0
for row in rows:
    expected=corpus[row['corpusIndex']-1]
    assert row['text']==expected['text'] and row['view']==expected['view'] and row['stable']
    w,h=map(int,row['view'].split('x')); x0,y0,x1,y1=row['map']
    sx=(x1-x0)/(w*32); sy=(y1-y0)/(h*32)
    points=[]
    for x,y in row['dots']:
        px=(x-x0)/sx-16; py=(y1-y)/sy-16
        assert max(abs(px-round(px)),abs(py-round(py)))<.4, (row['index'],px,py)
        points.append([round(px),round(py)])
    points.sort()
    if 'excluded' in expected:
        exclusions.append({'index':row['index'],'text':row['text'],'reason':expected['excluded'],
                           'stock':expected['positions'],'fugue':points})
    elif points!=expected['positions']:
        failures.append({'index':row['index'],'text':row['text'],'stock':expected['positions'],'fugue':points})
    else:
        passed+=1
report={'passed':passed,'failed':len(failures),'excluded_not_parity_passes':exclusions,'failures':failures}
path.with_suffix('.comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'Native comparison: {passed} passed, {len(failures)} failed, {len(exclusions)} known out-of-subset cases (not parity passes)')
for failure in failures[:5]: print(failure)
raise SystemExit(bool(failures))
