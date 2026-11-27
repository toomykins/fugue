#!/usr/bin/env python3
"""Right-click this world's probe atoms in stock DreamSeeker (or in a Fugue client) and keep what the menu shows.
Windows only; needs BYOND and Pillow.

  menuprobe.py --out <dir> [--client fugue-qt.exe] [CASE...]

The world is prepared with prepare.py and compiled; one DreamDaemon and one client run every case. A case is a right
click with the real pointer (SetCursorPos, then a real right press and release) on one cell of the map. For each
case <out>/<case>.json keeps the menu as Windows has it (stock's popup is a Win32 menu: per entry its type, text, state,
whether it has a submenu, and its rectangle on the screen) and each popup on the screen is grabbed:
<case>-root.png at once, then each submenu opened from the keyboard, <case>-sub<N>.png, with the root grabbed again
while that entry is highlighted, <case>-root<N>.png. A case named in TIMED is grabbed three times 1.1 s apart while its
menu stays open, with the map cell beside each grab. For a Fugue client the entries come from its recorder's menu
marker instead (FUGUE_RECORD_DIR), and its popups are grabbed the same way.

The pointer is moved by the probe and put back at the end; the window is kept on top while the run lasts. Only the
processes started here are stopped.
"""
import argparse, ctypes, json, os, re, shutil, subprocess, sys, tempfile, time
from ctypes import wintypes
from pathlib import Path
from PIL import ImageGrab

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--byond', default='C:/Program Files (x86)/BYOND/bin')
ap.add_argument('--port', type=int, default=18930)
ap.add_argument('--at', default='300,200', help='screen position of the client window')
ap.add_argument('--client', default='', help='a fugue-qt executable instead of stock DreamSeeker')
ap.add_argument('--out', required=True)
ap.add_argument('--keep', action='store_true', help='keep the runtime directory (the world, logs, Fugue''s recording)')
ap.add_argument('cases', nargs='*')
a = ap.parse_args()

# name: (tile x, tile y[, pixel x, pixel y from the cell's top left, 16,16 when left out]), tiles 1-based with y up, as
# world.dm places them; the map shows 1..18 x 1..12. anim-b and anim-c are anim again, 1.5 s and 0.7 s later.
CASES = {
    'plain': (1, 11), 'dir-east': (3, 11), 'anim': (5, 11), 'overlay': (7, 11), 'underlay': (9, 11),
    'colored': (11, 11), 'alpha': (13, 11), 'scaled': (15, 11), 'turned': (17, 11),
    'big': (1, 9), 'big-top': (2, 10), 'small': (4, 9, 8, 24), 'iconless': (6, 9), 'nostate': (8, 9), 'offset': (10, 9),
    'offset-up': (11, 10, 4, 28), 'outlined': (12, 9), 'holder': (14, 9), 'imaged': (16, 9), 'changer': (18, 9),
    'improper': (1, 7), 'proper': (3, 7), 'amp': (5, 7), 'html': (7, 7), 'unicode': (9, 7), 'long': (11, 7),
    'twins': (13, 7), 'suffix': (15, 7), 'empty': (17, 7),
    'verbs': (2, 4), 'stack': (5, 4), 'mob': (8, 4), 'argonly': (11, 4), 'hud': (18, 1),
    'anim-b': (5, 11), 'anim-c': (5, 11),
}
TIMED = {'anim', 'anim-b', 'anim-c', 'changer'}
PAUSE = {'anim-b': 1.5, 'anim-c': 0.7}
cases = a.cases or list(CASES)
if any(c not in CASES for c in cases): raise SystemExit('unknown case; known: ' + ' '.join(CASES))

user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for name, args in {'SetWindowPos': [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT],
                   'GetWindowThreadProcessId': [wintypes.HWND, ctypes.c_void_p], 'GetClientRect': [wintypes.HWND, ctypes.c_void_p],
                   'GetWindowRect': [wintypes.HWND, ctypes.c_void_p], 'ClientToScreen': [wintypes.HWND, ctypes.c_void_p],
                   'SetForegroundWindow': [wintypes.HWND], 'GetClassNameW': [wintypes.HWND, ctypes.c_wchar_p, ctypes.c_int],
                   'GetSubMenu': [ctypes.c_void_p, ctypes.c_int], 'GetMenuItemCount': [ctypes.c_void_p],
                   'GetMenuItemInfoW': [ctypes.c_void_p, wintypes.UINT, wintypes.BOOL, ctypes.c_void_p],
                   'GetMenuItemRect': [wintypes.HWND, ctypes.c_void_p, wintypes.UINT, ctypes.c_void_p],
                   'SendMessageTimeoutW': [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT, wintypes.UINT, ctypes.c_void_p],
                   'PostMessageW': [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]}.items():
    getattr(user32, name).argtypes = args
user32.GetSubMenu.restype = ctypes.c_void_p


class MENUITEMINFOW(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.UINT), ('fMask', wintypes.UINT), ('fType', wintypes.UINT), ('fState', wintypes.UINT),
                ('wID', wintypes.UINT), ('hSubMenu', ctypes.c_void_p), ('hbmpChecked', ctypes.c_void_p),
                ('hbmpUnchecked', ctypes.c_void_p), ('dwItemData', ctypes.c_size_t), ('dwTypeData', ctypes.c_wchar_p),
                ('cch', wintypes.UINT), ('hbmpItem', ctypes.c_void_p)]


def windows(pid, wanted):
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128); user32.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and user32.IsWindowVisible(hwnd) and wanted(name.value): found.append(hwnd)
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return found


def main_window(pid):
    best = None
    for hwnd in windows(pid, lambda n: n != '#32768' and 'Popup' not in n and 'ToolTip' not in n):
        r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r))
        if not best or r.right * r.bottom > best[0]: best = (r.right * r.bottom, hwnd)
    return best[1] if best else None


def rect(hwnd):
    r = wintypes.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(r)); return [r.left, r.top, r.right, r.bottom]


def popups(pid):
    """The process's popup menu windows, in the order they opened (the root first)."""
    found = windows(pid, lambda n: n == '#32768' or 'Popup' in n)
    return sorted(found, key=lambda h: rect(h)[0])


def items(menu):
    out = []
    for i in range(max(0, user32.GetMenuItemCount(menu))):
        buf = ctypes.create_unicode_buffer(512)
        info = MENUITEMINFOW(cbSize=ctypes.sizeof(MENUITEMINFOW), fMask=0x1 | 0x2 | 0x4 | 0x20 | 0x40 | 0x80 | 0x100,
                             dwTypeData=ctypes.cast(buf, ctypes.c_wchar_p), cch=512)
        user32.GetMenuItemInfoW(menu, i, True, ctypes.byref(info))
        r = wintypes.RECT(); user32.GetMenuItemRect(None, menu, i, ctypes.byref(r))
        out.append({'type': info.fType, 'state': info.fState, 'text': buf.value, 'submenu': bool(info.hSubMenu),
                    'bitmap': info.hbmpItem or 0, 'data': bool(info.dwItemData),
                    'rect': [r.left, r.top, r.right, r.bottom], 'sub': info.hSubMenu})
    return out


def hmenu(hwnd):
    result = ctypes.c_size_t(0); user32.SendMessageTimeoutW(hwnd, 0x01E1, 0, 0, 2, 1000, ctypes.byref(result))
    return result.value


def press(hwnd, *names):
    code = {'down': (0x28, 0x150), 'right': (0x27, 0x14d), 'left': (0x25, 0x14b), 'escape': (0x1b, 0x01)}
    for name in names:
        vk, scan = code[name]
        user32.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16); time.sleep(.05)
        user32.PostMessageW(hwnd, 0x101, vk, 1 | scan << 16 | 3 << 30); time.sleep(.2)


def text(path): return path.read_text(errors='replace') if path.exists() else ''


def wait(done, failure, seconds=40):
    end = time.time() + seconds
    while not done():
        if time.time() > end: raise SystemExit(failure)
        time.sleep(.1)


out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='menuprobe-')); byond = Path(a.byond)
subprocess.run([sys.executable, str(HERE / 'prepare.py'), str(work)], check=True)
built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True)
if ' 0 errors' not in built.stdout: raise SystemExit(built.stdout)
saved = wintypes.POINT(); user32.GetCursorPos(ctypes.byref(saved))
procs = []; log = work / 'events.log'; record = work / 'record'
try:
    dd = open(work / 'dd.out', 'w')
    procs.append(subprocess.Popen([str(byond / 'dd.exe'), 'world.dmb', str(a.port), '-trusted', '-invisible'], cwd=work,
                                  stdout=dd, stderr=subprocess.STDOUT))
    wait(lambda: 'World opened' in text(work / 'dd.out'), 'DreamDaemon did not open the world')
    if a.client:
        env = dict(os.environ, QT_ENABLE_HIGHDPI_SCALING='0', FUGUE_RECORD_DIR=str(record))
        procs.append(subprocess.Popen([a.client, 'client', '--connect', f'127.0.0.1:{a.port}', '--state-dir',
                                       str(work / 'state')], cwd=work, env=env, stdout=open(work / 'client.log', 'w'),
                                      stderr=subprocess.STDOUT))
    else:
        procs.append(subprocess.Popen([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{a.port}'], cwd=work))
    pid = procs[1].pid
    wait(lambda: 'READY' in text(log), 'no READY in events.log')
    wait(lambda: main_window(pid), 'the client has no window')
    time.sleep(3)
    hwnd = main_window(pid); at = [int(v) for v in a.at.split(',')]
    user32.SetWindowPos(hwnd, wintypes.HWND(-1), at[0], at[1], 0, 0, 0x0001 | 0x0040)
    user32.SetForegroundWindow(hwnd); time.sleep(1)
    # The map: at the client area's top-left corner in stock. Fugue has a menu bar above it: the map starts at the first
    # row of the floor's dark grey, down the middle of the client area.
    origin = wintypes.POINT(0, 0); user32.ClientToScreen(hwnd, ctypes.byref(origin)); top = 0
    if a.client:
        column = ImageGrab.grab((origin.x + 288, origin.y, origin.x + 289, origin.y + 200), all_screens=True).convert('RGB')
        top = next((y for y in range(200) if max(abs(c - 16) for c in column.getpixel((0, y))) <= 2), 0)
    def screen(x, y): return origin.x + x, origin.y + top + y
    def markers():
        return [json.loads(l) for l in text(record / 'markers.jsonl').splitlines() if '"kind":"menu"' in l]
    def shot(box, name): ImageGrab.grab(tuple(box), all_screens=True).save(out / f'{name}.png')
    run = {'client': a.client or 'stock', 'dpi': user32.GetDpiForWindow(hwnd), 'window': rect(hwnd), 'map_top': top, 'cases': {}}
    for case in cases:
        tx, ty, dx, dy = (CASES[case] + (16, 16))[:4]
        px, py = (tx - 1) * 32 + dx, (12 - ty) * 32 + dy
        time.sleep(PAUSE.get(case, 0))
        user32.SetCursorPos(*screen(px, py)); time.sleep(.3)
        seen = len(markers())
        user32.mouse_event(8, 0, 0, 0, 0); time.sleep(.08); user32.mouse_event(16, 0, 0, 0, 0)
        end = time.time() + 2
        while time.time() < end and not popups(pid): time.sleep(.05)
        time.sleep(.4)
        found = popups(pid); result = {'tile': [tx, ty], 'pointer': list(screen(px, py))}
        if not found:
            result['menu'] = None
        else:
            # Off the menu, so that no entry is highlighted by the pointer: left of the window.
            user32.SetCursorPos(*screen(-60, py)); time.sleep(.3)
            root = found[0]; result['root_window'] = rect(root)
            if not a.client:
                result['menu'] = items(hmenu(root))
            else:
                time.sleep(.3); m = markers()
                result['marker'] = m[-1] if len(m) > seen else None
            shot(rect(root), f'{case}-root')
            if case in TIMED:
                cell = screen((tx - 1) * 32, (12 - ty) * 32)
                for k in range(3):
                    shot(rect(root), f'{case}-t{k}'); shot((cell[0], cell[1], cell[0] + 32, cell[1] + 32), f'{case}-cell{k}')
                    time.sleep(1.1)
            entries = result.get('menu') or [{}] * len((result.get('marker') or {}).get('menu', []))
            subs = []
            for i, entry in enumerate(entries):
                press(root, 'down' if i else 'down')
                if a.client or entry.get('submenu'):
                    press(root, 'right'); time.sleep(.3)
                    now = [h for h in popups(pid) if h != root]
                    if now:
                        sub = {'window': rect(now[0])}
                        if not a.client: sub['menu'] = items(entry['sub'])
                        shot(rect(now[0]), f'{case}-sub{i}'); shot(rect(root), f'{case}-root{i}')
                        subs.append(sub)
                        press(now[0], 'left'); time.sleep(.2)
                    else:
                        subs.append(None)
                else:
                    shot(rect(root), f'{case}-root{i}')
            result['subs'] = subs
            for e in result.get('menu') or []: e.pop('sub', None)
            for s in subs:
                for e in (s or {}).get('menu', []): e.pop('sub', None)
            press(root, 'escape', 'escape', 'escape'); time.sleep(.3)
            while popups(pid): press(popups(pid)[0], 'escape'); time.sleep(.2)
        run['cases'][case] = result
        (out / f'{case}.json').write_text(json.dumps(result, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
        print(case, json.dumps(result.get('menu') if not a.client else result.get('marker'), ensure_ascii=False)[:400])
    version = re.search(r'Version ([\d.]+)', text(work / 'dd.out'))
    run['byond'] = version.group(1) if version else '?'
    (out / 'run.json').write_text(json.dumps(run, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
finally:
    for button, up in ((1, 4), (2, 16), (4, 64)):
        if user32.GetAsyncKeyState(button) & 0x8000: user32.mouse_event(up, 0, 0, 0, 0)
    user32.SetCursorPos(saved.x, saved.y)
    for p in reversed(procs):
        if p.poll() is None: p.kill()
    for p in procs: p.wait()
    if a.keep: print('kept', work)
    else: shutil.rmtree(work, ignore_errors=True)
