"""Materialize the small relay-gate fixture in a disposable build directory."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw

out = Path(sys.argv[1]).resolve()
assert 'build' in out.parts
out.mkdir(parents=True, exist_ok=True)
src = Path(__file__).resolve().parent
(out / 'world.dm').write_text((src / 'world.dm').read_text().expandtabs(4))
shutil.copyfile(src.parent / 'plane-source-center-world' / 'skin.dmf', out / 'world.dmf')
(out / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
Image.new('RGBA', (32,32), (20,70,90,255)).save(out / 'floor.png')
im = Image.new('RGBA', (32,32))
d = ImageDraw.Draw(im)
d.rectangle((2,2,25,27), fill=(240,30,30,255))
d.rectangle((2,2,9,8), fill=(255,220,20,255))
d.rectangle((12,12,19,19), fill=(0,0,0,0))
im.save(out / 'ink.png')
for y in range(2,28):
    for x in range(22,26):
        r,g,b,_ = im.getpixel((x,y))
        im.putpixel((x,y),(r,g,b,1 if y<=12 else 128))
im.save(out / 'partial.png')
im = Image.new('RGBA', (32,32))
ImageDraw.Draw(im).rectangle((0,0,3,3), fill='white')
im.save(out / 'ack.png')
im = Image.new('RGBA', (32,32))
ImageDraw.Draw(im).rectangle((0,0,15,15), fill=(30,220,240,255))
im.save(out / 'child.png')
# Flat fractional-color patches discriminate material/capture quantization.
colors = [(240,30,70,1),(73,149,211,3),(123,61,199,7),(91,173,47,17),
          (37,119,203,63),(211,53,107,128),(33,181,229,193),(177,93,41,255)]
im = Image.new('RGBA', (32,32))
for y in range(32):
    for x in range(32): im.putpixel((x,y), colors[(y//16)*4+x//8])
im.save(out / 'precision.png')
im = Image.new('RGBA', (32,32))
ImageDraw.Draw(im).rectangle((0,0,15,15), fill=(230,210,30,255))
im.save(out / 'yellow.png')
