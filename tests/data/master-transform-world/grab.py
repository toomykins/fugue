"""Grab what stock draws for the fx-* cases (stock only; requires Pillow and a BYOND install on Windows).

usage: grab.py <workdir> <port> <case> [<case> ...]
Runs prepare.py into <workdir>, then for each case starts DreamDaemon and DreamSeeker, waits for READY, grabs the
client window, keeps the 224 px map as filters/<case>.png and stops the two processes it started.
"""
import ctypes, subprocess, sys, time
from ctypes import wintypes
from pathlib import Path
from PIL import Image

source = Path(__file__).resolve().parent
work, port = Path(sys.argv[1]).resolve(), int(sys.argv[2])
cases = sys.argv[3:]
out = source / 'filters'
byond = Path('C:/Program Files (x86)/BYOND/bin')
subprocess.run([sys.executable, str(source / 'prepare.py'), str(work)], check=True)
built = subprocess.run([str(byond / 'dm.exe'), 'world.dme'], cwd=work, capture_output=True, text=True, timeout=60)
if ' 0 errors' not in built.stdout: sys.exit(built.stdout)

user32 = ctypes.windll.user32; gdi32 = ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
for f, res in ((user32.GetDC, wintypes.HDC), (gdi32.CreateCompatibleDC, wintypes.HDC),
               (gdi32.CreateCompatibleBitmap, wintypes.HBITMAP), (gdi32.SelectObject, wintypes.HGDIOBJ)):
    f.restype = res
user32.GetDC.argtypes = [wintypes.HWND]; user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]; gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.c_void_p]; user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.c_void_p]
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]; gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]


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
    old = gdi32.SelectObject(dc, bmp); user32.PrintWindow(hwnd, dc, 3); gdi32.SelectObject(dc, old)
    header = (ctypes.c_uint32 * 10)(40, w, ctypes.c_uint32(-h).value, 1 | 32 << 16, 0, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(w * h * 4); gdi32.GetDIBits(dc, bmp, 0, h, pixels, header, 0)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(dc); user32.ReleaseDC(None, screen)
    return Image.frombuffer('RGBA', (w, h), pixels.raw, 'raw', 'BGRA', 0, 1).convert('RGB')


def text(p): return p.read_text(errors='replace') if p.exists() else ''


for case in cases:
    log = work / 'events.log'; log.unlink(missing_ok=True)
    procs = []; ddout = open(work / 'dd.out', 'w')
    try:
        procs.append(subprocess.Popen([str(byond / 'dd.exe'), 'world.dmb', str(port), '-trusted', '-invisible',
                                       '-params', f'set={case}'], cwd=work, stdout=ddout, stderr=subprocess.STDOUT))
        end = time.time() + 30
        while 'World opened' not in text(work / 'dd.out'):
            if time.time() > end: raise RuntimeError('dd did not open: ' + text(work / 'dd.out'))
            time.sleep(.1)
        procs.append(subprocess.Popen([str(byond / 'dreamseeker.exe'), f'byond://127.0.0.1:{port}'], cwd=work))
        end = time.time() + 40
        while 'READY' not in text(log):
            if time.time() > end: raise RuntimeError('no READY')
            time.sleep(.1)
        time.sleep(4)
        img = grab(procs[1].pid)
        img.crop((0, 0, 224, 224)).save(out / f'{case}.png', optimize=True)
        print(case, img.size, 'saved', flush=True)
    except Exception as e:
        print(case, 'FAILED', e, flush=True)
    finally:
        for p in reversed(procs):
            if p.poll() is None: p.kill()
        for p in procs: p.wait()
        ddout.close()
        time.sleep(1)
