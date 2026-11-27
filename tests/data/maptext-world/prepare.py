"""Stage synthetic maptext inputs; requires Pillow. Never writes outside destination."""
from pathlib import Path
import shutil
import sys
from PIL import Image

source = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
for name in ("world.dm", "world.dme", "skin.dmf"):
    shutil.copyfile(source / name, output / name)
for name, color in (("black", "black"), ("red", "red")):
    Image.new("RGBA", (32, 32), color).save(output / f"{name}.png")
