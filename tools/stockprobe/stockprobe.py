#!/usr/bin/env python3
"""Run stock DreamSeeker and this client on one DreamDaemon and look at both. Windows only (DreamSeeker is); needs BYOND,
a built fugue-qt with its Qt on PATH, and Pillow. For the maintainer who adds a fixture case or re-records after a BYOND
update: the tests themselves replay what this writes and need none of it.

  stockprobe.py record <set>...           parity fixture: writes replay/<set>.txt (the frames this client received),
                                          replay/res/ and replay/<set>.png (stock's map) for the compositiongpu suite
  stockprobe.py pair <name>...            grabs both maps: <out>/<name>-stock.png, -client.png and -diff.png
  stockprobe.py click <name> ACTION...    does the same things in both windows (client-area pixels) and prints what
                                          the world logged for each, action by action
  stockprobe.py click @<script>           the same for many cases: each line of the script is "<name> ACTION..."
  stockprobe.py capture <name>           timed raw window images, capture bounds, geometry, wire and logs in --out/name

An ACTION is one of
  X,Y                    a left click
  right:X,Y              a right click; the right-click menu each client then shows is printed as MENU lines
  down:X,Y  up:X,Y       a left press and a left release on their own (rdown:, rup: for the right button)
  move:X,Y               a pointer move, with the buttons as the last action left them
  wait:SECONDS           give the world time, e.g. between a press and its release
  menu:ATOM/VERB         choose that verb from the open menu; menu: alone closes it. ATOM is the entry's name or its
                         place from the top (1, 2, ..): stock draws the names itself, so its menu reads back without them

--world is a fixture directory with a world.dm (and a prepare.py, if the one next to this file does not do) that logs to events.log ("READY <key>" at login; CLICK or
whatever else it logs is printed by click). Reading and clicking take neither the focus nor the pointer: windows are
read with PrintWindow and clicked with posted messages, and a window behind others or off the screen reads the same.
Starting a client does take the foreground, as any new window does: once per case, or once per server with --live.
Only the processes started here are stopped.

Many cases in one run: record, pair and click @script take any number of cases. The world is prepared and compiled
once, and a "{}" in --params stands for the case's name (--params set={} pair fx-plain fx-wave). A case that fails is
reported as FAILED on stderr, the others still run, and the exit status is 1. Unless asked otherwise every case still
gets its own DreamDaemon and clients and the whole --settle, exactly as a run of its own. Three options make a batch
faster. Each is off by default, because each relies on something about the world:

  --still SECONDS   observe once every map has been drawn and has then not changed for SECONDS, instead of always
                    waiting --settle, which stays the upper bound. Only for a scene that is static once drawn: one
                    that animates, or that the world changes later (spawn(), sleep()), is either never still, which
                    costs the whole --settle and is noted on stderr, or is still too soon and is observed early with
                    no note. 0.5 is the tested value. Drawn means that the window exists, that its map is not one
                    flat colour, and that this client's log says it laid out the world's skin (it shows a built-in
                    skin until then).
  --live            one DreamDaemon and one pair of clients for all the cases. The next case goes into a file, the
                    world reboots itself with it as world.params (live.dm, compiled into the world for this), and the
                    clients reconnect; with --still the maps must also have gone blank and come back. Only for a
                    world that reads its case in world/New() or later and starts the same after a reboot: not one
                    that reads world.params earlier (a global's initial value, New() of an atom placed on a map),
                    that keeps files it wrote, or whose scene depends on which client logs in first (either may).
                    The clients are not new either: each still has the pointer where the last case left it. As a
                    check, the last case is observed once more on a fresh server and must come out the same; if not,
                    it is reported FAILED and no case of the run should be trusted. A case the world does not come
                    back with is run on a fresh server instead, with a note. record and capture always start fresh.
  --jobs N          N servers at once on ports --port .. --port+N-1, each with its own clients and its own copy of
                    the compiled world under --work. The windows overlap, which does not matter to PrintWindow or to
                    posted clicks. Results are those of separate runs unless the moment matters: on a busy machine
                    a client logs in and draws later, so a scene that moves is observed at another point of its
                    motion, and --settle has less to spare.

--stock-only leaves this client out: pair writes <name>-stock.png alone and click prints what stock answered; no
--client is needed. --timings prints where each case's time went, on stderr.

--cursor also puts the real mouse pointer on each point before its messages are posted. Stock 516.1689 starts a drag
only where the real pointer has moved: without --cursor a press moved elsewhere sends no MouseDrag or MouseDrop and
ends in a Click on the atom it is released over. --cursor moves the user's pointer, and a window over the client's at
that point gets real moves too: use it with --stock-only. Hover is measured with hoverprobe.py, which keeps stock's
window on top and moves the real pointer over it (tests/data/hover-world).

Stock draws by the display scale its process started on (default-size maptext differs between 100 % and 150 %), so
replay/recorded.tsv keeps the scale of every recording; DreamSeeker's map also stays blank while the display is idle.
"""
import argparse, ctypes, datetime, hashlib, json, math, os, queue, re, shutil, subprocess, sys, tempfile, threading, time, uuid
from ctypes import wintypes
from pathlib import Path
from PIL import Image, ImageChops
from capture import capture_sequence, write_record

HERE = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--world', default=str(HERE.parent.parent / 'tests/data/stock-parity-world'), help='fixture directory')
ap.add_argument('--params', default='', help='world.params for pair and click, e.g. set=images-groups (record passes set=<set>)')
ap.add_argument('--client', default=os.environ.get('FUGUE_CLIENT', ''), help='fugue-qt executable (or FUGUE_CLIENT)')
ap.add_argument('--byond', default=os.environ.get('BYOND_BIN', 'C:/Program Files (x86)/BYOND/bin'), help='BYOND bin directory')
ap.add_argument('--work', default='', help='scratch directory (default: a temporary one)')
ap.add_argument('--out', default='.', help='where pair/capture writes its observations')
ap.add_argument('--port', type=int, default=18790)
ap.add_argument('--settle', type=float, default=6, help='seconds between the last login and the grab')
ap.add_argument('--map', default='576x384', help='size of the map at the top left of both windows')
ap.add_argument('--duration', type=float, default=5, help='capture duration in seconds')
ap.add_argument('--interval', type=float, default=.05, help='requested seconds between capture samples')
ap.add_argument('--trigger-file', default='', help='capture: create this fresh relative work file with a unique token')
ap.add_argument('--still', type=float, default=0, help='grab once the maps have not changed for this long; see above')
ap.add_argument('--live', action='store_true', help='one server and its clients for all cases; see above')
ap.add_argument('--jobs', type=int, default=1, help='servers to run at once, on --port and the ports after it')
ap.add_argument('--stock-only', action='store_true', help='pair and click without this client')
ap.add_argument('--cursor', action='store_true', help='click: move the real pointer to each point too; see above')
ap.add_argument('--timings', action='store_true', help="print each case's time breakdown on stderr")
ap.add_argument('command', choices=['record', 'pair', 'click', 'capture'])
ap.add_argument('args', nargs='+')
a = ap.parse_args()
if sys.platform != 'win32': sys.exit('stockprobe needs Windows: DreamSeeker runs nowhere else')
if a.stock_only and a.command not in ('pair', 'click'): sys.exit('--stock-only is for pair and click')
if not a.stock_only:
    if not a.client or not Path(a.client).exists(): sys.exit('--client (or FUGUE_CLIENT): the fugue-qt executable to run')
    a.client = str(Path(a.client).resolve())
if a.jobs < 1 or not math.isfinite(a.still) or a.still < 0: sys.exit('--jobs is at least 1 and --still is 0 or more seconds')
if a.command == 'capture' and (a.still or a.live or a.jobs > 1): sys.exit('capture takes none of --still, --live and --jobs')
if a.command == 'record' and a.live: sys.exit('record starts every set fresh: a recording is one client session')
world = Path(a.world).resolve(); byond = Path(a.byond).resolve(); mapsize = tuple(int(v) for v in a.map.split('x'))
work = Path(a.work).resolve() if a.work else Path(tempfile.mkdtemp(prefix='stockprobe-'))
if work == world or work in world.parents: sys.exit('--work must not contain the source fixture')
if a.command == 'capture':
    if work.exists() and any(work.iterdir()): sys.exit('capture requires a new or empty --work directory')
    if len(a.args) != 1 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', a.args[0]):
        sys.exit('capture requires one plain directory name')
    if not all(math.isfinite(v) and v > 0 for v in (a.duration, a.interval)):
        sys.exit('--duration and --interval must be finite positive seconds')
    capture_out = Path(a.out).resolve() / a.args[0]
    if capture_out.exists(): sys.exit('capture output already exists: ' + str(capture_out))
    if capture_out == work or work in capture_out.parents or capture_out in work.parents:
        sys.exit('capture output and runtime work directories must not contain each other')
    trigger_path = (work / a.trigger_file).resolve() if a.trigger_file else None
    if trigger_path and (Path(a.trigger_file).is_absolute() or work not in trigger_path.parents):
        sys.exit('--trigger-file must stay inside the runtime work directory')
    if trigger_path and trigger_path.exists(): sys.exit('trigger file already exists: ' + str(trigger_path))

user32 = ctypes.windll.user32; gdi32 = ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # sizes in real pixels
for f, res in ((user32.GetDC, wintypes.HDC), (gdi32.CreateCompatibleDC, wintypes.HDC), (gdi32.CreateCompatibleBitmap, wintypes.HBITMAP),
               (gdi32.SelectObject, wintypes.HGDIOBJ), (user32.ChildWindowFromPointEx, wintypes.HWND)):
    f.restype = res
user32.GetDC.argtypes = [wintypes.HWND]; user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]; user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.GetDpiForWindow.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.MapWindowPoints.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_void_p, wintypes.UINT]
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]; gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.ChildWindowFromPointEx.argtypes = [wintypes.HWND, wintypes.POINT, wintypes.UINT]
user32.GetClassNameW.argtypes = [wintypes.HWND, ctypes.c_wchar_p, ctypes.c_int]
user32.GetSubMenu.restype = ctypes.c_void_p; user32.GetSubMenu.argtypes = [ctypes.c_void_p, ctypes.c_int]
user32.GetMenuItemCount.argtypes = [ctypes.c_void_p]
user32.GetMenuStringW.argtypes = [ctypes.c_void_p, wintypes.UINT, ctypes.c_wchar_p, ctypes.c_int, wintypes.UINT]
user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT,
                                       wintypes.UINT, ctypes.c_void_p]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


class ProbeError(SystemExit):
    """One case could not be observed; the message says why. Uncaught, it ends the run with that message."""


def main_window(pid, wanted=True):
    """The largest visible top-level window of the process (none: an error, or None if not `wanted`)."""
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); found.append((r.right * r.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    if not found and wanted: raise ProbeError(f'process {pid} has no window')
    return max(found)[1] if found else None


def grab(pid, with_geometry=False):
    """The window's client area, drawn by the window itself (works behind other windows)."""
    hwnd = main_window(pid); r = wintypes.RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(r)): raise RuntimeError('GetClientRect failed')
    w, h = r.right, r.bottom
    if w <= 0 or h <= 0: raise RuntimeError('window has no client pixels')
    bounds = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(bounds)): raise RuntimeError('GetWindowRect failed')
    origin = wintypes.POINT(0, 0); user32.MapWindowPoints(hwnd, None, ctypes.byref(origin), 1)
    geometry = {'window_bounds_physical': [bounds.left, bounds.top, bounds.right, bounds.bottom],
                'client_origin_physical': [origin.x, origin.y], 'client_size_physical': [w, h],
                'dpi': user32.GetDpiForWindow(hwnd)}
    screen = user32.GetDC(None); dc = bmp = old = None
    try:
        if not screen: raise RuntimeError('GetDC failed')
        dc = gdi32.CreateCompatibleDC(screen); bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
        if not dc or not bmp: raise RuntimeError('capture bitmap allocation failed')
        old = gdi32.SelectObject(dc, bmp)
        if not old or old == ctypes.c_void_p(-1).value: raise RuntimeError('SelectObject failed')
        if not user32.PrintWindow(hwnd, dc, 3): raise RuntimeError('PrintWindow failed')
        gdi32.SelectObject(dc, old); old = None
        header = (ctypes.c_uint32 * 10)(40, w, ctypes.c_uint32(-h).value, 1 | 32 << 16, 0, 0, 0, 0, 0, 0)
        pixels = ctypes.create_string_buffer(w * h * 4)
        if gdi32.GetDIBits(dc, bmp, 0, h, pixels, header, 0) != h: raise RuntimeError('GetDIBits returned incomplete pixels')
        image = Image.frombuffer('RGBA', (w, h), pixels.raw, 'raw', 'BGRA', 0, 1).convert('RGB')
        return (image, geometry) if with_geometry else image
    finally:
        if old: gdi32.SelectObject(dc, old)
        if bmp: gdi32.DeleteObject(bmp)
        if dc: gdi32.DeleteDC(dc)
        if screen: user32.ReleaseDC(None, screen)


MOVE, LDOWN, LUP, RDOWN, RUP = 0x200, 0x201, 0x202, 0x204, 0x205
MOUSE = {'': [(MOVE, 0), (LDOWN, 1), (LUP, 0)], 'right': [(MOVE, 0), (RDOWN, 2), (RUP, 0)], 'move': [(MOVE, None)],
         'down': [(MOVE, 0), (LDOWN, 1)], 'up': [(MOVE, 1), (LUP, 0)], 'rdown': [(MOVE, 0), (RDOWN, 2)], 'rup': [(MOVE, 2), (RUP, 0)]}
held = {}   # pid -> the MK_ button bits its window was last left with


def mouse(pid, kind, x, y):
    hwnd = main_window(pid); p = wintypes.POINT(x, y); target = user32.ChildWindowFromPointEx(hwnd, p, 0) or hwnd
    if a.cursor:
        screen = wintypes.POINT(x, y); user32.ClientToScreen(hwnd, ctypes.byref(screen)); user32.SetCursorPos(screen.x, screen.y)
    user32.MapWindowPoints(hwnd, target, ctypes.byref(p), 1); where = (p.y << 16) | (p.x & 0xffff)
    for message, buttons in MOUSE[kind]:
        buttons = held.get(pid, 0) if buttons is None else buttons
        user32.PostMessageW(target, message, buttons, where); held[pid] = buttons; time.sleep(.08)


def windows(pid, wanted):
    """The process's visible top-level windows whose class name passes `wanted`, topmost first."""
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128); user32.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and user32.IsWindowVisible(hwnd) and wanted(name.value): found.append(hwnd)
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return found


def native_menu(pid):
    """Stock's open right-click menu, read from the menu itself: [(atom, [verb, ...])], or None when none is open. An
    entry with no submenu has no verbs."""
    popups = windows(pid, lambda name: name == '#32768')
    if not popups: return None
    handles = []
    for hwnd in popups:
        result = ctypes.c_size_t(0); user32.SendMessageTimeoutW(hwnd, 0x01E1, 0, 0, 2, 1000, ctypes.byref(result))   # MN_GETHMENU
        handles.append(result.value)
    subs = lambda menu: [user32.GetSubMenu(menu, i) for i in range(max(0, user32.GetMenuItemCount(menu)))]
    root = next((m for m in handles if not any(m in subs(o) for o in handles if o != m)), handles[0])
    def label(menu, i):
        buf = ctypes.create_unicode_buffer(256); user32.GetMenuStringW(menu, i, buf, 256, 0x400); return buf.value.replace('&', '')
    return [(label(root, i), [label(sub, j) for j in range(user32.GetMenuItemCount(sub))] if sub else [])
            for i, sub in enumerate(subs(root))]


def press(hwnd, *names):
    code = {'down': (0x28, 0x150), 'right': (0x27, 0x14d), 'enter': (0x0d, 0x1c), 'escape': (0x1b, 0x01)}
    for name in names:
        vk, scan = code[name]
        user32.PostMessageW(hwnd, 0x100, vk, 1 | scan << 16); time.sleep(.05)
        user32.PostMessageW(hwnd, 0x101, vk, 1 | scan << 16 | 3 << 30); time.sleep(.15)


def menu_lines(menu):
    if menu is None: return ['MENU none']
    return [f'MENU {atom}: ' + ' | '.join(verbs) for atom, verbs in menu] or ['MENU empty']


def text(path): return path.read_text(errors='replace') if path.exists() else ''


SKIN = 'skin loaded (from world)'   # this client's log, each time it has laid out the skin the world sent


def blank(image): return len(image.getcolors(2) or [0, 0]) < 2


class Run:
    """DreamDaemon with the world in `work`, stock DreamSeeker and this client, both logged in and settled."""
    def __init__(self, work, port, params, record=None):
        self.work = work; self.port = port; self.log = work / 'events.log'; self.log.unlink(missing_ok=True)
        self.procs = []; self.outputs = []; self.clock = [('', time.perf_counter())]; self.notes = []; self.since = 0
        self.client = None; self.serial = 1
        try:
            if a.live: (work / 'stockprobe-case.txt').write_bytes(f'1\n{params}'.encode())
            self.outputs.append(open(work / 'dd.out', 'w'))
            self.start([str(byond / 'dd.exe'), 'world.dmb', str(port), '-trusted', '-invisible', '-params', params],
                       stdout=self.outputs[-1])
            self.wait(lambda: 'World opened' in text(work / 'dd.out'), 'DreamDaemon did not open the world: ' + text(work / 'dd.out'))
            self.tick('server')
            self.stock = self.start([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{port}'])
            self.record = Path(record) if record else None
            if not a.stock_only:
                env = dict(os.environ, QT_ENABLE_HIGHDPI_SCALING='0')   # request unscaled client maps; capture records actual DPI
                if record: env['FUGUE_RECORD_DIR'] = str(record)
                self.outputs.append(open(work / 'client.log', 'w'))
                self.client = self.start([a.client, 'client', '--connect', f'127.0.0.1:{port}', '--state-dir', str(work / 'state')],
                                         env=env, stdout=self.outputs[-1])
            self.arrive(False)
            version = re.search(r'Version ([\d.]+)', text(work / 'dd.out')); self.byond = version.group(1) if version else '?'
        except BaseException:
            self.stop()
            raise
    def start(self, command, **more):
        self.procs.append(subprocess.Popen(command, cwd=self.work, stderr=subprocess.STDOUT, **more)); return self.procs[-1].pid
    def tick(self, label): self.clock.append((label, time.perf_counter()))
    def wait(self, done, failure, seconds=40, every=.1):
        end = time.time() + seconds
        while not done():
            if time.time() > end: self.stop(); raise ProbeError(failure)
            time.sleep(every)
    def switch(self, params):
        """The next case on the same server and clients (--live): live.dm reboots the world with these params and
        logs LIVE <serial> from world/New(), before anyone can have logged in again."""
        self.clock = [('', time.perf_counter())]; self.notes = []; self.serial += 1
        skins = text(self.work / 'client.log').count(SKIN); begin = len(text(self.log)); mark = f'LIVE {self.serial}\n'
        case = self.work / 'stockprobe-case.txt'; fresh = self.work / 'stockprobe-case.new'
        fresh.write_bytes(f'{self.serial}\n{params}'.encode())
        for attempt in range(50):   # the world may have the file open, for an instant
            try: os.replace(fresh, case); break
            except PermissionError: time.sleep(.02)
        else: raise ProbeError('the case file could not be replaced')
        self.wait(lambda: mark in text(self.log)[begin:], 'the world did not come back with the case (LIVE in events.log)', 10, .02)
        self.since = text(self.log).index(mark, begin) + len(mark); self.tick('reboot')
        self.arrive(True, skins)
    def pids(self): return [self.stock] + ([self.client] if self.client else [])
    def arrive(self, switched, skins=0):
        """Waits for every client's READY, and then --settle; or, with --still, until the scene has been drawn and
        has stayed the same for that long. After a switch drawn also means blanked since: both clients empty their
        map when the world goes down, and until then they show the previous case."""
        limit = time.time() + 40; ready = None; blanked = set(); last = None; since = 0; box = (0, 0) + mapsize
        while True:
            now = time.time()
            if ready is None:
                if text(self.log)[self.since:].count('READY') >= len(self.pids()): ready = now; self.tick('logins')
                elif now > limit: self.stop(); raise ProbeError('the world did not log READY for both clients (events.log)')
            if ready is not None and not a.still: time.sleep(a.settle); break
            if a.still and (switched or ready is not None):   # a window that is still starting answers slowly
                seen = [self.glimpse(pid, box) for pid in self.pids()]
                blanked.update(pid for pid, image in zip(self.pids(), seen) if image and blank(image))
                drawn = (ready is not None and all(image and not blank(image) for image in seen)
                         and (not switched or len(blanked) == len(seen))
                         and (not self.client or text(self.work / 'client.log').count(SKIN) > skins))
                key = [image.tobytes() for image in seen] if drawn else None
                if key is None or key != last: last = key; since = now
                elif now - since >= a.still: break
                if ready is not None and now - ready >= a.settle:
                    self.notes.append(f'not still for {a.still:g} s within --settle: observed {a.settle:g} s after the logins instead')
                    break
            time.sleep(.05 if a.still else .1)
        self.tick('settle')
    def glimpse(self, pid, box):
        """The map as it is now, or None while the process has no window to show (a client changing its skin)."""
        try: return grab(pid).crop(box) if main_window(pid, False) else None
        except ProbeError: return None
    def maps(self):
        box = (0, 0) + mapsize; stock = grab(self.stock).crop(box); client = grab(self.client).crop(box) if self.client else None
        self.tick('grab')
        if blank(stock): self.stop(); raise ProbeError("stock's map is blank: is the display idle or the window minimised?")
        return stock, client
    def lines(self): return text(self.log).splitlines()
    def markers(self):
        return [l for l in text(self.record / 'markers.jsonl').splitlines() if '"kind":"menu"' in l] if self.record else []
    def menu(self, pid, seen):
        """The right-click menu `pid` shows after a right click: stock's own popup, and for this client what its
        recorder says it offered (it shows a popup only for a menu with entries)."""
        end = time.time() + 2
        while time.time() < end:
            if pid == self.stock:
                found = native_menu(pid)
                if found is not None: return found
            elif len(self.markers()) > seen:
                return [(entry['atom'], entry['verbs']) for entry in json.loads(self.markers()[-1])['menu']]
            time.sleep(.1)
        return None
    def choose(self, pid, menu, wanted):
        """Walks the open menu with the keyboard: down to the atom, into its verbs, down to the verb."""
        popups = windows(pid, (lambda name: name == '#32768') if pid == self.stock else (lambda name: 'Popup' in name))
        if not popups: return 'PICK no menu is open'
        if not wanted: press(popups[0], 'escape', 'escape'); return None
        atom, _, verb = wanted.partition('/')
        at = next((i for i, entry in enumerate(menu or [])
                   if atom in (entry[0], str(i + 1)) and (verb in entry[1] or not entry[1])), None)
        if at is None: press(popups[0], 'escape', 'escape'); return f'PICK {wanted} is not in the menu'
        verbs = menu[at][1]
        press(popups[0], *['down'] * (at + 1), *(['right'] + ['down'] * verbs.index(verb) if verbs else []), 'enter')
        return None
    def act(self, pid, actions):
        """Does the actions in one window; what the world logged and the menus shown, per action."""
        said = []; menu = None
        for action in actions:
            before = len(self.lines()); seen = len(self.markers()); extra = []
            kind, _, value = action.rpartition(':')
            if kind == 'wait': time.sleep(float(value))
            elif kind == 'menu':
                note = self.choose(pid, menu, value); extra = [note] if note else []; menu = None; time.sleep(.8)
            else:
                mouse(pid, kind, *(int(v) for v in value.split(','))); time.sleep(.5)
                if kind in ('right', 'rup'): menu = self.menu(pid, seen); extra = menu_lines(menu)
            said.append(self.lines()[before:] + extra)
        if menu: self.choose(pid, menu, '')
        return said
    def stop(self):
        for p in reversed(self.procs):
            if p.poll() is None: p.kill()
        for p in self.procs: p.wait()
        for output in self.outputs: output.close()
        self.procs = []; self.outputs = []
    def __enter__(self): return self
    def __exit__(self, *_): self.stop()


def differing(x, y):
    """Pixels with a channel more than 8 levels apart, as the tests count them, and a picture of where."""
    mask = ImageChops.difference(x, y).point(lambda v: 255 if v > 8 else 0).convert('L').point(lambda v: 255 if v else 0)
    return mask.histogram()[255], mask


def fingerprint(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def file_version(path):
    version = ctypes.windll.version
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.c_void_p, ctypes.c_void_p]
    size = version.GetFileVersionInfoSizeW(str(path), None)
    if not size: return None
    data = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, data): return None
    value = ctypes.c_void_p(); length = wintypes.UINT()
    if not version.VerQueryValueW(data, '\\', ctypes.byref(value), ctypes.byref(length)) or length.value < 52: return None
    fields = ctypes.cast(value, ctypes.POINTER(wintypes.DWORD))
    if fields[0] != 0xFEEF04BD: return None
    return '.'.join(str(v) for v in (fields[2] >> 16, fields[2] & 65535, fields[3] >> 16, fields[3] & 65535))


def remove_runtime(path):
    path = Path(path).resolve()
    if work not in path.parents: raise ValueError('cleanup must stay within the runtime work directory')
    shutil.rmtree(path, ignore_errors=True)


if a.command == 'capture':
    capture_out.mkdir(parents=True)
    capture_record = {'complete': False, 'world': str(world), 'params': a.params,
                      'requested_map_crop_physical': [0, 0, *mapsize], 'settle_seconds': a.settle,
                      'state_policy': 'stock existing global configuration; fresh Fugue state directory',
                      'readiness': 'two server READY lines, then settle; not a presentation acknowledgement',
                      'capture_method': 'PrintWindow client area; not a desktop-compositor observation'}
    write_record(capture_out, capture_record)

own = world / 'prepare.py'   # a world with none is prepared by the one next to this file, which says what that gives it
try:
    subprocess.run([sys.executable, str(own), str(work)] if own.exists() else [sys.executable, str(HERE / 'prepare.py'), str(world), str(work)],
                   check=True, stdout=subprocess.DEVNULL)
    if a.live:   # last, so that its world/New() runs first and the world's own sees the case's params
        shutil.copyfile(HERE / 'live.dm', work / 'stockprobe-live.dm')
        with open(work / 'world.dme', 'a') as project: project.write('\n#include "stockprobe-live.dm"\n')
    built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True, timeout=60)
    if a.command == 'capture': (capture_out / 'compile.log').write_text(built.stdout + built.stderr, encoding='utf-8')
    if not (work / 'world.dmb').exists() or ' 0 errors' not in built.stdout:
        raise RuntimeError('the world did not compile:\n' + built.stdout)
except BaseException as error:
    if a.command == 'capture':
        capture_record['error'] = f'{type(error).__name__}: {error}'
        write_record(capture_out, capture_record)
    raise
dpi = user32.GetDpiForSystem(); failed = 0

FORM = re.compile(r'(?:(?:right|down|up|rdown|rup|move):)?\d+,\d+$|wait:\d+(?:\.\d+)?$|menu:.*$')


def planned():
    """The cases asked for: (name, world.params, actions)."""
    if a.command == 'record': return [(st, f'set={st}', []) for st in a.args]
    if a.command == 'pair': return [(name, a.params.replace('{}', name), []) for name in a.args]
    if a.args[0].startswith('@'):
        if len(a.args) > 1: sys.exit('click @<script> takes its actions from the script')
        rows = [line.split() for line in Path(a.args[0][1:]).read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')]
    else: rows = [a.args]
    for row in rows:
        wrong = [action for action in row[1:] if not FORM.match(action)]
        if len(row) < 2 or wrong: sys.exit('click <name> ACTION...' + (': not an action: ' + ' '.join(wrong) if wrong else ''))
    return [(row[0], a.params.replace('{}', row[0]), row[1:]) for row in rows]


def job_work(job):
    """The runtime directory of one of --jobs servers: the first has the prepared one, the others a copy each."""
    if not job: return work
    mine = work / f'job-{job}'; remove_runtime(mine)
    shutil.copytree(work, mine, ignore=lambda folder, names: [n for n in names if Path(folder) == work and (
        n.startswith(('job-', 'frames-')) or n in ('state', 'events.log', 'dd.out', 'client.log'))])
    return mine


def observe(run, name, actions):
    """What one case shows, taken while its server is up; emit() writes it out."""
    if a.command == 'click':
        names = [l.split()[1] for l in run.lines() if l.startswith('READY')]
        said = [run.act(pid, actions) for pid in run.pids()]; run.tick('actions')
        return names, said
    return run.maps() + (run.byond,)


def compared(result):
    """What two observations of one case must have in common."""
    if a.command != 'click': return [image and image.tobytes() for image in result[:2]]
    names, said = result
    return [[[re.sub('|'.join(map(re.escape, names)) or '$^', '<key>', l) for l in lines] for lines in window] for window in said]


proved = threading.Lock()   # taken by the server that compares a --live case with a fresh one


def serve(job, mine, todo, done, runs):
    """One server's share of the cases. A case that fails does not take the others with it. With --live the last
    case of the batch that came from a reboot is observed again on a fresh server: a world that cannot be run live
    answers differently there, and then every case of the batch is in doubt."""
    run = None
    while True:
        try: index, (name, params, actions) = todo.get_nowait()
        except queue.Empty: break
        frames = mine / (f'frames-{name}' if a.command == 'record' else 'frames-click') if a.command != 'pair' and not a.stock_only else None
        notes = []; began = time.perf_counter(); clock = []; rebooted = False; doubt = None
        def fresh():
            if frames: remove_runtime(frames)
            runs[job] = Run(mine, a.port + job, params, frames); return runs[job]
        try:
            if run:
                try: run.switch(params); rebooted = True
                except ProbeError as error:
                    notes.append(f'{error}: started fresh instead'); run.stop(); run = None
            run = run or fresh()
            result = observe(run, name, actions); clock = run.clock; notes += run.notes
            if rebooted and todo.empty() and proved.acquire(blocking=False):
                run.stop(); run = fresh()
                if compared(observe(run, name, actions)) != compared(result):
                    doubt = ('a fresh server answers differently: this world, or a scene that moves, cannot be run with '
                             '--live, and every case of this run is in doubt')
                clock = clock + [('fresh check', time.perf_counter())]
        except (ProbeError, Exception) as error:
            result = error if isinstance(error, ProbeError) else ProbeError(f'{type(error).__name__}: {error}')
            if run: run.stop(); run = None
        if run and not a.live: run.stop(); run.tick('stop'); run = None
        times = [(label, at - before) for (_, before), (label, at) in zip(clock, clock[1:])] + [('all', time.perf_counter() - began)]
        done.put((index, result, frames, notes, times, doubt))
    if run: run.stop()


def emit(name, actions, result, frames):
    """Writes and prints one case, as a run of its own would."""
    if a.command == 'click':
        names, said = result
        plain = lambda l: re.sub('|'.join(map(re.escape, names)) or '$^', '<key>', l)
        if a.stock_only:
            print(f'{name}: {len(actions)} actions; {sum(map(len, said[0]))} lines for stock (what the world logged, and '
                  'the probe\'s own MENU and PICK lines)')
            for action, stock in zip(actions, said[0]):
                print(f'  {action}')
                for line in stock: print('    stock  ' + plain(line))
            return
        print(f'{name}: {len(actions)} actions; {sum(map(len, said[0]))} lines for stock and {sum(map(len, said[1]))} '
              'for the client (what the world logged, and the probe\'s own MENU and PICK lines)')
        for action, stock, client in zip(actions, *said):
            print(f'  {action}')
            for i in range(max(len(stock), len(client))):
                s, c = (plain(part[i]) if i < len(part) else '-' for part in (stock, client))
                print(('    same   ' if s == c else '    DIFFER ') + s + ('' if s == c else '\n             ' + c))
        return
    stock, client, version = result
    if a.command == 'record':
        off, _ = differing(stock, client)
        stock.save(replay / f'{name}.png', optimize=True)
        (replay / f'{name}.txt').write_bytes((frames / 'frames.txt').read_bytes().replace(b'\r\n', b'\n'))
        for resource in (frames / 'res').iterdir(): shutil.copyfile(resource, replay / 'res' / resource.name)
        rows[name] = f'{version}\t{dpi}\t{datetime.date.today()}'
        print(f'{name}: recorded; the live client differs from stock in {off} pixels')
        return
    stock.save(out / f'{name}-stock.png')
    if a.stock_only: print(f'{name}: stock grabbed (display at {dpi} dpi)'); return
    off, mask = differing(stock, client)
    client.save(out / f'{name}-client.png')
    Image.composite(Image.new('RGB', stock.size, (255, 0, 255)), stock.point(lambda v: v // 3), mask).save(out / f'{name}-diff.png')
    box = mask.getbbox()
    print(f'{name}: {off} pixels differ by more than 8 levels' + (f', within {box}' if box else '') + f' (display at {dpi} dpi)')


if a.command == 'capture':
    try:
        source = capture_out / 'world'; source.mkdir()
        prepared = sorted(p for p in work.rglob('*') if p.is_file() and p.suffix in ('.dm', '.dme', '.dmf', '.dmb', '.rsc'))
        capture_record['world_sha256'] = {p.relative_to(work).as_posix(): fingerprint(p) for p in prepared}
        for path in prepared:
            if path.suffix in ('.dm', '.dme', '.dmf'):
                target = source / path.relative_to(work); target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        capture_record['executables_sha256'] = {str(path): fingerprint(path)
            for path in (byond / 'dreamseeker.exe', byond / 'dd.exe', Path(a.client))}
        capture_record['executable_file_versions'] = {str(path): file_version(path)
            for path in (byond / 'dreamseeker.exe', byond / 'dd.exe', Path(a.client))}
        with Run(work, a.port, a.params, capture_out / 'wire') as run:
            capture_record['dreamdaemon_version'] = run.byond
            capture_record['system_dpi'] = dpi
            def trigger():
                token = uuid.uuid4().hex
                trigger_path.parent.mkdir(parents=True, exist_ok=True)
                with trigger_path.open('x', encoding='ascii') as signal: signal.write(token)
                return {'file': a.trigger_file, 'token': token}
            pids = {'stock': run.stock, 'client': run.client}
            capture_sequence(capture_out, pids, lambda subject: grab(pids[subject], with_geometry=True),
                             a.duration, a.interval, capture_record, trigger if trigger_path else None)
    except BaseException as error:
        capture_record['complete'] = False
        capture_record['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        for name in ('events.log', 'dd.out', 'client.log'):
            path = work / name
            if path.exists(): shutil.copyfile(path, capture_out / name)
        if trigger_path and trigger_path.is_file(): shutil.copyfile(trigger_path, capture_out / 'trigger.txt')
        write_record(capture_out, capture_record)
    print(f'{a.args[0]}: retained {len(capture_record["frames"])} captures in {capture_out}')
else:
    cases = planned()
    if a.command == 'record':
        replay = world / 'replay'; (replay / 'res').mkdir(parents=True, exist_ok=True); recorded = replay / 'recorded.tsv'
        rows = dict(line.split('\t', 1) for line in text(recorded).splitlines() if '\t' in line and not line.startswith('#'))
    if a.command == 'pair': out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    todo = queue.Queue(); done = queue.Queue(); runs = {}; waiting = {}
    for case in enumerate(cases): todo.put(case)
    servers = [threading.Thread(target=serve, args=(job, job_work(job), todo, done, runs), daemon=True)   # copied before any server runs
               for job in reversed(range(min(a.jobs, len(cases))))]
    try:
        for server in servers: server.start()
        for index, (name, params, actions) in enumerate(cases):   # written out in the order asked for
            while index not in waiting:
                try: finished = done.get(timeout=.2)
                except queue.Empty:
                    if not any(server.is_alive() for server in servers) and done.empty(): sys.exit('a server thread ended without its cases')
                    continue
                waiting[finished[0]] = finished[1:]
            result, frames, notes, times, doubt = waiting.pop(index)
            for note in notes: print(f'{name}: {note}', file=sys.stderr)
            if doubt: failed += 1; print(f'{name}: FAILED: {doubt}', file=sys.stderr)
            if a.timings: print(f'{name}: ' + '  '.join(f'{label} {seconds:.2f}' for label, seconds in times), file=sys.stderr)
            if isinstance(result, ProbeError):
                failed += 1; print(f'{name}: FAILED: {result}', file=sys.stderr)
            else: emit(name, actions, result, frames)
            sys.stdout.flush()
        for server in servers: server.join()
    finally:
        for run in list(runs.values()): run.stop()
    if a.command == 'record':
        recorded.write_text('# set\tBYOND\tdisplay dpi\trecorded\n' + ''.join(f'{k}\t{v}\n' for k, v in sorted(rows.items())), newline='\n')
if not a.work:
    if work.parent != Path(tempfile.gettempdir()).resolve() or not work.name.startswith('stockprobe-'):
        raise ValueError('temporary runtime cleanup escaped its owned directory')
    shutil.rmtree(work, ignore_errors=True)
if failed: sys.exit(1)
