#!/usr/bin/env python3
"""Move the real mouse pointer over stock DreamSeeker's map and log what the world's mouse procs saw. Windows only.

  hoverprobe.py --world tests/data/hover-world --out <file.json> [--switches CLIENT_ENTER,..] [--params lag=10] SCRIPT

The world is prepared with its own prepare.py <dir> [switches] and compiled once; one DreamDaemon and one stock client
run every case of SCRIPT in turn. Each line of SCRIPT is "<case> ACTION...", and an ACTION is one of

  X,Y            a real pointer move to that MAP pixel (SetCursorPos: the window under it gets a real move). Points
                 outside the map are allowed (with a wider skin, its other controls are reached this way): -40,100 is left of the window, 100,-60 above its client area
  post:X,Y       a WM_MOUSEMOVE posted to the window at that point; the real pointer stays where it is
  sweep:X0,Y0,X1,Y1,N,MS   N real moves from the first point to the second, MS milliseconds apart
  psweep:X0,Y0,X1,Y1,N,MS  the same N moves posted to the window (Windows merges no posted moves)
  down up rdown rup mdown mup   a real press or release of the left, right or middle button where the pointer is
  tap            a real left press and release at once
  key:K kdown:K kup:K      a real key pressed and released, pressed, or released: K is ctrl, shift, alt, esc or f1..f9
  shot:NAME      a grab of the screen around the window, NAME.png beside --out (to find where things are drawn)
  wait:SECONDS   give the world time

An action is followed by --settle seconds before its lines are read; with a "!" after it (tap!) the next one follows
at once.

--wire puts a relay between stock and DreamDaemon (DreamDaemon on --port + 1) and keeps, per action, the plaintext
bodies of the input commands (c2s 0x0002) and stat tab selections (0x006b) stock sent while it lasted, in hex. They
are read with the session key both ends derive from the handshake (net/handshake.h, net/cipher.h); the login frame
is never read or kept.

Before each case the pointer is put outside the window and the probe waits a second, so that a case starts with the
pointer off the map. The stock window is moved to --at and kept topmost while the run lasts, so that real moves reach
it. events.log lines the world wrote during each action are kept per action, with the probe's own clock (seconds since
the case began) on the action. The user's pointer is put back at the end. Only processes started here are stopped. --timer1 holds a 1 ms system timer
resolution (timeBeginPeriod) in this process while the run lasts.
"""
import argparse, ctypes, json, re, shutil, subprocess, sys, tempfile, time
from ctypes import wintypes
from pathlib import Path

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--world', required=True)
ap.add_argument('--byond', default='C:/Program Files (x86)/BYOND/bin')
ap.add_argument('--port', type=int, default=18870)
ap.add_argument('--params', default='')
ap.add_argument('--switches', default='')
ap.add_argument('--at', default='300,200', help='screen position of the stock window')
ap.add_argument('--settle', type=float, default=.35, help='seconds after each action before its lines are read')
ap.add_argument('--out', required=True)
ap.add_argument('--timer1', action='store_true', help='hold timeBeginPeriod(1) while the run lasts')
ap.add_argument('--wire', action='store_true', help='relay stock to DreamDaemon and keep its input commands')
ap.add_argument('--replay', help='with --wire: write what DreamDaemon sent stock before the first case here')
ap.add_argument('script')
a = ap.parse_args()


class Relay:
    """Stock -> 127.0.0.1:port -> DreamDaemon on port + 1, bytes passed on untouched. The c2s stream is read as it
    goes: the handshake [type 0001][len][body] (proto, field2, field3 little-endian), whose base key decrypts the
    server's first frame; the nonce in it gives the session key (net/handshake.cpp loginKey). Every later c2s frame is
    [seq][type][len][body], the body under the session key with a trailing checksum (net/cipher.cpp). The login frame
    (0x001a) is skipped unread. The server's frames are read under the same key."""

    KEEP = (0x0002, 0x006b)

    def __init__(self, port):
        import socket, threading
        self.socket, self.threading = socket, threading
        self.frames = []   # (perf_counter, type, plaintext hex)
        self.s2c = []      # (perf_counter, type, length on the wire, plaintext hex), resource frames left out
        self.bad = 0
        self.key = None
        self.base = None
        self.listener = socket.socket(); self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(('127.0.0.1', port)); self.listener.listen(4)
        self.port = port
        threading.Thread(target=self.accept, daemon=True).start()

    @staticmethod
    def decrypt(body, key):
        out = bytearray(); s = 0
        for c in body[:-1]:
            p = (c - s - ((key >> (s & 31)) & 0xff)) & 0xff
            out.append(p); s = (s + p) & 0xff
        return bytes(out), bool(body) and body[-1] == s

    @staticmethod
    def login_key(base, nonce):
        words = [int.from_bytes(nonce[i:i + 4], 'little') for i in range(0, len(nonce) // 4 * 4, 4)]
        i = 0
        while i < len(words) and (((words[i] + 0x71bd632f) & 0xffffffff) >> 1) & 0x02004000: i += 1
        return base if i + 1 >= len(words) else (base + words[i + 1]) & 0xffffffff

    def accept(self):
        while True:
            try: client, _ = self.listener.accept()
            except OSError: return
            server = self.socket.create_connection(('127.0.0.1', self.port + 1))
            self.threading.Thread(target=self.pump, args=(client, server, True), daemon=True).start()
            self.threading.Thread(target=self.pump, args=(server, client, False), daemon=True).start()

    def pump(self, src, dst, c2s):
        buf = bytearray(); first = True
        while True:
            try: chunk = src.recv(65536)
            except OSError: chunk = b''
            if not chunk:
                try: dst.shutdown(self.socket.SHUT_WR)
                except OSError: pass
                return
            if not c2s: dst.sendall(chunk)
            buf += chunk
            while True:
                if c2s and first:
                    if len(buf) < 4 or len(buf) < 4 + int.from_bytes(buf[2:4], 'big'): break
                    n = int.from_bytes(buf[2:4], 'big'); body = bytes(buf[4:4 + n]); del buf[:4 + n]; first = False
                    proto, field2, field3 = (int.from_bytes(body[i:i + 4], 'little') for i in (0, 4, 8))
                    self.base = (field3 + (field2 << 16) + proto) & 0xffffffff
                    continue
                if not c2s:
                    if self.base is None or len(buf) < 4: break
                    kind, n, hdr = int.from_bytes(buf[0:2], 'big'), int.from_bytes(buf[2:4], 'big'), 4
                    if kind == 0x00a0 and n >= 6:
                        if len(buf) < 10: break
                        kind, n, hdr = int.from_bytes(buf[4:6], 'big'), int.from_bytes(buf[6:10], 'big'), 10
                    if len(buf) < hdr + n: break
                    body = bytes(buf[hdr:hdr + n]); del buf[:hdr + n]
                    if kind == 0x009f and self.key is not None: continue
                    plain, ok = self.decrypt(body, self.key if self.key is not None else self.base) if body else (b'', True)
                    if not ok: self.bad += 1
                    elif self.key is None: self.key = self.login_key(self.base, plain[11:])
                    else: self.s2c.append((time.perf_counter(), kind, len(body), plain.hex()))
                    continue
                hdr = 6
                if len(buf) < hdr: break
                kind, n = int.from_bytes(buf[2:4], 'big'), int.from_bytes(buf[4:6], 'big')
                if kind == 0x00a0 and n >= 6:
                    if len(buf) < 12: break
                    kind, n, hdr = int.from_bytes(buf[6:8], 'big'), int.from_bytes(buf[8:12], 'big'), 12
                if len(buf) < hdr + n: break
                body = bytes(buf[hdr:hdr + n]); del buf[:hdr + n]
                if kind in self.KEEP and self.key is not None:
                    plain, ok = self.decrypt(body, self.key)
                    if ok: self.frames.append((time.perf_counter(), kind, plain.hex()))
                    else: self.bad += 1
            if c2s: dst.sendall(chunk)   # after the handshake is read, so that the server's reply finds its key

    def close(self):
        try: self.listener.close()
        except OSError: pass

user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
user32.ChildWindowFromPointEx.restype = wintypes.HWND
user32.ChildWindowFromPointEx.argtypes = [wintypes.HWND, wintypes.POINT, wintypes.UINT]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.MapWindowPoints.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_void_p, wintypes.UINT]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.c_void_p]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]


def main_window(pid):
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); found.append((r.right * r.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return max(found)[1] if found else None


def text(path): return path.read_text(errors='replace') if path.exists() else ''


def wait(done, failure, seconds=40):
    end = time.time() + seconds
    while not done():
        if time.time() > end: raise SystemExit(failure)
        time.sleep(.1)


world = Path(a.world).resolve(); byond = Path(a.byond)
work = Path(tempfile.mkdtemp(prefix='hoverprobe-'))
switches = [s for s in a.switches.split(',') if s]
subprocess.run([sys.executable, str(world / 'prepare.py'), str(work)] + switches, check=True)
built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True)
if ' 0 errors' not in built.stdout: raise SystemExit(built.stdout)
cases = [line.split() for line in Path(a.script).read_text().splitlines() if line.strip() and not line.startswith('#')]
FORM = re.compile(r'(-?\d+,-?\d+|post:\d+,\d+|p?sweep:(-?\d+,){5}\d+|down|up|rdown|rup|mdown|mup|tap|(key|kdown|kup):(ctrl|shift|alt|esc|f[1-9]))!?$'
                  r'|wait:\d+(\.\d+)?$|shot:\w+$')
KEYS = {'ctrl': 0x11, 'shift': 0x10, 'alt': 0x12, 'esc': 0x1b, **{f'f{i}': 0x6f + i for i in range(1, 10)}}
for case in cases:
    bad = [x for x in case[1:] if not FORM.match(x)]
    if bad: raise SystemExit('not an action: ' + ' '.join(bad))

saved = wintypes.POINT(); user32.GetCursorPos(ctypes.byref(saved))
def resolution():   # the system timer's current resolution, in ms
    low, high, now = wintypes.ULONG(), wintypes.ULONG(), wintypes.ULONG()
    ctypes.windll.ntdll.NtQueryTimerResolution(ctypes.byref(low), ctypes.byref(high), ctypes.byref(now))
    return now.value / 10000
timer = {'before': resolution()}
if a.timer1:
    ctypes.windll.winmm.timeBeginPeriod(1); timer['held'] = resolution()
procs = []; log = work / 'events.log'; results = []; relay = None
at = [int(v) for v in a.at.split(',')]
try:
    out = open(work / 'dd.out', 'w')
    if a.wire: relay = Relay(a.port)
    procs.append(subprocess.Popen([str(byond / 'dd.exe'), 'world.dmb', str(a.port + 1 if a.wire else a.port), '-trusted', '-invisible', '-params',
                                   a.params], cwd=work, stdout=out, stderr=subprocess.STDOUT))
    wait(lambda: 'World opened' in text(work / 'dd.out'), 'DreamDaemon did not open the world')
    procs.append(subprocess.Popen([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{a.port}'], cwd=work))
    wait(lambda: 'READY' in text(log), 'no READY in events.log')
    wait(lambda: main_window(procs[1].pid), 'stock has no window')
    time.sleep(2)
    hwnd = main_window(procs[1].pid)
    user32.SetWindowPos(hwnd, wintypes.HWND(-1), at[0], at[1], 0, 0, 0x0001 | 0x0040)   # topmost, no size change
    user32.SetForegroundWindow(hwnd)
    time.sleep(1)
    timer['running'] = resolution()

    def screen(x, y):
        p = wintypes.POINT(x, y); user32.ClientToScreen(hwnd, ctypes.byref(p)); return p.x, p.y

    def lines(): return text(log).splitlines()

    def post(x, y):
        p = wintypes.POINT(x, y); target = user32.ChildWindowFromPointEx(hwnd, p, 0) or hwnd
        user32.MapWindowPoints(hwnd, target, ctypes.byref(p), 1)
        user32.PostMessageW(target, 0x200, 0, (p.y << 16) | (p.x & 0xffff))

    def act(action):
        kind, _, value = action.rstrip('!').partition(':')
        if not value: kind, value = ('move', kind) if ',' in kind else (kind, '')
        if kind == 'move':
            user32.SetCursorPos(*screen(*map(int, value.split(','))))
        elif kind == 'post':
            post(*map(int, value.split(',')))
        elif kind in ('sweep', 'psweep'):
            x0, y0, x1, y1, n, ms = map(int, value.split(','))
            start = time.perf_counter()
            for i in range(1, n + 1):
                while time.perf_counter() < start + (i - 1) * ms / 1000: pass   # sleep() is too coarse for this
                x, y = round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n)
                if kind == 'sweep': user32.SetCursorPos(*screen(x, y))
                else: post(x, y)
            return time.perf_counter() - start
        elif kind in ('down', 'up', 'rdown', 'rup', 'mdown', 'mup', 'tap'):
            user32.mouse_event({'down': 2, 'up': 4, 'rdown': 8, 'rup': 16, 'mdown': 32, 'mup': 64, 'tap': 6}[kind],
                               0, 0, 0, 0)
        elif kind in ('key', 'kdown', 'kup'):
            if kind != 'kup': user32.keybd_event(KEYS[value], 0, 0, 0)
            if kind != 'kdown': user32.keybd_event(KEYS[value], 0, 2, 0)
        elif kind == 'shot':
            from PIL import ImageGrab
            r = wintypes.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(r))
            ImageGrab.grab((r.left, r.top, r.right + 200, r.bottom + 200), all_screens=True).save(
                Path(a.out).with_name(value + '.png'))
        elif kind == 'wait':
            time.sleep(float(value))

    if relay and a.replay:
        started = time.perf_counter()
        Path(a.replay).write_text(f'=== hoverprobe --wire: decrypted s2c frames DreamDaemon sent stock DreamSeeker before '
                                  f'the first case (0x009f resource frames are not recorded) ===\n' + ''.join(
            f'0x{k:04x} len={n} {h}\n' for t, k, n, h in list(relay.s2c) if t < started), newline='\n')
    for case in cases:
        user32.SetCursorPos(*screen(-80, 112)); time.sleep(1)
        begin = time.perf_counter(); said = []
        for action in case[1:]:
            before = len(lines()); clock = time.perf_counter() - begin
            sent = len(relay.frames) if relay else 0
            took = act(action)
            if not action.startswith(('wait:', 'shot:')) and not action.endswith('!'): time.sleep(a.settle)
            said.append({'action': action, 'at': round(clock, 3), 'logged': lines()[before:]})
            if relay: said[-1]['sent'] = [(f'{k:04x} ' if k != 2 else '') + h for _, k, h in relay.frames[sent:]]
            if took is not None: said[-1]['took'] = round(took, 4)
        results.append({'case': case[0], 'actions': said})
        print(case[0])
        for row in said:
            print(f"  {row['at']:7.3f} {row['action']}" + (f"  (took {row['took']:.4f} s)" if 'took' in row else ''))
            for line in row['logged']: print('      ' + line)
            for body in row.get('sent', []): print('      > ' + body)
    version = re.search(r'Version ([\d.]+)', text(work / 'dd.out'))
    Path(a.out).write_text(json.dumps({'stock': version.group(1) if version else '?', 'switches': switches,
                                       'params': a.params, 'timer_ms': timer, 'cases': results}, indent=1) + '\n')
    if relay and relay.bad: print(f'{relay.bad} frames did not decrypt')
finally:
    for key in KEYS.values():   # nothing is left held for the user
        if user32.GetAsyncKeyState(key) & 0x8000: user32.keybd_event(key, 0, 2, 0)
    for button, up in ((1, 4), (2, 16), (4, 64)):
        if user32.GetAsyncKeyState(button) & 0x8000: user32.mouse_event(up, 0, 0, 0, 0)
    user32.SetCursorPos(saved.x, saved.y)
    if a.timer1: ctypes.windll.winmm.timeEndPeriod(1)
    for p in reversed(procs):
        if p.poll() is None: p.kill()
    for p in procs: p.wait()
    if relay: relay.close()
    shutil.rmtree(work, ignore_errors=True)
