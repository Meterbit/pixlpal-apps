#!/usr/bin/env python3
"""Lists a WebAssembly module's imports and exports. With --check, fails if anything is imported from a module other
than "pixlpal" (an app linked against anything else would not start on Pixlpal), imports a function the API doesn't
have, or doesn't export pxl_frame and its memory. build.py runs the same checks."""
import sys

def uleb(b, i):
    r = s = 0
    while True:
        x = b[i]; i += 1
        r |= (x & 0x7F) << s; s += 7
        if not x & 0x80: return r, i

def name(b, i):
    n, i = uleb(b, i)
    return b[i:i + n].decode(), i + n

def info(path):
    b = open(path, "rb").read()
    assert b[:4] == b"\0asm", "not a WebAssembly module"
    i, imports, exports = 8, [], []
    while i < len(b):
        sid = b[i]; size, i = uleb(b, i + 1); end = i + size
        if sid == 2:
            n, j = uleb(b, i)
            for _ in range(n):
                mod, j = name(b, j); fld, j = name(b, j); kind = b[j]; j += 1
                if kind == 0: _, j = uleb(b, j)
                elif kind == 1: j += 1; flags, j = uleb(b, j); _, j = uleb(b, j); j = uleb(b, j)[1] if flags & 1 else j
                elif kind == 2: flags, j = uleb(b, j); _, j = uleb(b, j); j = uleb(b, j)[1] if flags & 1 else j
                elif kind == 3: j += 2
                imports.append((mod, fld, kind))
        elif sid == 7:
            n, j = uleb(b, i)
            for _ in range(n):
                fld, j = name(b, j); kind = b[j]; _, j = uleb(b, j + 1)
                exports.append((fld, kind))
        i = end
    return imports, exports

KIND = {0: "function", 1: "table", 2: "memory", 3: "global"}

# The Pixlpal App API (sdk/pixlpal.h): the only functions an app may import.
API = {"fill", "rect", "frame", "pixel", "circle", "triangle", "text", "text_width", "millis", "random", "sound",
       "sound_stop", "save_read", "save_write", "phone_ui", "phone_values", "json_int", "json_str", "log",
       # 0.3
       "time", "clock_24h", "line", "round_rect", "bitmap", "bitmap_mono", "say", "silent", "http_request", "json_float", "json_count",
       # 0.4
       "yield", "sound_busy", "open_app", "open_previous_app", "usb_state", "brightness", "set_brightness",
       "brightness_step", "file_read", "file_write", "file_remove", "file_list", "phone_search", "phone_send", "phone_data", "status_text"}
# The WASI functions Pixlpal provides, for the C library (the time, random numbers, stdout into the log).
WASI = {"clock_time_get", "fd_write", "fd_close", "fd_seek", "fd_fdstat_get", "fd_prestat_get", "fd_prestat_dir_name",
        "environ_sizes_get", "environ_get", "args_sizes_get", "args_get", "random_get", "sched_yield", "proc_exit"}


def problems(path):
    """Why the module would not run on Pixlpal, as a list of sentences (empty: it would)."""
    imports, exports = info(path)
    out = []
    outside = [f"{m}.{f}" for m, f, k in imports if m != "pixlpal" and not (m == "wasi_snapshot_preview1" and f in WASI)]
    if outside: out.append("it imports from outside the Pixlpal App API: " + ", ".join(outside)
                           + " (a C library function that needs files, sockets or threads: Pixlpal has none of those)")
    unknown = [f for m, f, k in imports if m == "pixlpal" and (f not in API or k != 0)]
    if unknown: out.append("it imports pixlpal functions that don't exist: " + ", ".join(unknown))
    names = {f for f, k in exports}
    if "pxl_frame" not in names and "pxl_main" not in names:
        out.append("it exports neither pxl_frame nor pxl_main (PXL_EXPORT(\"pxl_frame\"))")
    if "memory" not in names: out.append("it doesn't export its memory (link with -Wl,--export-memory)")
    return out


if __name__ == "__main__":
    check = "--check" in sys.argv
    path = [a for a in sys.argv[1:] if a != "--check"][0]
    if check:
        p = problems(path)
        if p: print("; ".join(p)); sys.exit(1)
        sys.exit(0)
    imports, exports = info(path)
    print("imports:", ", ".join(f"{m}.{f}" for m, f, k in imports))
    print("exports:", ", ".join(f"{f} ({KIND[k]})" for f, k in exports))
