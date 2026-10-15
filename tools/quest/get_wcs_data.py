#!/usr/bin/env python3
"""Download Wing Commander Saga 0.9.65 (free, from the FSNebula/Knossos mod repository),
verify and extract it, and optionally copy it to a Quest connected over USB.

  python3 get_wcs_data.py [output folder] [--push]

Needs Python 3 and 7-Zip (7z / 7zz / 7za in PATH, or 7-Zip installed on Windows).
--push also needs adb in PATH and copies the result to /sdcard/WCSaga/ on the headset.
About 3.7 GB is downloaded and about 7 GB is needed after extraction.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request

REPO_URL = "https://fsnebula.org/storage/repo.json"
MOD_ID, MOD_VERSION = "Wing_Commander_Saga", "0.9.65"


def find_7z():
    for name in ("7zz", "7z", "7za"):
        path = shutil.which(name)
        if path:
            return path
    for path in (r"C:\Program Files\7-Zip\7z.exe", r"C:\Program Files (x86)\7-Zip\7z.exe"):
        if os.path.exists(path):
            return path
    sys.exit("7-Zip not found: install it (https://www.7-zip.org) and run this again")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            h.update(block)
    return h.hexdigest()


def download(urls, dst, want):
    for url in urls:
        print("downloading", os.path.basename(dst), "from", url, flush=True)
        try:
            with urllib.request.urlopen(url) as r, open(dst, "wb") as f:
                shutil.copyfileobj(r, f, 1 << 20)
        except OSError as e:
            print("  failed:", e)
            continue
        if sha256(dst) == want:
            return
        print("  checksum mismatch, trying the next mirror")
    sys.exit("could not download " + os.path.basename(dst))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    base = os.path.abspath(args[0] if args else "WCSaga")
    sevenzip = find_7z()
    archives = os.path.join(base, "archives")
    out = os.path.join(base, MOD_ID)
    os.makedirs(archives, exist_ok=True)

    repo = os.path.join(archives, "repo.json")
    if not os.path.exists(repo):
        print("fetching the mod list", flush=True)
        urllib.request.urlretrieve(REPO_URL, repo)
    mods = json.load(open(repo, encoding="utf-8"))["mods"]
    mod = next(m for m in mods if m["id"] == MOD_ID and m["version"] == MOD_VERSION)

    for package in mod["packages"]:
        for f in package["files"]:
            dst = os.path.join(archives, f["filename"])
            done = dst + ".extracted"
            if os.path.exists(done):
                continue
            if not (os.path.exists(dst) and sha256(dst) == f["checksum"][1]):
                download(f["urls"], dst, f["checksum"][1])
            target = os.path.join(out, f.get("dest") or "")
            subprocess.check_call([sevenzip, "x", "-y", "-o" + target, dst], stdout=subprocess.DEVNULL)
            open(done, "w").close()
            os.remove(dst)  # keep disk use down; the .extracted marker remembers it
            print("extracted", f["filename"], flush=True)
    print("game data ready in", out)

    if "--push" in sys.argv:
        adb = shutil.which("adb") or sys.exit("adb not found")
        print("copying to the headset (/sdcard/WCSaga/), this takes a few minutes", flush=True)
        subprocess.check_call([adb, "shell", "mkdir", "-p", "/sdcard/WCSaga"])
        subprocess.check_call([adb, "push", out, "/sdcard/WCSaga/"])
        print("done")


if __name__ == "__main__":
    main()
