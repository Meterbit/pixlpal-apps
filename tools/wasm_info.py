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
       "sound_stop", "save_read", "save_write", "phone_ui", "phone_values", "json_int", "json_str", "log"}


def problems(path):
    """Why the module would not run on Pixlpal, as a list of sentences (empty: it would)."""
    imports, exports = info(path)
    out = []
    outside = [f"{m}.{f}" for m, f, k in imports if m != "pixlpal"]
    if outside: out.append("it imports from outside the Pixlpal App API: " + ", ".join(outside)
                           + " (a C library function? Apps are built with -nostdlib: write a small helper instead)")
    unknown = [f for m, f, k in imports if m == "pixlpal" and (f not in API or k != 0)]
    if unknown: out.append("it imports pixlpal functions that don't exist: " + ", ".join(unknown))
    names = {f for f, k in exports}
    if "pxl_frame" not in names: out.append("it doesn't export pxl_frame (PXL_EXPORT(\"pxl_frame\"))")
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
