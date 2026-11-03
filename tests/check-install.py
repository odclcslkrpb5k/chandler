#!/usr/bin/env python3
"""check-install.py <install.sh> <chandler> <data dir>: install.sh, from a staged directory
(CHANDLER_FROM, as ./build install runs it) into temporary prefixes.

What it pins (plan §6): the files land where the desktop finds them and name each other
correctly; the shim runs this Chandler; the installer never overwrites or removes a
gnome-software it didn't write, and writes no shim while another gnome-software is on PATH;
uninstall removes exactly what was installed.
"""
import os
import shutil
import subprocess
import sys
import tempfile

APP_ID = "io.github.odclcslkrpb5k.Chandler"
installer, chandler, data = sys.argv[1], sys.argv[2], sys.argv[3]
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"FAIL {what}")


BASE_PATH = ["/usr/bin", "/bin"]


def run(prefix, path_dirs=None, piped=False, **env):
    # The whole test environment, as `./build install` passes its own to install.sh: a
    # variable the build exports must not change what the installer does (it once did:
    # the build's version stamp and the installer's CHANDLER_VERSION shared a name).
    full = {
        **os.environ,
        "HOME": "/nonexistent",
        "PATH": ":".join([*(path_dirs if path_dirs is not None else [f"{prefix}/bin"]),
                          *BASE_PATH]),
        "CHANDLER_FROM": stage,
        "CHANDLER_PREFIX": prefix,
        **env,
    }
    if piped:  # as `curl ... | bash` runs it
        with open(installer, "rb") as script:
            return subprocess.run(["bash"], stdin=script, env=full, capture_output=True,
                                  text=True, timeout=60)
    return subprocess.run(["bash", installer], env=full, capture_output=True, text=True,
                          timeout=60)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text, mode=0o755):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    os.chmod(path, mode)


def files_under(prefix):
    return sorted(os.path.relpath(os.path.join(d, f), prefix)
                  for d, _, fs in os.walk(prefix) for f in fs)


# Hermetic (TS-6): a gnome-software on the base PATH would make every case skip the shim.
for d in BASE_PATH:
    if os.path.lexists(os.path.join(d, "gnome-software")):
        sys.exit(f"{d}/gnome-software exists: this test needs a PATH without one")
check("CHANDLER_BUILD_HASH" in os.environ, "the build's variables aren't in the test environment")

version = subprocess.run([chandler, "--version"], capture_output=True, text=True,
                         timeout=30).stdout.strip()
check(version.startswith("chandler "), f"--version printed {version!r}")

tmp = tempfile.mkdtemp(prefix="check-install-")
stage = os.path.join(tmp, "stage")
os.mkdir(stage)
shutil.copy(chandler, os.path.join(stage, "chandler"))
for f in (f"{APP_ID}.desktop.in", f"{APP_ID}.svg", "gnome-software.in"):
    shutil.copy(os.path.join(data, f), stage)

INSTALLED = sorted([
    "bin/chandler",
    "bin/gnome-software",
    f"share/applications/{APP_ID}.desktop",
    f"share/icons/hicolor/scalable/apps/{APP_ID}.svg",
])

# 1. A fresh install, piped as from curl: four files, each naming the others correctly.
p = os.path.join(tmp, "fresh")
r = run(p, piped=True)
check(r.returncode == 0, f"fresh install exit {r.returncode}: {r.stderr}")
check(files_under(p) == INSTALLED, f"fresh install wrote {files_under(p)}")
desktop = os.path.join(p, "share/applications", f"{APP_ID}.desktop")
v = subprocess.run(["desktop-file-validate", desktop], capture_output=True, text=True)
check(v.returncode == 0 and v.stdout == "", f"desktop-file-validate: {v.stdout}{v.stderr}")
entry = dict(line.split("=", 1) for line in read(desktop).splitlines() if "=" in line)
check(entry.get("Exec") == f"{p}/bin/chandler", f"Exec={entry.get('Exec')}")
check(entry.get("TryExec") == f"{p}/bin/chandler", f"TryExec={entry.get('TryExec')}")
check(entry.get("Icon") == APP_ID, f"Icon={entry.get('Icon')}")
shim = os.path.join(p, "bin/gnome-software")
s = subprocess.run([shim, "--version"], capture_output=True, text=True, timeout=30,
                   env={**os.environ, "PATH": "/usr/bin:/bin"})
check(s.stdout.strip() == version, f"the shim ran {s.stdout!r}, expected {version!r}")
check(os.stat(shim).st_mode & 0o777 == 0o755, "the shim isn't mode 755")

# 2. Again over it: the same files, the shim rewritten (it is ours).
write(shim, read(shim).replace(f"{p}/bin/chandler", "/old/chandler"))
r = run(p)
check(r.returncode == 0 and files_under(p) == INSTALLED, f"reinstall: {r.stderr}")
check(f"'{p}/bin/chandler'" in read(shim), "reinstall didn't update its own shim")

# 3. Someone else's gnome-software in the same place: untouched, and the rest installs.
p = os.path.join(tmp, "foreign")
foreign = "#!/bin/sh\necho mine\n"
write(f"{p}/bin/gnome-software", foreign)
r = run(p)
check(r.returncode == 0, f"foreign shim: exit {r.returncode}")
check(read(f"{p}/bin/gnome-software") == foreign, "overwrote a gnome-software it didn't write")
check("Chandler didn't write it" in r.stderr, f"foreign shim not reported: {r.stderr}")
# ... and uninstall leaves it, and an unrelated file, where they are.
write(f"{p}/bin/other", "#!/bin/sh\n")
r = run(p, CHANDLER_UNINSTALL="1")
check(r.returncode == 0, f"uninstall exit {r.returncode}: {r.stderr}")
check(files_under(p) == ["bin/gnome-software", "bin/other"],
      f"uninstall left {files_under(p)}")
check(read(f"{p}/bin/gnome-software") == foreign, "uninstall touched a foreign gnome-software")

# 4. A real gnome-software elsewhere on PATH: no shim on a fresh install ...
real = os.path.join(tmp, "usr-bin")
write(f"{real}/gnome-software", "#!/bin/sh\necho real\n")
p = os.path.join(tmp, "real-first")
r = run(p, path_dirs=[f"{p}/bin", real])
check(r.returncode == 0 and "would hide it" in r.stderr, f"fresh, real one on PATH: {r.stderr}")
check(not os.path.lexists(f"{p}/bin/gnome-software"), "shim written beside a real gnome-software")
# ... and an earlier shim of ours goes once one appears.
p = os.path.join(tmp, "real")
check(run(p).returncode == 0, "install before the real gnome-software")
check(os.path.exists(f"{p}/bin/gnome-software"), "no shim before the real gnome-software")
r = run(p, path_dirs=[f"{p}/bin", real])
check(r.returncode == 0, f"with a real gnome-software: exit {r.returncode}")
check(not os.path.exists(f"{p}/bin/gnome-software"), "shim kept beside a real gnome-software")
check("would hide it" in r.stderr, f"real gnome-software not reported: {r.stderr}")
check(read(f"{real}/gnome-software") == "#!/bin/sh\necho real\n", "touched the real one")

# 5. CHANDLER_NO_SHIM: everything but the shim, and an existing shim of ours stays.
p = os.path.join(tmp, "noshim")
r = run(p, CHANDLER_NO_SHIM="1")
check(r.returncode == 0 and files_under(p) == [f for f in INSTALLED if "gnome-software" not in f],
      f"CHANDLER_NO_SHIM wrote {files_under(p)}")
check(run(p).returncode == 0 and run(p, CHANDLER_NO_SHIM="1").returncode == 0, "noshim reruns")
check(files_under(p) == INSTALLED, f"CHANDLER_NO_SHIM removed the shim: {files_under(p)}")

# 5b. Other things at the shim's path are foreign too: a dangling symlink, a symlink to a
# shim, a directory, a file with the marker on another line. Each is left as it was.
for name, make in [
    ("dangling", lambda s: os.symlink("/nonexistent/gnome-software", s)),
    ("link-to-shim", lambda s: os.symlink(os.path.join(tmp, "fresh/bin/gnome-software"), s)),
    ("directory", lambda s: os.mkdir(s)),
    ("marker-line-3", lambda s: write(s, "#!/bin/sh\n#\n# chandler-shim: no\nexec true\n")),
]:
    p = os.path.join(tmp, "odd-" + name)
    s = f"{p}/bin/gnome-software"
    os.makedirs(f"{p}/bin")
    make(s)
    before = (os.path.islink(s), os.readlink(s) if os.path.islink(s) else None,
              os.path.isdir(s), read(s) if os.path.isfile(s) and not os.path.islink(s) else None)
    for extra in ({}, {"CHANDLER_UNINSTALL": "1"}):
        r = run(p, **extra)
        after = (os.path.islink(s), os.readlink(s) if os.path.islink(s) else None,
                 os.path.isdir(s), read(s) if os.path.isfile(s) and not os.path.islink(s) else None)
        check(r.returncode == 0 and after == before and os.path.lexists(s),
              f"{name} {extra}: changed to {after} (exit {r.returncode}: {r.stderr})")
        check(os.listdir(s) == [] if os.path.isdir(s) and not os.path.islink(s) else True,
              f"{name}: wrote into the directory")

# 5c. Its own directory spelt differently on PATH (a trailing slash) is not "another"
# gnome-software: the shim stays across runs instead of being removed by the second.
p = os.path.join(tmp, "spelling")
for _ in range(2):
    r = run(p, path_dirs=[f"{p}/bin/"])
    check(r.returncode == 0 and os.path.exists(f"{p}/bin/gnome-software"),
          f"shim lost with {p}/bin/ on PATH: {r.stderr}")

# 6. Uninstall after a clean install removes all four, the shim included.
p = os.path.join(tmp, "uninstall")
check(run(p).returncode == 0, "install before uninstall")
r = run(p, CHANDLER_UNINSTALL="1")
check(r.returncode == 0 and files_under(p) == [], f"uninstall left {files_under(p)}")

# 7. A prefix that would need quoting in the .desktop file or the shim is refused, unwritten.
for bad in ("relative/dir", os.path.join(tmp, "has space"), os.path.join(tmp, "q'uote")):
    r = run(bad)
    check(r.returncode != 0 and not os.path.exists(bad), f"prefix {bad!r} accepted")

# 8. A missing staged file stops it before anything is written.
p = os.path.join(tmp, "missing")
os.remove(os.path.join(stage, "gnome-software.in"))
r = run(p)
check(r.returncode != 0 and not os.path.exists(p), f"missing file: exit {r.returncode}")

shutil.rmtree(tmp)
if failures:
    sys.exit(f"{len(failures)} failure(s)")
print("ok install.sh")
