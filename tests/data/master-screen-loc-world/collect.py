"""Stock-only probe driver: DreamDaemon + DreamSeeker, phase file handshake, PrintWindow captures."""
import ctypes, json, subprocess, sys, time
from ctypes import wintypes
from pathlib import Path
from PIL import Image

run = Path(sys.argv[1]).resolve(); out = Path(sys.argv[2]).resolve(); out.mkdir(parents=True, exist_ok=True)
phases = sys.argv[3:]
byond = Path('C:/Program Files (x86)/BYOND/bin'); port = 18811
if phases and phases[0].startswith('--port='): port = int(phases.pop(0)[7:])
user32 = ctypes.windll.user32; gdi32 = ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, res in ((user32.GetDC, wintypes.HDC), (gdi32.CreateCompatibleDC, wintypes.HDC),
               (gdi32.CreateCompatibleBitmap, wintypes.HBITMAP), (gdi32.SelectObject, wintypes.HGDIOBJ)):
    f.restype = res
user32.GetDC.argtypes = [wintypes.HWND]; user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]; gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]; user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]

def main_window(pid):
    found = []
    def each(hwnd, _):
        owner = wintypes.DWORD(); user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); found.append((r.right * r.bottom, hwnd))
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return max(found)[1] if found else None

def grab(pid):
    hwnd = main_window(pid); r = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(r)); w, h = r.right, r.bottom
    screen = user32.GetDC(None); dc = gdi32.CreateCompatibleDC(screen); bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
    old = gdi32.SelectObject(dc, bmp)
    try:
        if not user32.PrintWindow(hwnd, dc, 3): raise RuntimeError('PrintWindow failed')
        gdi32.SelectObject(dc, old); old = None
        header = (ctypes.c_uint32 * 10)(40, w, ctypes.c_uint32(-h).value, 1 | 32 << 16, 0, 0, 0, 0, 0, 0)
        pixels = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(dc, bmp, 0, h, pixels, header, 0)
        return Image.frombuffer('RGBA', (w, h), pixels.raw, 'raw', 'BGRA', 0, 1).convert('RGB')
    finally:
        if old: gdi32.SelectObject(dc, old)
        gdi32.DeleteObject(bmp); gdi32.DeleteDC(dc); user32.ReleaseDC(None, screen)

def text(p): return p.read_text(errors='replace') if p.exists() else ''
def wait(cond, what, t=40):
    end = time.time() + t
    while not cond():
        if time.time() > end: raise RuntimeError('timeout: ' + what)
        time.sleep(.1)

log = run / 'events.log'; log.unlink(missing_ok=True); (run / 'phase.txt').write_text('none')
procs = []; ddout = open(run / 'dd.out', 'w')
try:
    procs.append(subprocess.Popen([str(byond / 'dd.exe'), 'world.dmb', str(port), '-safe', '-invisible'], cwd=run,
                                  stdout=ddout, stderr=subprocess.STDOUT))
    wait(lambda: 'World opened' in text(run / 'dd.out') or 'running' in text(run / 'dd.out').lower(), 'dd open')
    time.sleep(1)
    procs.append(subprocess.Popen([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{port}'], cwd=run))
    wait(lambda: 'READY none' in text(log), 'login READY')
    time.sleep(3)
    record = {'version': next((l for l in text(run / 'dd.out').splitlines() if 'Version' in l), '?'), 'phases': []}
    for i, phase in enumerate(phases):
        (run / 'phase.txt').write_text(phase)
        wait(lambda: text(log).count(f'READY {phase}\n') >= 1, 'phase ' + phase)
        time.sleep(1.5)
        a = grab(procs[1].pid); time.sleep(.3); b = grab(procs[1].pid)
        name = f'{i:02d}.png'; a.save(out / name)
        record['phases'].append({'phase': phase, 'image': name, 'stable': list(a.getdata()) == list(b.getdata())})
        # restore a baseline between phases so each master is created fresh
        (run / 'phase.txt').write_text('none'); wait(lambda: text(log).count('READY none\n') >= i + 2, 'none'); time.sleep(.5)
    (out / 'record.json').write_text(json.dumps(record, indent=1))
    print(json.dumps(record, indent=1))
finally:
    for p in reversed(procs):
        if p.poll() is None: p.kill()
    for p in procs: p.wait()
    ddout.close()
