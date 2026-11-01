#!/usr/bin/env python3
"""check-version.py <chandler> <version>: `chandler --version` prints exactly "chandler <version>"."""
import re
import subprocess
import sys

exe, version = sys.argv[1], sys.argv[2]
# The configured version is the git hash (./build), never the placeholder.
if not re.fullmatch(r"[0-9a-f]{12}(-dirty)?", version):
    sys.exit(f"configured version {version!r} is not a 12-character git hash")
out = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30)
want = f"chandler {version}\n"
if out.returncode != 0 or out.stdout != want:
    sys.exit(f"--version: exit {out.returncode}, printed {out.stdout!r}, expected {want!r}")
print(f"ok {want.strip()}")
