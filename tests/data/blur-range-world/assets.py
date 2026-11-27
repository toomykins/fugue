from pathlib import Path
from PIL import Image, ImageDraw
p = Path(__file__).resolve().parent
Image.new('RGBA', (32,32), 'black').save(p/'black.png')
for shape in ('square','edge'):
    im=Image.new('RGBA',(992,672))
    d=ImageDraw.Draw(im)
    d.rectangle((368,208,623,463) if shape=='square' else (0,0,495,671),fill='white')
    im.save(p/f'{shape}.png')
im=Image.new('RGBA',(992,672))
d=ImageDraw.Draw(im)
for x,y,c in ((0,0,'#ff00ff'),(988,0,'#00ffff'),(0,668,'#ff8000'),(988,668,'#00ff00')):
    d.rectangle((x,y,x+3,y+3),fill=c)
im.save(p/'markers.png')
