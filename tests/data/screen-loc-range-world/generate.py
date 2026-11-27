"""Generate range strings, NOT expected answers; stock screenshots supply the oracle."""
import itertools
import json
import sys
from pathlib import Path


def cases():
    result = []
    def add(text, group, view='7x5'):
        result.append({'text':text,'view':view,'group':group})
    horizontal = ['WEST','LEFT','EAST','RIGHT']
    vertical = ['SOUTH','BOTTOM','NORTH','TOP']
    points = [f'{a},{b}' for x,y in itertools.product(horizontal,vertical) for a,b in [(x,y),(y,x)]]
    for a,b in itertools.product(points,repeat=2):
        add(f'{a} TO {b}','corner-axes')
    corners = ['SOUTHWEST','SOUTHEAST','NORTHWEST','NORTHEAST','BOTTOMLEFT','BOTTOMRIGHT','TOPLEFT','TOPRIGHT']
    for a,b in itertools.product(corners,repeat=2):
        add(f'{a} TO {b}','corner-shorthand')
    for c,p in itertools.product(corners,['WEST,SOUTH','EAST,SOUTH','WEST,NORTH','EAST,NORTH']):
        add(f'{c} TO {p}','mixed-shorthand')
        add(f'{p} TO {c}','mixed-shorthand')
    even_points = [f'{x},{y}' for x,y in itertools.product(horizontal,vertical)]
    for a,b in itertools.product(even_points,repeat=2):
        add(f'{a} TO {b}','even-view','8x6')
    for axis,(low,far,anchor) in enumerate([('WEST','EAST','RIGHT'),('SOUTH','NORTH','TOP')]):
        terms = [low,low+'+1',far+'-1',far,anchor+'-2',anchor+'-1',anchor,anchor+'+1']
        def point(term):
            return f'{term},SOUTH' if axis == 0 else f'WEST,{term}'
        for a,b in itertools.product(terms,repeat=2):
            add(f'{point(a)} TO {point(b)}','tile-offsets')
        for view,offset,(a,b) in itertools.product(['7x5','8x6'],[-7,0,5,7],
                [(low,anchor),(anchor+'-1',anchor),(anchor,far),(low+'+1',far)]):
            add(f'{point(a+":"+str(offset))} to {point(b+":"+str(offset))}','pixel-offsets',view)
    assert len(result) == 1600
    return result


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(json.dumps(cases(),indent=2)+'\n')
