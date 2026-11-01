#!/usr/bin/env python3
"""Vendored code is copied, never edited (RP-4), and this proves it.

  check-vendor.py            check every vendor/<name>/ against its MANIFEST
  check-vendor.py self-test  plant each kind of drift in a copy and require it to be caught

A MANIFEST lists "sha256 mode path" per file (comment lines start with #). The check fails on
a file whose content or mode differs, a listed file that is missing, and a file present but
not listed. Update a vendored copy by copying again from upstream and rewriting the
MANIFEST; a fix goes to upstream first.
"""

import hashlib
import os
import shutil
import stat
import sys

ROOT = "vendor"


def manifest(vdir):
    entries = {}
    for n, line in enumerate(open(os.path.join(vdir, "MANIFEST")), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 3 or len(parts[0]) != 64 or not parts[1].isdigit():
            sys.exit(f"{vdir}/MANIFEST:{n}: expected 'sha256 mode path', got: {line}")
        if parts[2] in entries:
            sys.exit(f"{vdir}/MANIFEST:{n}: {parts[2]} listed twice")
        entries[parts[2]] = (parts[0], parts[1])
    return entries


def present(vdir):
    found = set()
    for base, _, files in os.walk(vdir):
        for f in files:
            rel = os.path.relpath(os.path.join(base, f), vdir)
            if rel != "MANIFEST":
                found.add(rel)
    return found


def problems_in(vdir):
    problems = []
    entries = manifest(vdir)
    if not entries:
        problems.append(f"{vdir}: MANIFEST lists no files")
    for rel in sorted(present(vdir) - set(entries)):
        problems.append(f"{vdir}/{rel}: not in MANIFEST")
    for rel, (digest, mode) in sorted(entries.items()):
        path = os.path.join(vdir, rel)
        if not os.path.isfile(path):
            problems.append(f"{vdir}/{rel}: listed but missing")
            continue
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() != digest:
                problems.append(f"{vdir}/{rel}: content differs from MANIFEST (edited?)")
        actual = format(stat.S_IMODE(os.stat(path).st_mode), "o")
        if actual != mode:
            problems.append(f"{vdir}/{rel}: mode {actual}, MANIFEST says {mode}")
    return problems


def vendor_dirs(root):
    return sorted(os.path.join(root, d) for d in os.listdir(root)
                  if os.path.isdir(os.path.join(root, d)))


def check():
    dirs = vendor_dirs(ROOT)
    if not dirs:
        sys.exit("check-vendor: no vendored directories found; run from the repository root")
    problems = []
    files = 0
    for vdir in dirs:
        problems += problems_in(vdir)
        files += len(manifest(vdir))
    for p in problems:
        print("  " + p)
    if problems:
        sys.exit(f"check-vendor: {len(problems)} problems")
    print(f"check-vendor: {len(dirs)} vendored trees, {files} files, all as recorded")


def self_test():
    dirs = vendor_dirs(ROOT)
    if not dirs:
        sys.exit("check-vendor self-test: nothing vendored to copy")
    src = dirs[0]
    scratch = os.path.join("out", "check-vendor-self-test")
    first = sorted(manifest(src))[0]

    def edit(d):
        with open(os.path.join(d, first), "ab") as f:
            f.write(b"\n")

    def chmod(d):
        os.chmod(os.path.join(d, first), 0o755)

    def extra(d):
        open(os.path.join(d, "unlisted.c"), "w").close()

    def remove(d):
        os.remove(os.path.join(d, first))

    failed = 0
    for name, plant in [("unchanged", None), ("edited", edit), ("mode", chmod),
                        ("unlisted", extra), ("missing", remove)]:
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.copytree(src, scratch)
        if plant:
            plant(scratch)
        got = len(problems_in(scratch))
        if (got == 0) != (plant is None):
            failed += 1
            print(f"  {name}: {got} problems reported")
    shutil.rmtree(scratch, ignore_errors=True)
    if failed:
        sys.exit(f"check-vendor self-test: {failed} of 5 cases wrong")
    print("check-vendor self-test: 5 cases")


if __name__ == "__main__":
    {"self-test": self_test}.get(sys.argv[1] if len(sys.argv) > 1 else "", check)()
