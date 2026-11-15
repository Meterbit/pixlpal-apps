#!/usr/bin/env python3
"""
The Pixlpal app catalogue: checks every app in apps/ and, for Meterbit with its app-signing key, signs and lists them
in catalogue/, where the Pixlpal phone app and Pixlpal devices read them.

  python3 tools/publish.py check                build and check every app (what a pull request must pass); writes
                                                nothing
  python3 tools/publish.py verify               check catalogue/ against the public key: the signatures, the files
                                                and their fingerprints
  python3 tools/publish.py publish --key KEY    number new apps, sign new versions into catalogue/packages/, write
                                                and sign the index
  python3 tools/publish.py withdraw ID --reason TEXT [--versions 1.0.0,1.0.1] --key KEY
                                                put an app (or some of its versions) on the withdrawn list; Pixlpal
                                                devices stop installing and opening it
  python3 tools/publish.py restore ID --key KEY take an app off the withdrawn list

KEY can also come from PXP_APP_KEY. It must be the key whose public half is catalogue/public_key.pem.
docs/MAINTAINING.md has the whole routine.
"""
import argparse, datetime, glob, hashlib, json, os, shutil, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build, pxa  # noqa: E402

ROOT = os.path.dirname(HERE)
APPS = os.path.join(ROOT, "apps")
EXAMPLES = os.path.join(ROOT, "examples")
CAT = os.path.join(ROOT, "catalogue")
INDEX, INDEX_SIG = os.path.join(CAT, "index.json"), os.path.join(CAT, "index.sig")
WITHDRAWN, WITHDRAWN_SIG = os.path.join(CAT, "withdrawn.json"), os.path.join(CAT, "withdrawn.sig")
PUBLIC_KEY = os.path.join(CAT, "public_key.pem")
BASE_URL = "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/"

# The store's categories: the phone app's own (it lists an installed app beside the built-in apps of its category).
CATEGORIES = pxa.CATEGORIES
ICON_MIN, ICON_MAX, ICON_BYTES = 64, 512, 32 * 1024
ABOUT_MAX = 300


def load(path, default):
    return json.load(open(path, encoding="utf-8")) if os.path.exists(path) else default


def write_json(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def png_size(path):
    b = open(path, "rb").read(24)
    if b[:8] != b"\x89PNG\r\n\x1a\n" or b[12:16] != b"IHDR": return None
    return struct.unpack(">II", b[16:24])


def folders(root):
    return sorted(d for d in glob.glob(os.path.join(root, "*")) if os.path.isfile(os.path.join(d, "app.json")))


def app_id_number(text):
    g, _, s = text.partition("/")
    return int(g), int(s)


def withdrawn_versions(withdrawn, app_id):
    """None: not withdrawn; []: every version; else the versions withdrawn."""
    for w in withdrawn.get("apps", []):
        if w["id"] == app_id: return w.get("versions", [])
    return None


def is_withdrawn(withdrawn, app_id, version):
    v = withdrawn_versions(withdrawn, app_id)
    return v is not None and (v == [] or version in v)


def published_package(entry):
    path = os.path.join(CAT, entry["package"])
    return open(path, "rb").read() if os.path.exists(path) else None


class Problems(list):
    def add(self, where, text): self.append(f"{where}: {text}")


# Checking -------------------------------------------------------------------------------------------------------------
def check_app(folder, problems, catalogue=True):
    """Builds the app in folder (unsigned) and checks it. Returns (manifest, wasm), or None if it failed."""
    where = os.path.relpath(folder, ROOT)
    try:
        manifest = json.load(open(os.path.join(folder, "app.json"), encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        problems.add(where, f"app.json: {e}"); return None
    for p in pxa.manifest_problems(manifest): problems.add(where, p)
    if catalogue:
        if manifest.get("id") != os.path.basename(folder):
            problems.add(where, f"the folder must be named after the app's id ({manifest.get('id')})")
        if manifest.get("category") not in CATEGORIES:
            problems.add(where, "\"category\" is one of " + ", ".join(CATEGORIES))
        if not isinstance(manifest.get("about"), str) or not manifest["about"].strip():
            problems.add(where, "\"about\" says in a sentence or two what the app does")
        elif len(manifest["about"]) > ABOUT_MAX:
            problems.add(where, f"\"about\" is longer than {ABOUT_MAX} characters")
        icon = os.path.join(folder, "icon.png")
        if not os.path.exists(icon):
            problems.add(where, "icon.png is missing (a square PNG, 128 x 128 is best)")
        else:
            size = png_size(icon)
            if not size: problems.add(where, "icon.png is not a PNG")
            elif size[0] != size[1] or not ICON_MIN <= size[0] <= ICON_MAX:
                problems.add(where, f"icon.png is {size[0]} x {size[1]}: it must be square, {ICON_MIN} to {ICON_MAX} pixels")
            if os.path.getsize(icon) > ICON_BYTES: problems.add(where, f"icon.png is bigger than {ICON_BYTES // 1024} KB")
    try:
        with tempfile.TemporaryDirectory() as t:
            _, _, wasm = build.build(folder, os.path.join(t, "app.pxa"), None, quiet=True)
    except SystemExit as e:
        problems.add(where, str(e)); return None
    return manifest, wasm


def check_all(problems):
    """Every app and example. Returns {id: (folder, manifest, wasm)} for the catalogue's apps."""
    index = load(INDEX, {"apps": []})
    published = {e["id"]: e for e in index["apps"]}
    apps, numbers = {}, {}
    for folder in folders(APPS):
        r = check_app(folder, problems)
        if not r: continue
        manifest, wasm = r
        where = os.path.relpath(folder, ROOT)
        if manifest["id"] in apps: problems.add(where, f"another app has the id {manifest['id']}"); continue
        number = manifest.get("appID")
        if number:
            if number in numbers: problems.add(where, f"{numbers[number]} has the same appID {number}")
            numbers[number] = manifest["id"]
        old = published.get(manifest["id"])
        if old:
            if number and old["appID"] != number:
                problems.add(where, f"its appID changed from {old['appID']}: a published app keeps its number")
            newer = pxa.version_key(manifest["version"]) > pxa.version_key(old["version"])
            if pxa.version_key(manifest["version"]) < pxa.version_key(old["version"]):
                problems.add(where, f"version {manifest['version']} is older than the published {old['version']}")
            elif not newer:
                pkg = published_package(old)
                if pkg:
                    m, w, _, _ = pxa.parse(pkg)
                    if w != wasm or {k: v for k, v in m.items()} != dict(manifest, appID=old["appID"]):
                        problems.add(where, f"it changed since {old['version']} was published: raise its version")
        apps[manifest["id"]] = (folder, manifest, wasm)
    for folder in folders(EXAMPLES):
        check_app(folder, problems, catalogue=False)
    return apps


def cmd_check(_):
    problems = Problems()
    apps = check_all(problems)
    for p in problems: print("PROBLEM", p)
    print(f"{len(apps)} app{'s' if len(apps) != 1 else ''} in the catalogue checked: "
          + ("all good" if not problems else f"{len(problems)} problem{'s' if len(problems) != 1 else ''}"))
    sys.exit(1 if problems else 0)


# Verifying catalogue/ ---------------------------------------------------------------------------------------------------
def cmd_verify(_):
    problems = Problems()
    for f, s in ((INDEX, INDEX_SIG), (WITHDRAWN, WITHDRAWN_SIG)):
        name = os.path.relpath(f, ROOT)
        if not os.path.exists(f) or not os.path.exists(s): problems.add(name, "missing, or its signature is")
        elif not pxa.verify_bytes(PUBLIC_KEY, open(f, "rb").read(), open(s, "rb").read()):
            problems.add(name, "its signature is not valid")
    index = load(INDEX, {"apps": []})
    withdrawn = load(WITHDRAWN, {"apps": []})
    for e in index.get("apps", []):
        where = f"index: {e.get('id')}"
        pkg = published_package(e)
        if pkg is None: problems.add(where, f"{e['package']} is missing"); continue
        if hashlib.sha256(pkg).hexdigest() != e["sha256"]: problems.add(where, "the package's fingerprint differs")
        if len(pkg) != e["size"]: problems.add(where, "the package's size differs")
        try:
            m, _, body, sig = pxa.parse(pkg)
        except ValueError as x:
            problems.add(where, f"the package is damaged: {x}"); continue
        if not sig or not pxa.verify_bytes(PUBLIC_KEY, body, sig): problems.add(where, "the package is not signed with the app key")
        for k in ("id", "version", "appID", "name"):
            if m.get(k) != e.get(k): problems.add(where, f"its {k} differs from the package's")
        if e.get("icon") and not os.path.exists(os.path.join(CAT, e["icon"])): problems.add(where, f"{e['icon']} is missing")
        if is_withdrawn(withdrawn, e["id"], e["version"]): problems.add(where, "it is listed, and withdrawn")
    for p in problems: print("PROBLEM", p)
    print(f"catalogue: {len(index.get('apps', []))} apps, index {index.get('sequence')}, withdrawn list "
          f"{withdrawn.get('sequence')}: " + ("all good" if not problems else f"{len(problems)} problems"))
    sys.exit(1 if problems else 0)


# Publishing ---------------------------------------------------------------------------------------------------------------
def key_or_exit(key):
    if not key: sys.exit("give --key, or set PXP_APP_KEY (Meterbit's app-signing key)")
    probe = b"pixlpal catalogue key check"
    if not pxa.verify_bytes(PUBLIC_KEY, probe, pxa.sign_bytes(key, probe)):
        sys.exit(f"{key} is not the key whose public half is catalogue/public_key.pem")
    return key


def sign_file(key, path, sig_path):
    open(sig_path, "wb").write(pxa.sign_bytes(key, open(path, "rb").read()))


def next_free_number(used):
    for g in range(100, 200):
        for s in range(256):
            if f"{g}/{s}" not in used: return f"{g}/{s}"
    sys.exit("no catalogue number left")


def publish(key, quiet=False):
    problems = Problems()
    apps = check_all(problems)
    if problems:
        for p in problems: print("PROBLEM", p)
        sys.exit("not published: fix the problems first")
    index = load(INDEX, {"format": 1, "sequence": 0, "apps": []})
    withdrawn = load(WITHDRAWN, {"apps": []})
    published = {e["id"]: e for e in index["apps"]}
    used = {e["appID"] for e in index["apps"]} | {m["appID"] for _, m, _ in apps.values() if m.get("appID")}
    os.makedirs(os.path.join(CAT, "packages"), exist_ok=True)
    os.makedirs(os.path.join(CAT, "icons"), exist_ok=True)
    entries = []
    for app_id, (folder, manifest, wasm) in sorted(apps.items()):
        if not manifest.get("appID"):
            manifest["appID"] = published[app_id]["appID"] if app_id in published else next_free_number(used)
            used.add(manifest["appID"])
            on_disk = json.load(open(os.path.join(folder, "app.json"), encoding="utf-8"))
            on_disk["appID"] = manifest["appID"]
            write_json(os.path.join(folder, "app.json"), on_disk)
            print(f"{app_id}: numbered {manifest['appID']} (written into its app.json: commit it)")
        if is_withdrawn(withdrawn, app_id, manifest["version"]):
            # The newest published version that isn't withdrawn, if there is one; else the app is not listed.
            older = []
            for f in glob.glob(os.path.join(CAT, "packages", app_id, "*.pxa")):
                m, _, _, _ = pxa.parse(open(f, "rb").read())
                if not is_withdrawn(withdrawn, app_id, m["version"]): older.append((pxa.version_key(m["version"]), m))
            if not older:
                print(f"{app_id} {manifest['version']}: withdrawn, not listed"); continue
            manifest = max(older, key=lambda x: x[0])[1]
            print(f"{app_id} {manifest['version']}: listed instead of the withdrawn {apps[app_id][1]['version']}")
        rel = f"packages/{app_id}/{manifest['version']}.pxa"
        path = os.path.join(CAT, rel)
        if os.path.exists(path):
            package = open(path, "rb").read()          # a published version is never signed again
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            package = pxa.build_package(manifest, wasm, key)
            open(path, "wb").write(package)
            print(f"{app_id} {manifest['version']}: signed into {rel}")
        icon = None
        if os.path.exists(os.path.join(folder, "icon.png")):
            icon = f"icons/{app_id}.png"
            shutil.copyfile(os.path.join(folder, "icon.png"), os.path.join(CAT, icon))
        entry = {
            "id": app_id, "name": manifest["name"], "version": manifest["version"], "author": manifest["author"],
            "about": manifest["about"], "category": manifest["category"], "appID": manifest["appID"],
            "api": manifest["api"], "net": manifest.get("net", []),
            "size": len(package), "sha256": hashlib.sha256(package).hexdigest(),
            "package": rel, "icon": icon, "source": f"apps/{app_id}",
        }
        if manifest.get("ownerNet"): entry["ownerNet"] = True     # it may reach sites its owner allows
        entries.append(entry)
    entries.sort(key=lambda e: e["name"].lower())
    if entries == index.get("apps") and index.get("categories") == CATEGORIES and os.path.exists(INDEX_SIG):
        if not quiet: print(f"index {index['sequence']}: no change")
        return
    index = {"format": 1, "sequence": index.get("sequence", 0) + 1,
             "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
             "baseUrl": BASE_URL, "categories": CATEGORIES, "apps": entries}
    write_json(INDEX, index)
    sign_file(key, INDEX, INDEX_SIG)
    print(f"index {index['sequence']}: {len(entries)} app{'s' if len(entries) != 1 else ''}, signed")


def cmd_publish(a):
    key = key_or_exit(a.key)
    if not os.path.exists(WITHDRAWN): save_withdrawn(key, {"format": 1, "sequence": 0, "apps": []})
    publish(key)


def save_withdrawn(key, w):
    w["sequence"] = w.get("sequence", 0) + 1
    write_json(WITHDRAWN, w)
    sign_file(key, WITHDRAWN, WITHDRAWN_SIG)
    print(f"withdrawn list {w['sequence']}: {len(w['apps'])} app{'s' if len(w['apps']) != 1 else ''}, signed")


def cmd_withdraw(a):
    key = key_or_exit(a.key)
    w = load(WITHDRAWN, {"format": 1, "sequence": 0, "apps": []})
    w["apps"] = [x for x in w["apps"] if x["id"] != a.id]
    entry = {"id": a.id, "reason": a.reason}
    if a.versions: entry["versions"] = [v.strip() for v in a.versions.split(",") if v.strip()]
    w["apps"].append(entry)
    save_withdrawn(key, w)
    publish(key, quiet=True)


def cmd_restore(a):
    key = key_or_exit(a.key)
    w = load(WITHDRAWN, {"format": 1, "sequence": 0, "apps": []})
    if not any(x["id"] == a.id for x in w["apps"]): sys.exit(f"{a.id} is not on the withdrawn list")
    w["apps"] = [x for x in w["apps"] if x["id"] != a.id]
    save_withdrawn(key, w)
    publish(key, quiet=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    sub.add_parser("verify")
    for name in ("publish", "withdraw", "restore"):
        s = sub.add_parser(name)
        s.add_argument("--key", default=os.environ.get("PXP_APP_KEY"))
        if name != "publish": s.add_argument("id")
        if name == "withdraw":
            s.add_argument("--reason", required=True)
            s.add_argument("--versions")
    a = p.parse_args()
    {"check": cmd_check, "verify": cmd_verify, "publish": cmd_publish, "withdraw": cmd_withdraw,
     "restore": cmd_restore}[a.cmd](a)


if __name__ == "__main__":
    main()
