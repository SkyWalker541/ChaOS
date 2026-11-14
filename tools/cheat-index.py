#!/usr/bin/env python3
"""The cheat lists ChaOS fetches (src/cheats.h): one file per collection in
libretro-database's cht/, "# <commit>" and then each cheat file's name
without .cht, sorted. Run by pages.yml; reads `git ls-tree -z` of a
libretro-database checkout on stdin.

    git -C db ls-tree -z -r --name-only HEAD cht/ | tools/cheat-index.py <commit> <outdir>

The names are libretro's (libretro-database, CC BY-SA 4.0); LICENSE.txt in
<outdir> says so."""
import os
import sys


def main():
    commit, out = sys.argv[1], sys.argv[2]
    names = {}
    for path in sys.stdin.buffer.read().decode("utf-8", "replace").split("\0"):
        parts = path.split("/")
        if len(parts) != 3 or parts[0] != "cht" or not parts[2].endswith(".cht"):
            continue
        names.setdefault(parts[1], []).append(parts[2][:-4])
    os.makedirs(out, exist_ok=True)
    for coll, files in names.items():
        with open(os.path.join(out, coll + ".txt"), "w", encoding="utf-8") as f:
            f.write("# %s\n" % commit)
            for n in sorted(files):
                if "\n" not in n:
                    f.write(n + "\n")
    with open(os.path.join(out, "LICENSE.txt"), "w", encoding="utf-8") as f:
        f.write(
            "These lists name the cheat files in libretro's libretro-database\n"
            "(https://github.com/libretro/libretro-database, cht/), at commit\n"
            "%s, which is licensed CC BY-SA 4.0\n"
            "(https://creativecommons.org/licenses/by-sa/4.0/). They are\n"
            "shared under the same licence. ChaOS fetches the cheat files\n"
            "themselves from libretro's repository.\n" % commit)
    print("%d collections, %d files" % (len(names), sum(len(v) for v in names.values())))


if __name__ == "__main__":
    main()
