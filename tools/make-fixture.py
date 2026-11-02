#!/usr/bin/env python3
"""Cuts tests/data/catalog/ from the distribution's real AppStream catalog (TS-11).

  tools/make-fixture.py    run on the host (it reads /var/lib/swcatalog), then commit the
                           result; tests never read the host's catalog.

Real components are copied whole, as the archive ships them, with their cached icons. The
cases real data does not supply (markup and control characters in names, a bundled app, an
app with no package) are hand-written in chandler-fixture-handmade.yml, labelled as such.
"""

import glob
import gzip
import os
import re
import shutil
import sys

SRC = "/var/lib/swcatalog"
OUT = "tests/data/catalog"

# component id -> why it is in the fixture
REAL = {
    "org.gnome.Evince": "application/pdf; also duplicated into the updates catalog",
    "xournal.desktop": "application/pdf",
    "io.photoflare.photoflare": "image/png",
    "me.mitya57.ReText": "text/markdown",
    "org.kde.okular-md": "an addon providing text/markdown: must not be listed",
    "org.musicbrainz.Picard": "audio/flac",
    "org.gnome.Rhythmbox3.desktop": "lists only audio/x-flac, the alias of audio/flac",
    "io.github.quodlibet.QuodLibet": "lists only audio/x-flac",
}

HEADER = """---
File: DEP-11
Version: '1.0'
Origin: {origin}
MediaBaseUrl: https://appstream.ubuntu.com/media/resolute
"""

HANDMADE = r"""# Hand-written, not cut from the archive: cases real data does not supply.
---
Type: desktop-application
ID: chandler.test.Markup
Package: chandler-test-markup
Name:
  C: <b>Bold</b> & Viewer
Summary:
  C: "Opens <i>PDF</i> files \e[31mred\e[0m\tand tabs"
Provides:
  mediatypes:
  - application/pdf
---
Type: desktop-application
ID: chandler.test.Bundled
Package: chandler-test-bundled
Name:
  C: Bundled Viewer
Summary:
  C: Delivered as a snap
Bundles:
- type: snap
  id: chandler-test-bundled
Provides:
  mediatypes:
  - application/pdf
---
Type: desktop-application
ID: chandler.test.BadPackageName
Package: Bad Name
Name:
  C: Badly Packaged Viewer
Summary:
  C: Its package name is not a Debian package name
Provides:
  mediatypes:
  - application/pdf
---
Type: desktop-application
ID: chandler.test.NoPackage
Name:
  C: Packageless Viewer
Summary:
  C: Names no package
Provides:
  mediatypes:
  - application/pdf
"""


ESCAPE = """---
Type: desktop-application
ID: chandler.test.Escape
Package: chandler-test-escape
Name:
  C: Escaping Viewer
Summary:
  C: Its cached icon lies outside the icon directory
Icon:
  cached:
  - name: escape.png
    width: 64
    height: 64
Provides:
  mediatypes:
  - application/pdf
"""


def documents(path):
    with gzip.open(path, "rt", encoding="utf-8") as f:
        text = f.read()
    return text.split("\n---\n")[1:]


def doc_id(doc):
    m = re.search(r"^ID: (.+)$", doc, re.M)
    return m.group(1).strip() if m else None


def main():
    if not os.path.isdir(SRC):
        sys.exit(f"make-fixture: {SRC} not found (run on the host)")
    found = {}
    for path in sorted(glob.glob(f"{SRC}/yaml/*_dep11_Components-amd64.yml.gz")):
        origin = os.path.basename(path).split("_dists_")[1].split("_dep11")[0]
        origin = "ubuntu-" + origin.replace("_", "-")
        for doc in documents(path):
            cid = doc_id(doc)
            if cid in REAL and cid not in found:
                found[cid] = (origin, doc.strip("\n"))
    missing = set(REAL) - set(found)
    if missing:
        sys.exit("make-fixture: not in the host catalog: " + " ".join(sorted(missing)))
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(f"{OUT}/yaml")
    with open(f"{OUT}/yaml/chandler-fixture.yml", "w", encoding="utf-8") as f:
        f.write(HEADER.format(origin="chandler-fixture"))
        for cid in sorted(found):
            f.write("---\n" + found[cid][1] + "\n")
    # The same component in a second catalog, as the archive has it in resolute and in
    # resolute-updates: the pool must list it once.
    with open(f"{OUT}/yaml/chandler-fixture-updates.yml", "w", encoding="utf-8") as f:
        f.write(HEADER.format(origin="chandler-fixture-updates"))
        f.write("---\n" + found["org.gnome.Evince"][1] + "\n")
    with open(f"{OUT}/yaml/chandler-fixture-handmade.yml", "w", encoding="utf-8") as f:
        f.write(HEADER.format(origin="chandler-fixture-handmade"))
        f.write(HANDMADE.split("\n", 1)[1])
    # Hand-written: a catalog whose origin climbs out of the icon directory. A cached icon is
    # found at icons/<origin>/<size>/<name>, so this one resolves to escape/64x64/ beside
    # icons/, where a real PNG waits. Chandler must not use it (plan §3.2).
    with open(f"{OUT}/yaml/chandler-fixture-escape.yml", "w", encoding="utf-8") as f:
        f.write(HEADER.format(origin="../escape"))
        f.write(ESCAPE)
    pngs = sorted(glob.glob(f"{SRC}/icons/*/64x64/*.png"))
    if not pngs:
        sys.exit("make-fixture: no 64x64 cached icon in the host catalog to use as bait")
    escape_png = pngs[0]
    os.makedirs(f"{OUT}/escape/64x64")
    shutil.copyfile(escape_png, f"{OUT}/escape/64x64/escape.png")
    icons = 0
    for cid, (origin, doc) in found.items():
        for name in set(re.findall(r"^\s*- name: (\S+\.png)$", doc, re.M)):
            for size_dir in glob.glob(f"{SRC}/icons/{origin}/*"):
                src = os.path.join(size_dir, name)
                if os.path.isfile(src):
                    for fixture_origin in ("chandler-fixture", "chandler-fixture-updates"):
                        dest = f"{OUT}/icons/{fixture_origin}/{os.path.basename(size_dir)}"
                        os.makedirs(dest, exist_ok=True)
                        shutil.copyfile(src, os.path.join(dest, name))
                        icons += 1
    print(f"make-fixture: {len(found)} real components, 5 hand-written, {icons} icon files")


if __name__ == "__main__":
    main()
