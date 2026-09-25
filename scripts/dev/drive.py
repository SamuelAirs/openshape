"""Win32 input driver for exploring a running OpenShape window like a user.

Moves the real mouse and types on the real keyboard, so nobody should be using
the machine while it runs. Needs a Windows-native Python (MSYS2 UCRT64
python.exe works; standard library only).

Usage: python scripts/dev/drive.py "cmd args; cmd args; ..."
  e.g. python scripts/dev/drive.py "focus; click 640 400; key p; type 12; key enter; shot a.png 0.5"
Coordinates are client-area pixels of the OpenShape window (physical pixels,
same space as the screenshots this tool writes). Take a shot first, read the
positions off it, then act; re-shoot after anything that changes the layout.
For repeatable checks prefer the in-app acceptance runner (--acceptance).

Commands:
  info                         window rect / client size / dpi
  shot FILE [SCALE]            capture client area to PNG (SCALE e.g. 0.5)
  move X Y                     move the cursor
  click X Y [MODS]             left click (MODS: shift, ctrl, alt joined by +)
  dclick X Y                   double click
  rclick X Y
  mclick X Y
  drag X1 Y1 X2 Y2 [STEPS] [BUTTON] [MODS]   press-move-release (BUTTON left/right/middle)
  press X Y / release X Y      left button down / up at a point
  wheel X Y NOTCHES            scroll (positive = away from user = zoom in)
  type TEXT                    type text (use \\s for space)
  key NAME [MODS]              enter tab esc del bksp f1 a..z 0..9 left right up down home end
  sleep MS
  focus                        bring window to the foreground
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time
import zlib

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

# Per-monitor DPI awareness v2: all coordinates are physical pixels.
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004
MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP = 0x0008, 0x0010
MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP = 0x0020, 0x0040
MOUSEEVENTF_WHEEL = 0x0800
KEYEVENTF_KEYUP, KEYEVENTF_UNICODE = 0x0002, 0x0004

ULONG_PTR = ctypes.c_size_t


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wt.DWORD), ("wParamL", wt.WORD), ("wParamH", wt.WORD)]


class _U(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def send(*inputs):
    arr = (INPUT * len(inputs))(*inputs)
    n = user32.SendInput(len(inputs), arr, ctypes.sizeof(INPUT))
    if n != len(inputs):
        raise RuntimeError(f"SendInput failed: {ctypes.get_last_error()}")


def mouse(flags, data=0):
    i = INPUT(type=INPUT_MOUSE)
    i.mi = MOUSEINPUT(0, 0, data & 0xFFFFFFFF, flags, 0, 0)
    return i


def kbd(vk=0, scan=0, flags=0):
    i = INPUT(type=INPUT_KEYBOARD)
    i.ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    return i


# ---- window ----------------------------------------------------------------------
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_window():
    found = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if buf.value.endswith("OpenShape") and "Visual Studio" not in buf.value:
            found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    if not found:
        raise SystemExit("OpenShape window not found")
    return found[0]


HWND, TITLE = None, None


def client_origin():
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(HWND, ctypes.byref(pt))
    return pt.x, pt.y


def client_size():
    r = wt.RECT()
    user32.GetClientRect(HWND, ctypes.byref(r))
    return r.right - r.left, r.bottom - r.top


def to_screen(x, y):
    ox, oy = client_origin()
    return int(round(ox + float(x))), int(round(oy + float(y)))


def move_to(x, y):
    sx, sy = to_screen(x, y)
    user32.SetCursorPos(sx, sy)


# ---- keys ------------------------------------------------------------------------
VK = {"enter": 0x0D, "return": 0x0D, "tab": 0x09, "esc": 0x1B, "escape": 0x1B, "del": 0x2E, "delete": 0x2E,
      "bksp": 0x08, "backspace": 0x08, "space": 0x20, "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
      "home": 0x24, "end": 0x23, "f1": 0x70, "f2": 0x71, "f5": 0x74, "shift": 0x10, "ctrl": 0x11, "alt": 0x12}
MODVK = {"shift": 0x10, "ctrl": 0x11, "alt": 0x12}


def mods_down(mods):
    return [kbd(MODVK[m]) for m in mods]


def mods_up(mods):
    return [kbd(MODVK[m], flags=KEYEVENTF_KEYUP) for m in reversed(mods)]


def parse_mods(s):
    if not s:
        return []
    return [m for m in s.lower().split("+") if m in MODVK]


def press_vk(vk, mods=()):
    send(*mods_down(mods), kbd(vk), kbd(vk, flags=KEYEVENTF_KEYUP), *mods_up(mods))


def type_text(text):
    for ch in text:
        res = user32.VkKeyScanW(ord(ch))
        if res != -1 and (res & 0xFF) != 0xFF:
            vk = res & 0xFF
            shift = (res >> 8) & 1
            mods = ["shift"] if shift else []
            press_vk(vk, mods)
        else:
            code = ord(ch)
            send(kbd(0, code, KEYEVENTF_UNICODE), kbd(0, code, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP))
        time.sleep(0.012)


# ---- screenshot ----------------------------------------------------------------------
class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def screenshot(path, scale=1.0):
    ox, oy = client_origin()
    cw, ch = client_size()
    w, h = max(1, int(cw * scale)), max(1, int(ch * scale))
    sdc = user32.GetDC(None)
    mdc = gdi32.CreateCompatibleDC(sdc)
    bmp = gdi32.CreateCompatibleBitmap(sdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    gdi32.SetStretchBltMode(mdc, 4)  # HALFTONE
    SRCCOPY, CAPTUREBLT = 0x00CC0020, 0x40000000
    gdi32.StretchBlt(mdc, 0, 0, w, h, sdc, ox, oy, cw, ch, SRCCOPY | CAPTUREBLT)
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(None, sdc)
    data = memoryview(buf.raw)
    rgb = bytearray(w * h * 3)
    rgb[0::3] = data[2::4]
    rgb[1::3] = data[1::4]
    rgb[2::3] = data[0::4]
    write_png(path, w, h, rgb)
    print(f"shot {path} {w}x{h}")


# ---- commands --------------------------------------------------------------------------
def button_flags(name):
    return {"left": (MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP), "right": (MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP),
            "middle": (MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP)}[name]


def run(cmd):
    parts = cmd.split()
    if not parts:
        return
    op, a = parts[0].lower(), parts[1:]
    if op == "info":
        ox, oy = client_origin()
        cw, ch = client_size()
        dpi = user32.GetDpiForWindow(HWND)
        print(f"title={TITLE!r} client_origin=({ox},{oy}) client={cw}x{ch} dpi={dpi}")
    elif op == "focus":
        user32.keybd_event(0x12, 0, 0, 0)
        user32.keybd_event(0x12, 0, 2, 0)
        user32.SetForegroundWindow(HWND)
        time.sleep(0.15)
    elif op == "shot":
        screenshot(a[0], float(a[1]) if len(a) > 1 else 1.0)
    elif op == "move":
        move_to(a[0], a[1])
        time.sleep(0.03)
    elif op in ("click", "rclick", "mclick"):
        mods = parse_mods(a[2] if len(a) > 2 else "")
        down, up = button_flags({"click": "left", "rclick": "right", "mclick": "middle"}[op])
        move_to(a[0], a[1])
        time.sleep(0.04)
        send(*mods_down(mods))
        send(mouse(down))
        time.sleep(0.02)
        send(mouse(up))
        send(*mods_up(mods))
        time.sleep(0.06)
    elif op == "dclick":
        mods = parse_mods(a[2] if len(a) > 2 else "")
        move_to(a[0], a[1])
        time.sleep(0.04)
        send(*mods_down(mods))
        for _ in range(2):
            send(mouse(MOUSEEVENTF_LEFTDOWN))
            send(mouse(MOUSEEVENTF_LEFTUP))
            time.sleep(0.03)
        send(*mods_up(mods))
        time.sleep(0.06)
    elif op == "press":
        move_to(a[0], a[1])
        time.sleep(0.03)
        send(mouse(MOUSEEVENTF_LEFTDOWN))
    elif op == "release":
        move_to(a[0], a[1])
        time.sleep(0.03)
        send(mouse(MOUSEEVENTF_LEFTUP))
    elif op == "drag":
        x1, y1, x2, y2 = map(float, a[:4])
        steps = int(a[4]) if len(a) > 4 else 20
        button = a[5] if len(a) > 5 else "left"
        mods = parse_mods(a[6] if len(a) > 6 else "")
        down, up = button_flags(button)
        move_to(x1, y1)
        time.sleep(0.05)
        send(*mods_down(mods))
        send(mouse(down))
        time.sleep(0.03)
        for i in range(1, steps + 1):
            t = i / steps
            move_to(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t)
            time.sleep(0.016)
        time.sleep(0.05)
        send(mouse(up))
        send(*mods_up(mods))
        time.sleep(0.06)
    elif op == "wheel":
        move_to(a[0], a[1])
        time.sleep(0.03)
        notches = int(a[2])
        for _ in range(abs(notches)):
            send(mouse(MOUSEEVENTF_WHEEL, 120 if notches > 0 else -120))
            time.sleep(0.02)
    elif op == "type":
        type_text(" ".join(a).replace("\\s", " "))
    elif op == "key":
        name = a[0].lower()
        mods = parse_mods(a[1] if len(a) > 1 else "")
        if name in VK:
            press_vk(VK[name], mods)
        elif len(name) == 1:
            press_vk(user32.VkKeyScanW(ord(name)) & 0xFF, mods)
        else:
            raise SystemExit(f"unknown key {name}")
        time.sleep(0.05)
    elif op == "sleep":
        time.sleep(float(a[0]) / 1000.0)
    else:
        raise SystemExit(f"unknown command {op}")


def main():
    global HWND, TITLE
    HWND, TITLE = find_window()
    for cmd in " ".join(sys.argv[1:]).split(";"):
        run(cmd.strip())


if __name__ == "__main__":
    main()
