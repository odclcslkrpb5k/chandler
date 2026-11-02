#!/usr/bin/env python3
"""check-list.py <chandler>: `chandler --list` end to end, against the image's decoy system
catalog (Dockerfile), with no PackageKit (the container has no system bus).

The decoy's summary carries an OSC escape (ESC ] ... BEL) and a bidirectional override; none
of it may reach the terminal. Each line has five tab-separated fields.
"""
import os
import subprocess
import sys
import tempfile

exe = sys.argv[1]
env = dict(os.environ, XDG_CACHE_HOME=tempfile.mkdtemp(prefix="chandler-list-"))

out = subprocess.run([exe, "--list", "application/pdf"], capture_output=True, env=env, timeout=60)
if out.returncode != 0:
    sys.exit(f"--list application/pdf: exit {out.returncode}\n{out.stderr.decode(errors='replace')}")
lines = out.stdout.decode("utf-8").splitlines()
decoy = [l for l in lines if "chandler.decoy.System" in l]
if len(decoy) != 1:
    sys.exit(f"expected the decoy app once, got {len(decoy)}: {lines!r}")
fields = decoy[0].split("\t")
if len(fields) != 5 or fields[0] != "unknown" or fields[2] != "decoy-system-viewer":
    sys.exit(f"malformed line: {fields!r}")
for bad in ("\x1b", "\x07", chr(0x202E)):
    if bad in out.stdout.decode("utf-8"):
        sys.exit(f"U+{ord(bad):04X} reached the terminal")
if "]0;pwned" not in fields[4]:
    sys.exit(f"the summary's visible text went missing: {fields[4]!r}")
if b"PackageKit" not in out.stderr:
    sys.exit("no word on stderr that PackageKit is unavailable")

none = subprocess.run([exe, "--list", "application/x-chandler-nothing"], capture_output=True,
                      env=env, timeout=60)
if none.returncode != 1:
    sys.exit(f"--list of a type nothing opens: exit {none.returncode}, expected 1")
print("ok: --list printed the decoy, sanitised; exit 1 for a type with no apps")
