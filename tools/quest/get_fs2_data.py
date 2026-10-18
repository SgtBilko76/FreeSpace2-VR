#!/usr/bin/env python3
"""Stage FreeSpace 2 game data from a FreeSpace 2 install (GOG or Steam) or from the
GOG offline installer, and optionally copy it to a Quest connected over USB.

  python3 get_fs2_data.py <installed game folder | setup_freespace_2_*.exe> [output folder] [--push]

The GOG installer is unpacked with innoextract (https://constexpr.org/innoextract/),
which must be in PATH (or set INNOEXTRACT=/path/to/innoextract).
Output: <output>/FS2 with the nine retail .vp files and data/movies/*.mve (about 1.9 GB).
--push also needs adb in PATH and copies the result to /sdcard/FS2/ on the headset.
"""
import os
import shutil
import subprocess
import sys

RETAIL_VPS = ["root_fs2.vp", "smarty_fs2.vp", "sparky_fs2.vp", "sparky_hi_fs2.vp", "stu_fs2.vp",
              "tango1_fs2.vp", "tango2_fs2.vp", "tango3_fs2.vp", "warble_fs2.vp"]


def find_files(root):
    """Map lower-case file name -> path for every file under root (first hit wins)."""
    found = {}
    for d, _, files in os.walk(root):
        for f in files:
            found.setdefault(f.lower(), os.path.join(d, f))
    return found


def place(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    src = os.path.abspath(args[0])
    out = os.path.abspath(args[1] if len(args) > 1 else ".")
    game = os.path.join(out, "FS2")

    if os.path.isfile(src) and src.lower().endswith(".exe"):
        inno = os.environ.get("INNOEXTRACT") or shutil.which("innoextract")
        if not inno:
            sys.exit("innoextract not found: install it and run this again")
        unpacked = os.path.join(out, "fs2_installer_files")
        print("unpacking", os.path.basename(src), flush=True)
        subprocess.run([inno, "-s", "-e", "-d", unpacked, src], check=True)
        src = unpacked

    files = find_files(src)
    missing = [v for v in RETAIL_VPS if v not in files]
    if missing:
        sys.exit("not a FreeSpace 2 install, missing: " + ", ".join(missing))
    for v in RETAIL_VPS:
        place(files[v], os.path.join(game, v))
    movies = sorted(f for f in files if f.endswith(".mve"))
    for m in movies:
        place(files[m], os.path.join(game, "data", "movies", m))
    print("staged %d .vp files and %d movies in %s" % (len(RETAIL_VPS), len(movies), game))

    if "--push" in sys.argv:
        adb = shutil.which("adb") or sys.exit("adb not found")
        subprocess.run([adb, "shell", "mkdir", "-p", "/sdcard/FS2/data/movies"], check=True)
        for v in RETAIL_VPS:
            subprocess.run([adb, "push", os.path.join(game, v), "/sdcard/FS2/"], check=True)
        for m in movies:
            subprocess.run([adb, "push", os.path.join(game, "data", "movies", m), "/sdcard/FS2/data/movies/"],
                           check=True)
        print("copied to /sdcard/FS2 on the headset")


if __name__ == "__main__":
    main()
