"""Convert stable stock marker pixels to integer logical origins; no parser/model oracle."""
import argparse
import hashlib
import json
from pathlib import Path

# Explicitly retained observations outside this increment's existing range subset.
EXCLUDED = {
    'RIGHT+1:-192,SOUTH TO WEST:-192,SOUTH': 'reversed pixel-offset range',
    'WEST,TOP+1:-128 TO WEST,SOUTH:-128': 'reversed pixel-offset range',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output',type=Path)
    parser.add_argument('observations',type=Path,nargs='+')
    args = parser.parse_args()
    cases=[]; provenance=[]; max_error=0
    for path in args.observations:
        data=json.loads(path.read_text())
        provenance.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                           'probe':data['probe'],'cases':len(data['rows'])})
        for row in data['rows']:
            assert row['stable'] and not row.get('pixelX') and not row.get('pixelY')
            w,h=map(int,row['view'].split('x'))
            x0,y0,x1,y1=row['map']; sx=(x1-x0)/(w*32); sy=(y1-y0)/(h*32)
            assert abs(sx-sy)<.02, ('unexpected MAP aspect/expansion',row['index'])
            positions=[]
            for x,y in row['dots']:
                px=(x-x0)/sx-16; py=(y1-y)/sy-16
                error=max(abs(px-round(px)),abs(py-round(py)))
                max_error=max(max_error,error)
                assert error<.25, ('noninteger or clipped marker',row['index'],px,py)
                positions.append([round(px),round(py)])
            case={'text':row['text'],'view':row['view'],'group':row.get('group','large-pixels'),
                  'positions':sorted(positions)}
            if row['text'] in EXCLUDED:case['excluded']=EXCLUDED[row['text']]
            cases.append(case)
    args.output.write_text('[\n'+',\n'.join(json.dumps(c,separators=(',',':')) for c in cases)+'\n]\n')
    args.output.with_suffix('.provenance.json').write_text(json.dumps({
        'stock_version':'516.1688','tile_size':32,'marker_rect':[12,12,8,8],
        'max_origin_rounding_error':max_error,'sources':provenance},indent=2)+'\n')
    print(f'Exported {len(cases)} stock observations; {sum("excluded" in c for c in cases)} explicitly outside subset; max rounding error {max_error:.4f}')


if __name__ == '__main__':
    main()
