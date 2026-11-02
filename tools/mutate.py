#!/usr/bin/env python3
"""Plant bugs one at a time and require a named test to catch each (TS-5).

  tools/mutate.py MUTATIONS.py

MUTATIONS.py defines MUTATIONS, a list of (file, old, new, test): `old` must occur exactly
once in `file`; it is replaced by `new`, `./build debug` runs, and the file is restored.

Each mutation ends as one of:
  CAUGHT    the build compiled and `test` failed (meson: FAIL or ERROR)
  SURVIVED  the build compiled and `test` passed: the test does not guard this code
  INVALID   `test` never ran (the mutation broke the build, or the name is wrong): it
            proves nothing, and is reported as a failure of the run, never as a pass
Exit status 0 only when every mutation was CAUGHT.
"""
import runpy
import subprocess
import sys


def run_one(path, old, new, test):
    orig = open(path, encoding="utf-8").read()
    if orig.count(old) != 1:
        return "INVALID", f"pattern found {orig.count(old)} times"
    open(path, "w", encoding="utf-8").write(orig.replace(old, new))
    try:
        r = subprocess.run(["./build", "debug"], capture_output=True, text=True, check=False)
    finally:
        open(path, "w", encoding="utf-8").write(orig)
    lines = [l for l in (r.stdout + r.stderr).splitlines() if f"chandler:{test} " in l]
    if not lines:
        return "INVALID", f"test {test} never ran (build failed, or no such test)"
    if any(" FAIL " in l or " ERROR " in l for l in lines):
        log = open("out/debug/meson-logs/testlog.txt", encoding="utf-8", errors="replace").read()
        why = [l.strip() for l in log.splitlines() if l.startswith("not ok")][:1]
        return "CAUGHT", (why or lines)[0][:200]
    return "SURVIVED", lines[0].strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    mutations = runpy.run_path(sys.argv[1])["MUTATIONS"]
    verdicts = []
    for i, (path, old, new, test) in enumerate(mutations, 1):
        verdict, detail = run_one(path, old, new, test)
        verdicts.append(verdict)
        print(f"mutation {i} ({path} -> {test}): {verdict}: {detail}")
    caught = verdicts.count("CAUGHT")
    print(f"mutate: {caught} of {len(verdicts)} caught")
    sys.exit(0 if caught == len(verdicts) else 1)


if __name__ == "__main__":
    main()
