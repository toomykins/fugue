"""Stage this world in a runtime directory: the icons local-relay-world generates, white squares (w.dmi: "w" a whole
tile, "ws" 16x16 at the top left), then this world and its skin."""
import shutil, subprocess, sys
from pathlib import Path
from PIL import Image, ImageDraw, PngImagePlugin
source = Path(__file__).resolve().parent
target = Path(sys.argv[-1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
subprocess.run([sys.executable, '-B', str(source.parent / 'local-relay-world' / 'prepare.py'), str(target)], check=True)
im = Image.new('RGBA', (64, 32)); d = ImageDraw.Draw(im)
d.rectangle((0, 0, 31, 31), fill=(255, 255, 255, 255)); d.rectangle((32, 0, 47, 15), fill=(255, 255, 255, 255))
info = PngImagePlugin.PngInfo()
info.add_text('Description', '# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n' +
              ''.join(f'state = "{s}"\n\tdirs = 1\n\tframes = 1\n' for s in ('w', 'ws')) + '# END DMI\n')
im.save(target / 'w.dmi', format='PNG', pnginfo=info)
for name in ('world.dm', 'world.dmf'):
    shutil.copy2(source / name, target / name)
