#!/usr/bin/env python3
"""
Builds a Pixlpal app: compiles its C (or C++) files to WebAssembly with the WASI SDK, checks the result, and packs it
with its app.json into a package (.pxa) that Pixlpal installs.

  python3 tools/build.py APP_FOLDER [-o OUT.pxa] [--key KEY] [--keep-wasm]

APP_FOLDER holds app.json and the app's source: .c and .cpp files, in it or its folders below (an include/ folder is
on the include path). The package is written beside them, named after the .c file if there is one alone, else after
the folder. With --key (or PXP_APP_KEY), it is signed: only Meterbit signs, with its app key, for catalogue apps.
Without, it is a personal app, which installs in Developer Mode.

Two ways to build:
  - A C app with no .cpp files and no "libc" in app.json: no C library (-nostdlib), the smallest app.
  - Otherwise (a .cpp file, or "libc": true): the C and C++ libraries (malloc, snprintf, strtol, math...), built for
    WASI. Pixlpal provides the few WASI functions the C library needs (the time, random numbers, stdout into its log).
    With "kit": "compat" in app.json, the compat layer is added too: the firmware's app framework, so an app written
    for the firmware builds with few changes (compat/README.md).

The WASI SDK (github.com/WebAssembly/wasi-sdk/releases, version 34): the folder in WASI_SDK, else
~/.pixlpal-tools/wasi-sdk, else /opt/wasi-sdk. Works on macOS, Linux and Windows.
"""
import argparse, glob, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pxa, wasm_info  # noqa: E402

SDK_HEADER_DIR = os.path.join(HERE, "..", "sdk")
# The compat layer: beside tools/ in the firmware (components/meterbit_wasm/compat), in sdk/ in the catalogue.
COMPAT_DIRS = [os.path.join(HERE, "..", "compat"), os.path.join(HERE, "..", "sdk", "compat")]
# ArduinoJson, which the compat layer (and the firmware's apps) use: the compat layer's copy, or the firmware's.
JSON_DIRS = [os.path.join(d, "third_party", "ArduinoJson", "src") for d in COMPAT_DIRS] + \
            [os.path.join(HERE, "..", "..", "ArduinoJson-7xx", "src")]

# MVP WebAssembly plus the features WAMR on Pixlpal runs. Without -mcpu=mvp, newer clang turns on reference types,
# whose table encoding Pixlpal's runtime refuses.
FEATURES = ["-mcpu=mvp", "-mbulk-memory", "-msign-ext", "-mnontrapping-fptoint", "-mmutable-globals"]
BARE = ["--target=wasm32", *FEATURES, "-O2", "-nostdlib", "-fno-builtin", "-Wall",
        "-Wl,--no-entry", "-Wl,--export-memory", "-Wl,--strip-all", "-Wl,-z,stack-size=16384",
        "-Wl,--initial-memory=131072"]
LIBC = ["--target=wasm32-wasip1", *FEATURES, "-O2"]
CXX = ["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-std=gnu++17"]
LIBC_LINK = ["-mexec-model=reactor", "-Wl,--strip-all", "-Wl,-z,stack-size=65536", "-Wl,--gc-sections", "-Wl,--max-memory=4194304"]


def find_tool(name):
    places = [os.environ.get("WASI_SDK"), os.path.expanduser("~/.pixlpal-tools/wasi-sdk"), "/opt/wasi-sdk"]
    for p in places:
        if not p: continue
        for n in (name, name + ".exe"):
            c = os.path.join(p, "bin", n)
            if os.path.isfile(c): return c
    sys.exit("No WASI SDK found. Download it from github.com/WebAssembly/wasi-sdk/releases and put it in "
             "~/.pixlpal-tools/wasi-sdk, or set WASI_SDK to its folder.")


def first_dir(dirs, what):
    for d in dirs:
        if os.path.isdir(d): return os.path.abspath(d)
    sys.exit(f"{what} is missing (looked in {', '.join(dirs)})")


def sources_in(folder):
    out = []
    for root, dirs, files in os.walk(folder):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        out += [os.path.join(root, f) for f in sorted(files) if f.endswith((".c", ".cpp"))]
    return out


def compile_libc(sources, includes, defines, out_wasm, t):
    """C and C++ with the libraries: each file to an object, then one link."""
    objects = []
    for i, src in enumerate(sources):
        cpp = src.endswith(".cpp")
        cc = find_tool("clang++" if cpp else "clang")
        obj = os.path.join(t, f"{i}.o")
        args = [cc, *LIBC, *(CXX if cpp else ["-std=gnu11"]), *defines, *[f"-I{d}" for d in includes], "-c", src, "-o", obj]
        r = subprocess.run(args)
        if r.returncode: sys.exit(f"{os.path.basename(src)} did not compile")
        objects.append(obj)
    r = subprocess.run([find_tool("clang++"), *LIBC, *LIBC_LINK, *objects, "-o", out_wasm])
    if r.returncode: sys.exit("the app did not link")


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
    sources = sources_in(folder)
    if not sources: sys.exit(f"{folder} has no .c or .cpp files")
    top_c = sorted(glob.glob(os.path.join(folder, "*.c")))
    if out is None:
        stem = os.path.splitext(os.path.basename(top_c[0]))[0] if len(top_c) == 1 and len(sources) == 1 else os.path.basename(folder)
        out = os.path.join(folder, stem + ".pxa")
    compat = manifest.get("kit") == "compat"
    libc = compat or manifest.get("libc") is True or any(s.endswith(".cpp") for s in sources)
    # Paths the compiler writes into the code (assert's __FILE__) name no folder on the machine that built it: the same
    # app builds the same anywhere (publish.py check compares the bytes), and a package carries nobody's home folder.
    maps = [f"-ffile-prefix-map={folder}=app"]
    # The API version the app says it was written for, as a number (0.7 is 7, 1.2 would be 102): the compat layer
    # uses the newer functions only for an app that says so (on an older Pixlpal it isn't offered at all).
    major, minor = ([int(x) for x in manifest["api"].split(".")] + [0])[:2]
    maps.append(f"-DPXL_APP_API={major * 100 + minor}")
    with tempfile.TemporaryDirectory() as t:
        wasm_path = os.path.join(t, "app.wasm")
        if libc:
            includes = [folder, os.path.join(folder, "include"), SDK_HEADER_DIR]
            defines = ["-DPIXLPAL_WASM=1", *maps]
            if compat:
                c = first_dir(COMPAT_DIRS, "The compat layer")
                includes = [folder, os.path.join(folder, "include"), os.path.join(c, "include"), SDK_HEADER_DIR,
                            first_dir(JSON_DIRS, "ArduinoJson")]
                defines += ["-DPIXLPAL_COMPAT=1", "-DARDUINOJSON_ENABLE_STD_STREAM=0", "-DARDUINOJSON_ENABLE_STD_STRING=0",
                            "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_PROGMEM=0",
                            "-Wno-unused-function", "-Wno-unused-variable"]
                sources = sources + sorted(glob.glob(os.path.join(c, "src", "*.cpp")))
                defines.append(f"-ffile-prefix-map={c}=compat")
                shared = manifest.get("sharedNvs", [])
                if shared: defines.append('-DPXL_COMPAT_SHARED_NVS="' + ",".join(shared) + '"')
            compile_libc(sources, includes, defines, wasm_path, t)
        else:
            r = subprocess.run([find_tool("clang"), *BARE, *maps, "-I", SDK_HEADER_DIR, "-o", wasm_path, *sources])
            if r.returncode: sys.exit("the app did not compile")
        problems = wasm_info.problems(wasm_path, manifest["api"])
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
