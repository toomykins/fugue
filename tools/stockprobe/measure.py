"""Measure named flat colours in retained capture images without consulting Fugue output."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops


def colour_bounds(image, colour):
    """Exact-colour bounds in captured pixels, right/bottom exclusive; None if absent."""
    difference = ImageChops.difference(image.convert('RGB'), Image.new('RGB', image.size, colour))
    red, green, blue = difference.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(lambda value: 255 if value == 0 else 0)
    return mask.getbbox()


def measure(capture_path, colours, subject='stock', crop=None):
    capture_path = Path(capture_path)
    record = json.loads(capture_path.read_text(encoding='utf-8'))
    frames = []
    for frame in record['frames']:
        if frame['subject'] != subject or 'image' not in frame:
            continue
        path = capture_path.parent / frame['image']
        with Image.open(path) as image:
            if crop is not None:
                x, y, width, height = crop
                if min(x, y) < 0 or min(width, height) <= 0 or x + width > image.width or y + height > image.height:
                    raise ValueError(f'Crop lies outside {path}')
                image = image.crop((x, y, x + width, y + height))
            frames.append({'image': frame['image'], 'index': frame['index'], 'size': image.size,
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'bounds': {name: colour_bounds(image, tuple(colour)) for name, colour in colours.items()}})
    if not frames:
        raise ValueError(f'No captured images for {subject}')
    return {'capture_sha256': hashlib.sha256(capture_path.read_bytes()).hexdigest(), 'subject': subject, 'crop': crop,
            'units': 'captured pixels relative to crop; exact RGB bounds; right/bottom exclusive', 'colours': colours, 'frames': frames}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--subject', default='stock')
    parser.add_argument('--crop', type=int, nargs=4, metavar=('X', 'Y', 'W', 'H'), help='measure within this captured-pixel rectangle')
    parser.add_argument('--colour', action='append', required=True, help='name=R,G,B (repeatable)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    colours = {}
    for specification in args.colour:
        name, separator, value = specification.partition('=')
        try:
            colour = [int(channel) for channel in value.split(',')]
        except ValueError:
            parser.error('Colours must use name=R,G,B')
        if not name or not separator or len(colour) != 3 or any(not 0 <= channel <= 255 for channel in colour):
            parser.error('Colours must use name=R,G,B with channels from 0 to 255')
        if name in colours:
            parser.error(f'Duplicate colour name: {name}')
        colours[name] = colour
    args.output.write_text(json.dumps(measure(args.capture, colours, args.subject, args.crop), indent=2) + '\n', encoding='utf-8')
