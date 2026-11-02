#!/usr/bin/env python3
"""Static analyzers behind a baseline that can only shrink.

  analysis.py run      run clang-tidy and cppcheck; write out/analysis/findings.txt
  analysis.py compare  judge findings.txt against the baseline; exit 1 on any difference

A finding is counted per (tool, check, file), never per line: lines move with every edit.
The baseline is tools/analysis-baseline.txt. The comparison fails when
  - any count is above the baseline committed at origin/main (out/analysis/origin-baseline.txt,
    written by ./build on the host), so one commit can't raise the baseline and add the
    finding; or above the working tree's baseline;
  - any count is below the working tree's baseline: a fixed finding must leave the baseline
    in the same commit, so it can only shrink.
Every tool's own exit status is ignored and its report parsed instead (scan-build, for one,
exits 0 with --status-bugs when its output is plist or sarif, whatever it found).

The Clang static analyzer runs as clang-tidy's clang-analyzer-* checks: run through
scan-build as well, every one of its findings here was one clang-tidy also reported.
"""

import collections
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

SRC = "/src"
OUT = "out/analysis"
BASELINE = "tools/analysis-baseline.txt"
ORIGIN = OUT + "/origin-baseline.txt"
FINDINGS = OUT + "/findings.txt"
ABSENT = "# origin/main has no baseline\n"     # what ./build writes when it has none


def rel(path, base=""):
    """Path relative to the repository, given one relative to `base` (itself relative to the
    repository); a path outside the repository stays absolute."""
    p = os.path.normpath(os.path.join(SRC, base, path))
    return os.path.relpath(p, SRC) if p.startswith(SRC + "/") else p


def sh(cmd, log, env=None):
    with open(log, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=SRC,
                       env={**os.environ, **(env or {})}, check=False)


# Errors count as findings too: a clang-diagnostic error (an unknown check, a header that
# doesn't parse) would otherwise leave a file unanalysed with nothing to show for it.
TIDY_LINE = re.compile(r"^(.+?):(\d+):\d+: (?:warning|error): (.*) \[([A-Za-z0-9.,_-]+)\]$")
# A tool error starts its line: "path:line:col: error:" or ".clang-tidy" trouble ("Error
# parsing ..."). Not anywhere in a line: clang-tidy quotes source under each diagnostic, and
# a quoted string can say "error:" (found by Quoin, 2026-11-01).
TIDY_ERROR = re.compile(r"^(\S+?):(\d+):\d+: (?:fatal )?error: (.*)$|^(Error .*)$")
TIDY_RAN = re.compile(r"^\[\s*\d+/\d+\]\[[0-9.]+s\] \S*clang-tidy\S* .* (/src/\S+)$")
TIDY_SCOPE = re.compile(r"^/src/(src|tests)/")


def tidy_families():
    """Every family .clang-tidy turns on selects at least one check. A misspelt name
    ("bugprne-*") is silently ignored by clang-tidy, which then checks less than it says
    (found by Quoin, 2026-11-01)."""
    text = open(os.path.join(SRC, ".clang-tidy")).read()
    block = re.search(r"^Checks: >\n((?:  .*\n)+)", text, re.M)
    if block is None:
        sys.exit("analysis: no 'Checks: >' block in .clang-tidy")
    names = [n.strip().rstrip(",") for n in block.group(1).split("\n") if n.strip()]
    positive = [n for n in names if not n.startswith("-")]
    if not positive:
        sys.exit("analysis: .clang-tidy enables no checks")
    for name in names:
        pattern = name.lstrip("-")
        if pattern == "*":
            continue
        out = subprocess.run(["clang-tidy", "-checks=-*," + pattern, "--list-checks"],
                             capture_output=True, text=True, cwd="/tmp", check=False).stdout
        listed = [l.strip() for l in out.splitlines()[1:] if l.strip()]
        if not listed:
            sys.exit(f"analysis: .clang-tidy names {name}, which selects no clang-tidy check")
    return len(names)


def clang_tidy(db):
    """clang-tidy with the checks in .clang-tidy, over every file in the clang build."""
    families = tidy_families()
    log = OUT + "/clang-tidy.log"
    sh(["run-clang-tidy", "-p", db, "-quiet", "-j", str(os.cpu_count() or 2),
        "-header-filter=^/src/(src|tests)/", r"^/src/(src|tests)/"], log)
    found = []
    ran = set()
    for line in open(os.path.join(SRC, log)):
        line = line.strip()
        m = TIDY_LINE.match(line)
        if m:
            for check in m.group(4).split(","):
                found.append(("clang-tidy", check, rel(m.group(1), db), m.group(3)))
            continue
        m = TIDY_ERROR.match(line)
        if m:
            where = rel(m.group(1), db) if m.group(1) else ".clang-tidy"
            found.append(("clang-tidy", "error", where, m.group(3) or m.group(4)))
            continue
        m = TIDY_RAN.match(line)
        if m:
            ran.add(rel(m.group(1)))
    # Like cppcheck below: prove it ran on every file in scope, not on none (TS-6).
    with open(os.path.join(SRC, db, "compile_commands.json")) as f:
        wanted = {rel(os.path.join(e["directory"], e["file"])) for e in json.load(f)}
    wanted = {w for w in wanted if TIDY_SCOPE.match(os.path.join(SRC, w))}
    print(f"analysis: clang-tidy ran on {len(ran)} files; .clang-tidy's {families} entries "
          "each select checks")
    if not wanted or wanted != ran:
        sys.exit("analysis: clang-tidy did not run on exactly the files in scope; missing: "
                 + " ".join(sorted(wanted - ran)) + "; unexpected: " + " ".join(sorted(ran - wanted)))
    return sorted(set(found))


def cppcheck(db):
    """cppcheck over every file in the compile database, and proof that it checked them."""
    log = OUT + "/cppcheck.xml"
    progress = OUT + "/cppcheck.log"
    commands = os.path.join(db, "compile_commands.json")
    cmd = ["cppcheck", "--project=" + commands, "--xml", "-j", str(os.cpu_count() or 2),
           "--library=gtk", "--library=posix", "--library=gnu",
           "--library=tools/cppcheck-glib.cfg",
           "--enable=warning,style,performance,portability",
           "--suppressions-list=tools/cppcheck-suppressions.txt",
           "--error-exitcode=0"]
    with open(os.path.join(SRC, log), "w") as xml, open(os.path.join(SRC, progress), "w") as out:
        subprocess.run(cmd, stdout=out, stderr=xml, cwd=SRC, check=False)
    # Given a path it can't use, cppcheck checks nothing, writes nothing and exits 0.
    with open(os.path.join(SRC, commands)) as f:
        wanted = {rel(os.path.join(e["directory"], e["file"])) for e in json.load(f)}
    checked = set()
    for line in open(os.path.join(SRC, progress)):
        m = re.match(r"^Checking (\S+) ", line)
        if m:
            checked.add(rel(m.group(1)))
    if not wanted or wanted - checked:
        sys.exit("analysis: cppcheck did not check: " + " ".join(sorted(wanted - checked)))
    found = []
    for err in ET.parse(os.path.join(SRC, log)).getroot().iter("error"):
        loc = err.find("location")
        path = rel(loc.get("file")) if loc is not None else "(none)"
        found.append(("cppcheck", err.get("id"), path, err.get("msg")))
    return found


def run():
    os.makedirs(os.path.join(SRC, OUT), exist_ok=True)
    db = OUT + "/clang"
    if not os.path.exists(os.path.join(SRC, db, "build.ninja")):
        sh(["meson", "setup", db, "-Dbuildtype=debug"], OUT + "/clang-setup.log", {"CC": "clang"})
    # A clean clang build first: clang-tidy and cppcheck read its compile_commands.json,
    # and generated headers must exist.
    r = subprocess.run(["ninja", "-C", db], cwd=SRC, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("analysis: clang build failed\n" + r.stdout[-3000:])

    # Findings outside src/ and tests/ count too: cppcheck failing to parse a system header
    # stops it analysing the file that included it, which must not pass quietly.
    found = clang_tidy(db) + cppcheck(db)
    lines = [f"{tool}\t{check}\t{path}\t{' '.join(msg.split())}" for tool, check, path, msg in found]
    with open(os.path.join(SRC, FINDINGS), "w") as f:
        f.write("\n".join(sorted(lines)) + ("\n" if lines else ""))
    print(f"analysis: {len(lines)} findings in {FINDINGS}")


# Findings that can never be fixed, with the exact count and the reason (the appendix's
# CD-8: these leave the baseline, which is for violations to burn down, and live here).
# A count that falls fails too, so a stale entry can't linger.
PERMANENT = {
    ("cppcheck", "constParameterPointer", "src/window.c"):
        (1, "GObject _class_init: its signature is declared by G_DEFINE_TYPE"),
    ("cppcheck", "constParameterPointer", "src/catalog.c"):
        (1, "GObject _init: its signature is declared by G_DEFINE_TYPE"),
    ("cppcheck", "constParameterPointer", "vendor/quoin/tests/test-lifetime.c"):
        (2, "vendored unchanged (RP-4); its GObject _init and _class_init, as above"),
}


def counts_of_findings():
    c = collections.Counter()
    for line in open(os.path.join(SRC, FINDINGS)):
        tool, check, path, _ = line.rstrip("\n").split("\t", 3)
        c[(tool, check, path)] += 1
    return c


def without_permanent(found):
    problems = []
    for key, (count, _) in PERMANENT.items():
        if found[key] < count:
            problems.append(f"FIXED    {' '.join(key)}: {found[key]} found, PERMANENT says {count}: "
                            "lower it in tools/analysis.py")
        found[key] = max(0, found[key] - count)
    return problems


def read_baseline(path):
    c = collections.Counter()
    full = os.path.join(SRC, path)
    if not os.path.exists(full):
        return None
    for n, line in enumerate(open(full), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 4 or not parts[3].isdigit() or int(parts[3]) < 1:
            sys.exit(f"{path}:{n}: expected 'tool check file count', got: {line}")
        key = (parts[0], parts[1], parts[2])
        if key in c:
            sys.exit(f"{path}:{n}: duplicate entry")
        c[key] = int(parts[3])
    return c


def compare():
    found = counts_of_findings()
    permanent_problems = without_permanent(found)
    tree = read_baseline(BASELINE) or collections.Counter()
    if not os.path.exists(os.path.join(SRC, ORIGIN)):
        sys.exit(f"analysis: {ORIGIN} is missing; ./build writes it from origin/main")
    if open(os.path.join(SRC, ORIGIN)).read() == ABSENT:
        # Only while the commit that adds the baseline is not yet at origin.
        print("analysis: origin/main has no baseline yet; judging by the working tree's")
        origin = tree
    else:
        origin = read_baseline(ORIGIN)
    problems = list(permanent_problems)
    for key in sorted(set(found) | set(tree) | set(origin)):
        n, t, o = found[key], tree[key], origin[key]
        name = " ".join(key)
        if n > o:
            problems.append(f"NEW      {name}: {n} found, {o} allowed by origin's baseline")
        elif n > t:
            problems.append(f"NEW      {name}: {n} found, {t} in the baseline")
        elif n < t:
            problems.append(f"FIXED    {name}: {n} found, baseline says {t}: lower the baseline")
    if problems:
        print("analysis: the findings differ from the baseline (tools/analysis.py)")
        for p in problems:
            print("  " + p)
        print(f"  details: {FINDINGS}")
        sys.exit(1)
    print(f"analysis: {sum(found.values())} findings, all in the baseline")


if __name__ == "__main__":
    {"run": run, "compare": compare}.get(sys.argv[1] if len(sys.argv) > 1 else "",
                                         lambda: sys.exit(__doc__))()
