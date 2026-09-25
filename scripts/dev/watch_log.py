# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Live debug monitor for an OpenShape session someone else is driving.

Start the app with OPENSHAPE_LOG=debug (so PERFORMANCE timings are logged),
then run this in the background while the owner designs:

  python scripts/dev/watch_log.py [LOG] [CSV] [SLOW_MS]

  LOG      default %LOCALAPPDATA%/OpenShape/OpenShape/logs/openshape.log
  CSV      default timeline.csv next to the log (appended; every sample)
  SLOW_MS  default 30

Tails the log and pings the window's GUI thread. Prints one compact line per
actionable event, aggregated per 1.5 s window:
  SLOW    kernel/tessellation/recompute scopes over the threshold
  WARN/ERROR lines, Qt/QML diagnostics, failed features
  TOAST   messages shown to the user
  PREVIEW-FAIL  a live preview the kernel rejected
  STALL   GUI thread did not answer a WM_NULL within 200 ms
Each line ends with the last executed command, for context. Windows-native
Python (MSYS2 UCRT64 python.exe works; standard library only).
"""
import collections
import ctypes
import ctypes.wintypes as wt
import os
import re
import sys
import threading
import time

ARGS = sys.argv[1:] + [""] * 3  # an empty argument means "use the default"
LOG = ARGS[0] or os.path.join(os.environ.get("LOCALAPPDATA", "."), "OpenShape", "OpenShape", "logs", "openshape.log")
CSV = ARGS[1] or os.path.join(os.path.dirname(LOG), "timeline.csv")
SLOW_MS = float(ARGS[2] or 30.0)

user32 = ctypes.WinDLL("user32", use_last_error=True)
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

lock = threading.Lock()
slow = collections.defaultdict(lambda: [0, 0.0])  # label -> [count, max ms]
lines = collections.OrderedDict()                 # text -> count
stalls = []
last_cmd = ["(none)"]
os.makedirs(os.path.dirname(os.path.abspath(CSV)), exist_ok=True)
csv = open(CSV, "a", encoding="utf-8", buffering=1)


def emit(text):
    print(text, flush=True)


def find_window():
    found = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        n = user32.GetWindowTextLengthW(hwnd)
        if n and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if buf.value.endswith("OpenShape"):
                found.append(hwnd)
        return True

    user32.EnumWindows(proc(cb), 0)
    return found[0] if found else None


def pinger():
    hwnd = None
    gone_reported = False
    while True:
        if not hwnd or not user32.IsWindow(hwnd):
            hwnd = find_window()
            if not hwnd:
                if not gone_reported:
                    with lock:
                        lines["APP window gone (closed or crashed)"] = 1
                    gone_reported = True
                time.sleep(1.0)
                continue
            gone_reported = False
        result = ctypes.c_size_t(0)
        t0 = time.perf_counter()
        ok = user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 0x0000, 5000, ctypes.byref(result))  # WM_NULL, SMTO_NORMAL
        dt = (time.perf_counter() - t0) * 1000
        csv.write(f"{time.time():.3f},ping,{dt:.1f}\n")
        if not ok:
            with lock:
                stalls.append(5000.0)
        elif dt > 200:
            with lock:
                stalls.append(dt)
        time.sleep(0.5)


PERF = re.compile(r"PERFORMANCE: (.+?) took ([0-9.]+) ms")
CMD = re.compile(r"COMMAND: (executed|undid|redid|undone|redone)[^']*'([^']*)'")


def tail():
    while not os.path.exists(LOG):  # the app creates it on first start
        time.sleep(0.5)
    with open(LOG, "r", encoding="utf-8", errors="replace") as f:
        f.seek(0, os.SEEK_END)
        buf = ""
        while True:
            chunk = f.read()
            if not chunk:
                time.sleep(0.2)
                continue
            buf += chunk
            *complete, buf = buf.split("\n")
            for line in complete:
                handle(line.strip())


def handle(line):
    if not line or "GLib-GIO" in line or "NcsiUwpApp" in line:
        return
    m = CMD.search(line)
    if m:
        last_cmd[0] = f"{m.group(1)} '{m.group(2)}'"
        csv.write(f"{time.time():.3f},cmd,{m.group(1)} {m.group(2)}\n")
        return
    m = PERF.search(line)
    if m:
        ms = float(m.group(2))
        csv.write(f"{time.time():.3f},perf,{m.group(1)},{ms}\n")
        if ms >= SLOW_MS:
            with lock:
                entry = slow[m.group(1)]
                entry[0] += 1
                entry[1] = max(entry[1], ms)
        return
    text = None
    if line.startswith("[warning]") or line.startswith("[error]"):
        text = line
    elif "user message:" in line:
        text = "TOAST " + line.split("user message:", 1)[1].strip()
    elif "preview failed" in line:
        text = "PREVIEW-FAIL " + re.sub(r" at -?[0-9.e+-]+:", ":", line.split("INTERACTION:", 1)[-1].strip())
    elif " failed" in line.lower() or "exception" in line.lower():
        text = line
    if text:
        csv.write(f"{time.time():.3f},event,{text}\n")
        with lock:
            lines[text] = lines.get(text, 0) + 1


def flusher():
    while True:
        time.sleep(1.5)
        with lock:
            out = []
            ctx = f" [last: {last_cmd[0]}]"
            if slow:
                parts = [f"{label} {mx:.0f}ms" + (f" x{n}" if n > 1 else "") for label, (n, mx) in
                         sorted(slow.items(), key=lambda kv: -kv[1][1])]
                out.append("SLOW " + ", ".join(parts) + ctx)
                slow.clear()
            if stalls:
                out.append(f"STALL GUI thread blocked: {len(stalls)} sample(s), worst {max(stalls):.0f} ms" + ctx)
                stalls.clear()
            for text, n in lines.items():
                out.append(text[:400] + (f"  (x{n})" if n > 1 else ""))
            lines.clear()
        for o in out:
            emit(o)


threading.Thread(target=pinger, daemon=True).start()
threading.Thread(target=flusher, daemon=True).start()
emit(f"watching {LOG} (slow >= {SLOW_MS:.0f} ms, stall > 200 ms)")
tail()
