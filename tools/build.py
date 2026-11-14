#!/usr/bin/env python3
"""
Builds a Pixlpal app: compiles its C files to WebAssembly with the WASI SDK, checks the result, and packs it with its
app.json into a package (.pxa) that Pixlpal installs.

  python3 tools/build.py APP_FOLDER [-o OUT.pxa] [--key KEY] [--keep-wasm]

APP_FOLDER holds app.json and the app's .c files (all of them are compiled). The package is written beside them,
named after the .c file if there is one, else after the folder. With --key (or PXP_APP_KEY), it is signed: only
Meterbit signs, with its app key, for catalogue apps. Without, it is a personal app, which installs in Developer Mode.

The WASI SDK (github.com/WebAssembly/wasi-sdk/releases): the folder in WASI_SDK, else ~/.pixlpal-tools/wasi-sdk,
else /opt/wasi-sdk. Works on macOS, Linux and Windows.
"""
import argparse, glob, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pxa, wasm_info  # noqa: E402

SDK_HEADER_DIR = os.path.join(HERE, "..", "sdk")

# MVP WebAssembly plus the features WAMR on Pixlpal runs. Without -mcpu=mvp, newer clang turns on reference types,
# whose table encoding Pixlpal's runtime refuses. -nostdlib: an app brings its own small helpers.
FLAGS = ["--target=wasm32", "-mcpu=mvp", "-mbulk-memory", "-msign-ext", "-mnontrapping-fptoint", "-mmutable-globals",
         "-O2", "-nostdlib", "-fno-builtin", "-Wall",
         "-Wl,--no-entry", "-Wl,--export-memory", "-Wl,--strip-all", "-Wl,-z,stack-size=16384",
         "-Wl,--initial-memory=131072"]


def find_clang():
    places = [os.environ.get("WASI_SDK"), os.path.expanduser("~/.pixlpal-tools/wasi-sdk"), "/opt/wasi-sdk"]
    for p in places:
        if not p: continue
        for name in ("clang", "clang.exe"):
            c = os.path.join(p, "bin", name)
            if os.path.isfile(c): return c
    sys.exit("No WASI SDK found. Download it from github.com/WebAssembly/wasi-sdk/releases and put it in "
             "~/.pixlpal-tools/wasi-sdk, or set WASI_SDK to its folder.")


def build(folder, out=None, key=None, quiet=False, keep_wasm=False):
    """Builds the app in folder. Returns (package path, manifest, wasm bytes)."""
    folder = os.path.abspath(folder)
    manifest_path = os.path.join(folder, "app.json")
    if not os.path.isfile(manifest_path): sys.exit(f"{folder} has no app.json")
    try:
        manifest = json.load(open(manifest_path, encoding="utf-8"))
    except json.JSONDecodeError as e:
        sys.exit(f"app.json is not valid JSON: {e}")
    pxa.check_manifest(manifest)
    sources = sorted(glob.glob(os.path.join(folder, "*.c")))
    if not sources: sys.exit(f"{folder} has no .c files")
    if out is None:
        stem = os.path.splitext(os.path.basename(sources[0]))[0] if len(sources) == 1 else os.path.basename(folder)
        out = os.path.join(folder, stem + ".pxa")
    with tempfile.TemporaryDirectory() as t:
        wasm_path = os.path.join(t, "app.wasm")
        r = subprocess.run([find_clang(), *FLAGS, "-I", SDK_HEADER_DIR, "-o", wasm_path, *sources])
        if r.returncode: sys.exit("the app did not compile")
        problems = wasm_info.problems(wasm_path)
        if problems: sys.exit("the app would not run on Pixlpal: " + "; ".join(problems))
        wasm = open(wasm_path, "rb").read()
    if keep_wasm: open(os.path.splitext(out)[0] + ".wasm", "wb").write(wasm)
    package = pxa.build_package(manifest, wasm, key)
    open(out, "wb").write(package)
    if not quiet:
        kind = "signed (catalogue)" if key else "not signed (a personal app: Developer Mode)"
        print(f"{out}: {len(package)} bytes ({len(wasm)} of WebAssembly), {kind}")
    return out, manifest, wasm


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("folder")
    p.add_argument("-o", "--out")
    p.add_argument("--key", default=os.environ.get("PXP_APP_KEY"))
    p.add_argument("--keep-wasm", action="store_true", help="also keep the .wasm beside the package")
    a = p.parse_args()
    build(a.folder, a.out, a.key, keep_wasm=a.keep_wasm)


if __name__ == "__main__":
    main()
