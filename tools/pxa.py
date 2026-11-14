#!/usr/bin/env python3
"""
Pixlpal app packages (.pxa): pack, sign, inspect, and the firmware's copy of the app-signing public key.

  pxa.py pack app.json app.wasm -o app.pxa [--key KEY]   pack; with --key, sign (Meterbit's app key: catalogue apps)
  pxa.py info app.pxa [--key KEY]                        show it; with --key, check its signature against that key
  pxa.py trust KEY -o include/mtb_app_trust.h            write the public half of KEY for the firmware
  pxa.py sign FILE --key KEY [-o FILE.sig]                sign any file (the catalogue's index and withdrawn list)
  pxa.py verify FILE SIG --key KEY                        check such a signature (KEY: the private or public key)

The format (all lengths little-endian uint32):
  "PXA1" | manifest length | manifest (UTF-8 JSON) | wasm length | wasm | ["SIG1" | 64-byte signature]
The signature is ECDSA P-256 (r || s, 32 bytes each) over the SHA-256 of everything before "SIG1". A file's
signature (sign) is the same 64 bytes over the whole file, in a file of its own.

The manifest: {"id": "com.example.weather", "name": "Weather", "version": "1.0.0", "author": "...", "api": "0.2",
               "about": "...", "appID": "100/3"}. "appID" is given by Meterbit to catalogue apps (100-199) and is only
trusted in a signed package; a personal (unsigned) app gets its number from the device (200 and up).

Signing uses OpenSSL (openssl on the PATH). The key never goes in a repository: this refuses a key inside one.
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile, hashlib

MAGIC, SIG_MAGIC = b"PXA1", b"SIG1"
REQUIRED = ("id", "name", "version", "author", "api")
MAX_WASM = 512 * 1024
# The device keeps these in fixed fields (mtb_app_package.h): longer ones would be cut.
LIMITS = {"id": 47, "name": 39, "version": 15, "author": 31}
ID_FORMAT = re.compile(r"^[a-z0-9]+(\.[a-z0-9][a-z0-9-]*)+$")      # com.example.weather
VERSION_FORMAT = re.compile(r"^\d+(\.\d+){0,3}$")                 # 1.0.0


def refuse_key_in_repo(key):
    d = os.path.dirname(os.path.abspath(key))
    while d and d != os.path.dirname(d):
        if os.path.isdir(os.path.join(d, ".git")):
            sys.exit(f"{key} is inside a repository ({d}). Keep signing keys outside any repository.")
        d = os.path.dirname(d)


def der_to_raw(der):
    """ECDSA DER (SEQUENCE of two INTEGERs) to r || s, 32 bytes each."""
    assert der[0] == 0x30
    i = 2 if der[1] < 0x80 else 2 + (der[1] & 0x7F)
    out = b""
    for _ in range(2):
        assert der[i] == 0x02
        n = der[i + 1]; v = der[i + 2:i + 2 + n]; i += 2 + n
        v = v.lstrip(b"\x00")
        assert len(v) <= 32
        out += v.rjust(32, b"\x00")
    return out


def raw_to_der(raw):
    def integer(v):
        v = v.lstrip(b"\x00") or b"\x00"
        if v[0] & 0x80: v = b"\x00" + v
        return b"\x02" + bytes([len(v)]) + v
    body = integer(raw[:32]) + integer(raw[32:])
    return b"\x30" + bytes([len(body)]) + body


def openssl(*args, data=None):
    r = subprocess.run(["openssl", *args], input=data, capture_output=True)
    if r.returncode: sys.exit("openssl " + " ".join(args) + " failed: " + r.stderr.decode(errors="replace"))
    return r.stdout


def public_point(key):
    """The key's public point, uncompressed (65 bytes: 04 || X || Y)."""
    der = openssl("ec", "-in", key, "-pubout", "-outform", "DER")
    point = der[-65:]
    assert point[0] == 4 and len(point) == 65, "not a P-256 key"
    return point


def manifest_problems(m):
    """What is wrong with a manifest, as a list of sentences (empty: nothing)."""
    out = []
    for k in REQUIRED:
        if not isinstance(m.get(k), str) or not m[k]: out.append(f"the manifest needs \"{k}\" (a string)")
    for k, n in LIMITS.items():
        if isinstance(m.get(k), str) and len(m[k].encode()) > n: out.append(f"\"{k}\" is longer than {n} bytes")
    if isinstance(m.get("id"), str) and not ID_FORMAT.match(m["id"]):
        out.append("\"id\" is a reverse domain name in lower case, like \"com.example.weather\"")
    if isinstance(m.get("version"), str) and not VERSION_FORMAT.match(m["version"]):
        out.append("\"version\" is numbers and dots, like \"1.2.0\"")
    if isinstance(m.get("api"), str) and not m["api"].startswith("0."):
        out.append("\"api\" is the Pixlpal App API version the app was written for, like \"0.2\"")
    if "appID" in m:
        g, _, s = str(m["appID"]).partition("/")
        if not (g.isdigit() and s.isdigit() and 100 <= int(g) <= 199 and int(s) <= 255):
            out.append("\"appID\" is a catalogue number like \"100/3\" (100-199); leave it out for a personal app")
    return out


def check_manifest(m):
    problems = manifest_problems(m)
    if problems: sys.exit("; ".join(problems))


def version_key(v):
    """For comparing versions: "1.10.0" is newer than "1.9.2"."""
    return tuple(int(x) for x in v.split("."))


def sign_bytes(key, data):
    """Meterbit's 64-byte signature (r || s) of data."""
    refuse_key_in_repo(key)
    return der_to_raw(openssl("dgst", "-sha256", "-sign", key, "-binary", data=data))


def verify_bytes(key, data, sig):
    """True if sig (64 bytes) is the signature of data by key (a private or public key file)."""
    if len(sig) != 64: return False
    with tempfile.TemporaryDirectory() as t:
        pub = os.path.join(t, "pub.pem"); s = os.path.join(t, "sig.der")
        text = open(key, "rb").read()
        open(pub, "wb").write(text if b"PUBLIC KEY" in text else openssl("ec", "-in", key, "-pubout"))
        open(s, "wb").write(raw_to_der(sig))
        r = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", s], input=data, capture_output=True)
    return r.returncode == 0


def build_package(manifest, wasm, key=None):
    """The package's bytes; signed if key is given."""
    check_manifest(manifest)
    if wasm[:4] != b"\0asm": sys.exit("not a WebAssembly module")
    if len(wasm) > MAX_WASM: sys.exit(f"the app is {len(wasm)} bytes; the limit is {MAX_WASM}")
    m = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode()
    body = MAGIC + struct.pack("<I", len(m)) + m + struct.pack("<I", len(wasm)) + wasm
    return body + (SIG_MAGIC + sign_bytes(key, body) if key else b"")


def pack(args):
    manifest = json.load(open(args.manifest, encoding="utf-8"))
    out = build_package(manifest, open(args.wasm, "rb").read(), args.key)
    open(args.out, "wb").write(out)
    print(f"{args.out}: {len(out)} bytes, {'signed' if args.key else 'not signed (a personal app: Developer Mode)'}")


def parse(data):
    if data[:4] != MAGIC: raise ValueError("not a Pixlpal app package")
    (ml,) = struct.unpack_from("<I", data, 4)
    manifest = json.loads(data[8:8 + ml])
    (wl,) = struct.unpack_from("<I", data, 8 + ml)
    end = 12 + ml + wl
    wasm = data[12 + ml:end]
    sig = None
    if len(data) == end + 68 and data[end:end + 4] == SIG_MAGIC: sig = data[end + 4:]
    elif len(data) != end: raise ValueError("bad length")
    return manifest, wasm, data[:end], sig


def info(args):
    manifest, wasm, body, sig = parse(open(args.package, "rb").read())
    print(json.dumps(manifest, indent=2, ensure_ascii=False))
    print(f"wasm: {len(wasm)} bytes, sha256 {hashlib.sha256(wasm).hexdigest()}")
    if not sig: print("signature: none (installs only in Developer Mode)"); return
    if not args.key: print("signature: present (not checked; give --key)"); return
    good = verify_bytes(args.key, body, sig)
    print("signature:", "valid" if good else "NOT VALID for this key")
    if not good: sys.exit(1)


def trust(args):
    refuse_key_in_repo(args.key)
    point = public_point(args.key)
    rows = ", ".join("0x%02x" % b for b in point)
    with open(args.out, "w") as f:
        f.write("// Generated by tools/pxa.py trust: the public half of Meterbit's app-signing key (ECDSA P-256, uncompressed).\n")
        f.write("// Pixlpal installs an app without Developer Mode only if this key signed it. Public: safe in the repository.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write(f"static const uint8_t MTB_APP_TRUST_KEY[65] = {{ {rows} }};\n")
        f.write(f"#define MTB_APP_TRUST_KEY_ID \"{hashlib.sha256(point).hexdigest()[:16]}\"\n")
    print(f"{args.out}: key {hashlib.sha256(point).hexdigest()[:16]}")


def sign(args):
    out = args.out or args.file + ".sig"
    open(out, "wb").write(sign_bytes(args.key, open(args.file, "rb").read()))
    print(f"{out}: signed")


def verify(args):
    good = verify_bytes(args.key, open(args.file, "rb").read(), open(args.sig, "rb").read())
    print(f"{args.file}: signature {'valid' if good else 'NOT VALID'}")
    if not good: sys.exit(1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("pack"); a.add_argument("manifest"); a.add_argument("wasm"); a.add_argument("-o", "--out", required=True); a.add_argument("--key")
    a = sub.add_parser("info"); a.add_argument("package"); a.add_argument("--key")
    a = sub.add_parser("trust"); a.add_argument("key"); a.add_argument("-o", "--out", required=True)
    a = sub.add_parser("sign"); a.add_argument("file"); a.add_argument("--key", required=True); a.add_argument("-o", "--out")
    a = sub.add_parser("verify"); a.add_argument("file"); a.add_argument("sig"); a.add_argument("--key", required=True)
    args = p.parse_args()
    {"pack": pack, "info": info, "trust": trust, "sign": sign, "verify": verify}[args.cmd](args)


if __name__ == "__main__":
    main()
