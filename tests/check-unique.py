#!/usr/bin/env python3
"""check-unique.py <chandler>: one Chandler at a time (plan §3.3).

A first launch takes the application's bus name and stays; a second launch, with GTK's
exact arguments, hands its command line to the first and exits 0 at once; the first keeps
running, and its window now shows the second launch's search (read from the application's
disabled "query" action, whose state is the window's search). Needs DISPLAY and DBUS_SESSION_BUS_ADDRESS (tools/in-container.sh provides both).
"""
import os
import signal
import subprocess
import sys
import time

import dbus

NAME = "io.github.odclcslkrpb5k.Chandler"
exe = sys.argv[1]
for var in ("DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
    if not os.environ.get(var):
        sys.exit(f"check-unique: {var} is not set")

bus = dbus.SessionBus()


def query():
    actions = bus.get_object(NAME, "/io/github/odclcslkrpb5k/Chandler")
    _, _, state = actions.Describe("query", dbus_interface="org.gtk.Actions")
    return str(state[0]) if state else None


def wait_query(want):
    deadline = time.monotonic() + 30
    while (got := query()) != want:
        if time.monotonic() > deadline:
            sys.exit(f"check-unique: the window's search is {got!r}, not {want!r}")
        time.sleep(0.05)

if bus.name_has_owner(NAME):
    sys.exit(f"check-unique: {NAME} is already owned before the test starts")

first = subprocess.Popen([exe, "--search=application/pdf"])
try:
    deadline = time.monotonic() + 30
    while not bus.name_has_owner(NAME):
        if first.poll() is not None:
            sys.exit(f"check-unique: the first chandler exited ({first.returncode}) before owning {NAME}")
        if time.monotonic() > deadline:
            sys.exit(f"check-unique: the first chandler never owned {NAME}")
        time.sleep(0.05)

    wait_query("application/pdf")
    second = subprocess.run([exe, "--search=image/svg+xml"], timeout=30)
    if second.returncode != 0:
        sys.exit(f"check-unique: the second chandler exited {second.returncode}")
    if first.poll() is not None:
        sys.exit(f"check-unique: the first chandler exited ({first.returncode}) when the second ran")
    wait_query("image/svg+xml")
    print("ok: second launch handed over to the first and exited 0; first still running and "
          "showing the second's search")
finally:
    if first.poll() is None:
        first.send_signal(signal.SIGTERM)
        first.wait(timeout=30)
