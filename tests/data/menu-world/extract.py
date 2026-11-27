#!/usr/bin/env python3
"""stock.tsv and icons/ from menuprobe.py's output for stock: extract.py <probe out dir>... (requires Pillow).

Per case and menu entry: the atom's name as the grab reads (stock draws that row itself, so Windows has no text for it:
the names below were read off the grabs by eye), the verbs as Windows has them (the text of each item of the entry's
submenu, in order, with Win32's "&&" read as the "&" it shows), and the 16x16 picture beside the name, cut from the
grab of the menu before any entry was highlighted. The picture is what the screen showed, over the menu's background
(240,240,240); "-" when the entry shows none. A later directory's case replaces an earlier one's.
"""
import json, sys
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
# Read off <case>-root.png, top to bottom. html is drawn whole but its end ("e") is under the submenu arrow.
NAMES = {
    'plain': ['plain'], 'dir-east': ['dir-east'], 'anim': ['anim'], 'anim-b': ['anim'], 'anim-c': ['anim'],
    'overlay': ['overlay'], 'underlay': ['underlay'], 'colored': ['colored'], 'alpha': ['alpha'], 'scaled': ['scaled'],
    'turned': ['turned'], 'big': ['big'], 'big-top': ['big'], 'small': ['small'], 'iconless': ['iconless'],
    'nostate': ['nostate'], 'offset': ['offset'], 'offset-up': ['offset'], 'outlined': ['outlined'],
    'holder': ['member', 'holder'], 'imaged': ['imaged'], 'changer': ['changer'], 'improper': ['the Rock'],
    'proper': ['rock'], 'amp': ['Tom & Jerry'], 'html': ['<b>bold</b> name'], 'unicode': ['café ünï'],
    'long': ['a very long name that goes on and on past any menu width you would expect to see in a game'],
    'twins': ['twin', 'twin'], 'suffix': ['suffixed'], 'verbs': ['verbs'],
    'stack': ['second', 'first', 'third', 'verbed-turf'], 'mob': ['dummy-mob'], 'argonly': ['argonly'], 'hud': ['hud'],
}
BACKGROUND = (240, 240, 240)
cases = {}
for directory in map(Path, sys.argv[1:]):
    for path in sorted(directory.glob('*.json')):
        if path.name == 'run.json': continue
        cases[path.stem] = (directory, json.loads(path.read_text(encoding='utf-8')))
(HERE / 'icons').mkdir(exist_ok=True)
lines = ['# Stock DreamSeeker 516.1689 x DreamDaemon 516.1689, 2026-11-26, display at 150 % (144 dpi): what the map\'s',
         '# right-click menu showed for each case of menuprobe.py (see README.md), entry by entry, top to bottom.',
         '# case\tentry\tatom\tverbs (| between)\ticon']
for case, (directory, result) in cases.items():
    if result.get('menu') is None:
        lines.append(f'{case}\t-\t-\t-\t-'); continue
    root = Image.open(directory / f'{case}-root.png').convert('RGB')
    names = NAMES[case]
    if len(names) != len(result['menu']): raise SystemExit(f'{case}: {len(result["menu"])} entries, {len(names)} names')
    for i, entry in enumerate(result['menu']):
        sub = result['subs'][i] or {}
        verbs = [item['text'].replace('&&', '&') for item in sub.get('menu', [])]
        icon = root.crop((5, 10 + 30 * i, 21, 26 + 30 * i))
        name = '-'
        if any(icon.getpixel((x, y)) != BACKGROUND for x in range(16) for y in range(16)):
            name = f'icons/{case}-{i}.png'; icon.save(HERE / name)
        lines.append(f'{case}\t{i}\t{names[i]}\t{"|".join(verbs)}\t{name}')
(HERE / 'stock.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
