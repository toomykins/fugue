"""Disambiguate range reversal with edge-aligned vs tile-coordinate endpoints."""
import itertools
import json
import sys
from pathlib import Path

result = []
for axis,(low,low_alias,far,far_alias) in enumerate([('WEST','LEFT','EAST','RIGHT'),('SOUTH','BOTTOM','NORTH','TOP')]):
    terms = [low,low+'+1',low+'+2',low_alias,low_alias+'+1',low_alias+'+2',
             far,far+'-1',far_alias+'-1',far_alias,far_alias+'+1',far_alias+'+2']
    def point(term):
        return f'{term},SOUTH' if axis == 0 else f'WEST,{term}'
    for a,b in itertools.product(terms,repeat=2):
        result.append({'text':f'{point(a)} TO {point(b)}','view':'7x5','group':'endpoint-modes'})
assert len(result) == 288
Path(sys.argv[1]).write_text(json.dumps(result,indent=2)+'\n')
