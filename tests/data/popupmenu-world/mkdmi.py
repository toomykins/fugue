# passmouse contrast-world icons, 32x32: z.dmi. "st"/"nst"/"img": a coloured top-left quadrant, the rest transparent,
# so mouse_opacity 1 and 2 answer differently; "rock" a full square; "floor" a full square.
import sys
from PIL import Image, PngImagePlugin
W = sys.argv[1]
def sq(c, inset=0):
    return lambda x, y: c if inset <= x < 32 - inset and inset <= y < 32 - inset else None
def quad(c):
    return lambda x, y: c if x < 16 and y < 16 else None
states = [("floor", [sq((40, 40, 40, 255))]), ("mob", [sq((0, 0, 200, 255), 8)]), ("rock", [sq((0, 160, 0, 255), 4)]),
          ("st", [quad((255, 0, 0, 255))]), ("nst", [quad((0, 128, 255, 255))]), ("img", [quad((255, 255, 0, 255))])]
desc = "# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n"
tiles = []
for n, fr in states:
    desc += 'state = "%s"\n\tdirs = 1\n\tframes = %d\n' % (n, len(fr))
    tiles += fr
desc += "# END DMI\n"
img = Image.new("RGBA", (32 * len(tiles), 32), (0, 0, 0, 0))
for i, f in enumerate(tiles):
    for y in range(32):
        for x in range(32):
            c = f(x, y)
            if c: img.putpixel((i * 32 + x, y), c)
info = PngImagePlugin.PngInfo(); info.add_text("Description", desc, zip=True)
img.save(W + "/z.dmi", format="PNG", pnginfo=info)
