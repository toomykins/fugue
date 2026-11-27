"""Select a reproducible native end-to-end sample; the C++ oracle covers all admitted rows."""
import json
import random
import sys
from pathlib import Path

corpus=json.loads(Path(__file__).with_name('stock-positions.json').read_text())
rng=random.Random(0x5161688)
selected=[]
for group,count in [('corner-axes',64),('corner-shorthand',64),('even-view',64),('endpoint-modes',48),('large-pixels',16)]:
    pool=[i for i,c in enumerate(corpus) if c['group']==group]
    selected.extend(sorted(rng.sample(pool,count)))
Path(sys.argv[1]).write_text(json.dumps([
    {'text':corpus[i]['text'],'view':corpus[i]['view'],'group':corpus[i]['group'],'corpusIndex':i+1}
    for i in selected],indent=2)+'\n')
