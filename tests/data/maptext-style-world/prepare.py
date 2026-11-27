"""Stage bounded CSS-class/outline maptext probes; requires Pillow."""
from pathlib import Path
import shutil
import sys
from PIL import Image

source = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
for name in ("world.dm", "world.dme", "skin.dmf"):
    shutil.copyfile(source / name, output / name)
Image.new("RGBA", (32, 32), (70, 70, 70, 255)).save(output / "gray.png")
