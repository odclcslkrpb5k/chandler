#!/usr/bin/env python3
"""Run a test binary once per PackageKit scenario, each against its own fake daemon.

  with-fake-pk.py <fake-packagekit.py> <scenarios-dir> <test-binary>

The binary lists its cases with --scenarios; they must be exactly the scenario files (TS-6:
a case without a scenario, or a scenario without a case, fails the run rather than being
skipped). For each case:
  - a private bus is started, and given to the binary as DBUS_SYSTEM_BUS_ADDRESS. It is the
    only system bus there is: the container has none (compose.yaml, in-container.sh).
  - the fake (python-dbusmock with tests/fake-packagekit.py) serves the scenario on it,
    unless the scenario says "daemon": false.
  - `<test-binary> <case>` runs; then the fake's Verdict() must be empty: the client made
    exactly the calls the scenario lists, no more and no fewer.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

import dbus

FAKE_NAME = "org.freedesktop.PackageKit"


def start_bus(tmp):
    bus = subprocess.Popen(
        ["dbus-daemon", "--session", "--nofork", "--print-address=1",
         f"--address=unix:dir={tmp}"],
        stdout=subprocess.PIPE, text=True)
    address = bus.stdout.readline().strip()
    # TS-6: assert what was selected. A test must never reach a real system bus.
    if not address.startswith(f"unix:path={tmp}/") or "/run/dbus" in address:
        bus.kill()
        sys.exit(f"with-fake-pk: unexpected bus address {address!r}")
    return bus, address


def wait_for_name(address, proc, log):
    conn = dbus.bus.BusConnection(address)
    for _ in range(200):
        if conn.name_has_owner(FAKE_NAME):
            return conn
        if proc.poll() is not None:
            sys.exit(f"with-fake-pk: the fake exited ({proc.returncode}):\n{log.read_text()}")
        time.sleep(0.05)
    sys.exit("with-fake-pk: the fake never claimed its name")


def run_case(template, scenario, binary, name):
    spec = json.loads(scenario.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="chandler-bus-") as tmp:
        bus, address = start_bus(tmp)
        # TMPDIR: packagekit-glib2 makes its debconf socket there for every install
        # transaction, named after the transaction ("/1_fake"), which every fake shares.
        env = dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=address, CHANDLER_FAKE_PK=name, TMPDIR=tmp)
        fake, conn, log = None, None, pathlib.Path(tmp) / "fake.log"
        try:
            if spec.get("daemon", True):
                fake = subprocess.Popen(
                    [sys.executable, "-m", "dbusmock", "--system", "-t", str(template),
                     "-p", json.dumps({"scenario": str(scenario)}), "-l", str(log)], env=env)
                conn = wait_for_name(address, fake, log)
            status = subprocess.run([binary, name], env=env, check=False).returncode
            verdict = ""
            if conn is not None:
                verdict = str(conn.get_object(FAKE_NAME, "/org/freedesktop/PackageKit").Verdict(
                    dbus_interface="io.github.odclcslkrpb5k.Chandler.FakePackageKit"))
        finally:
            for proc in (fake, bus):
                if proc is not None:
                    proc.terminate()
                    proc.wait()
        problems = []
        if status != 0:
            problems.append(f"the test exited {status}")
        if verdict:
            problems.append("the fake: " + verdict.replace("\n", "\n    "))
        if problems and log.exists():
            problems.append("fake's log:\n" + log.read_text())
        return problems


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    template, scenarios, binary = (pathlib.Path(a) for a in sys.argv[1:])
    cases = subprocess.run([binary, "--scenarios"], capture_output=True, text=True,
                           check=True).stdout.split()
    files = sorted(p.stem for p in scenarios.glob("*.json"))
    if sorted(cases) != files or not cases:
        sys.exit(f"with-fake-pk: cases {sorted(cases)} != scenario files {files}")
    failed = 0
    for name in cases:
        problems = run_case(template.resolve(), (scenarios / f"{name}.json").resolve(), binary, name)
        print(f"{'FAIL' if problems else 'ok'}: {name}")
        for p in problems:
            print("    " + p)
        failed += bool(problems)
    print(f"with-fake-pk: {len(cases) - failed} of {len(cases)} scenarios passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
