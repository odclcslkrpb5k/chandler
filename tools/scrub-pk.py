#!/usr/bin/env python3
"""Scrub a PackageKit recording into test transcripts (plan §1.2 item 3, §4.2).

  tools/scrub-pk.py out/pk-record tests/data/pk/recorded

Reads <record>/bus.txt (sudo dbus-monitor's text output) and <record>/steps.txt (one line per
step of tools/record-pk.sh), and writes one <step>.json per step. Each pk-drive run is one
bus connection, so the n-th client to call CreateTransaction is the n-th step.

What is kept is an allowlist (DP-1): the daemon's listed root properties, and per transaction
the method called with its arguments, then the listed signals and property changes in order.
Everything else is dropped: senders, serials, times, SetHints (it names the user's runtime
directory), polkit traffic. A transaction member that is not listed stops the scrub, so a
new kind of message is looked at before it can reach the repository.
"""
import json
import pathlib
import re
import sys

ROOT_PROPERTIES = {"VersionMajor", "VersionMinor", "VersionMicro", "BackendName",
                   "BackendDescription", "Roles", "Groups", "Filters", "MimeTypes", "Locked",
                   "NetworkState", "DistroId"}
TRANSACTION_PROPERTIES = {"Role", "Status", "Percentage", "AllowCancel", "CallerActive",
                          "TransactionFlags", "LastPackage", "ElapsedTime", "RemainingTime",
                          "Speed", "DownloadSizeRemaining"}
CALLS = {"Resolve", "InstallPackages"}
SIGNALS = {"Package", "Packages", "ItemProgress", "ErrorCode", "Finished", "Destroy",
           "EulaRequired", "RepoSignatureRequired", "MediaChangeRequired", "RequireRestart"}
DROPPED = {"SetHints", "GetAll"}
# Steps whose recording isn't what the step says, so not committed (plan §1.2 item 3).
MISRECORDED = {"05-locked": "the network was still off: a second no-network, no lock"}  # GetAll's reply is read separately, for the root only

HEADER = re.compile(r"^(method call|method return|signal|error) time=\S+ sender=(\S+) -> "
                    r"destination=(.+?) serial=\d+(?: reply_serial=(\d+))?"
                    r"(?: path=([^;]*); interface=([^;]*); member=(\w+))?")
SCALAR = re.compile(r"^(u?int(?:16|32|64)|byte|double|boolean|string|object path|signature) (.*)$")


def parse_value(lines, i):
    """One value starting at lines[i]; returns (value, next index)."""
    line = lines[i].strip()
    if line.startswith("variant"):
        lines[i] = line[len("variant"):].strip()
        return parse_value(lines, i)
    for opener, closer in (("array [", "]"), ("struct {", "}"), ("dict entry(", ")")):
        if line == opener:
            items, i = [], i + 1
            while lines[i].strip() != closer:
                value, i = parse_value(lines, i)
                items.append(value)
            if opener == "dict entry(":
                return tuple(items), i + 1
            return items, i + 1
    m = SCALAR.match(line)
    if m is None:
        sys.exit(f"scrub-pk: can't read value {line!r}")
    kind, text = m.groups()
    if kind in ("string", "object path", "signature"):
        if not (text.startswith('"') and text.endswith('"')):
            sys.exit(f"scrub-pk: unterminated string {line!r}")
        return text[1:-1], i + 1
    if kind == "boolean":
        return text == "true", i + 1
    if kind == "double":
        return float(text), i + 1
    return int(text), i + 1


def parse_body(lines):
    values, i = [], 0
    while i < len(lines):
        if not lines[i].strip():
            i += 1
            continue
        value, i = parse_value(lines, i)
        values.append(value)
    return values


def messages(text):
    for chunk in re.split(r"\n(?=(?:method call|method return|signal|error) time=)", text):
        lines = chunk.split("\n")
        m = HEADER.match(lines[0])
        if m is None:
            continue
        kind, sender, dest, reply, path, iface, member = m.groups()
        yield {"kind": kind, "sender": sender, "dest": dest, "reply": reply, "path": path,
               "iface": iface, "member": member, "body": lines[1:]}


def scrub(record):
    text = (record / "bus.txt").read_text(encoding="utf-8")
    steps = [line.split(" pk-drive ", 1)[1].strip() for line in
             (record / "steps.txt").read_text(encoding="utf-8").splitlines() if line.startswith("### ")]
    names = sorted(p.stem for p in record.glob("[0-9][0-9]-*.txt"))
    if len(names) != len(steps):
        sys.exit(f"scrub-pk: {len(steps)} steps in steps.txt but {len(names)} step logs")

    clients, owner_of, properties = [], {}, None
    root_getall = {}  # (client, serial) of a root GetAll call
    transactions = {}  # path -> transaction
    for msg in messages(text):
        member, path = msg["member"], msg["path"]
        if msg["kind"] == "method call" and member == "CreateTransaction":
            if msg["sender"] not in clients:
                clients.append(msg["sender"])
            continue
        if msg["kind"] == "method call" and member == "GetAll" and path == "/org/freedesktop/PackageKit":
            root_getall[msg["sender"]] = True
            continue
        if msg["kind"] == "method return" and properties is None and root_getall.get(msg["dest"]):
            body = parse_body(msg["body"])
            if body and isinstance(body[0], list) and any(k == "VersionMajor" for k, _ in body[0]):
                properties = {k: v for k, v in body[0] if k in ROOT_PROPERTIES}
            continue
        if path is None or not re.fullmatch(r"/\d+_[a-z]+", path):
            continue
        if msg["iface"] == "org.freedesktop.PackageKit.Transaction" and msg["kind"] == "method call":
            if member in DROPPED:
                continue
            if member == "Cancel" and path in transactions:
                transactions[path]["events"].append(["Cancel (client)", []])
                continue
            if member not in CALLS:
                sys.exit(f"scrub-pk: unlisted call {member} on a transaction: look at it first")
            owner_of[path] = msg["sender"]
            flags, items = parse_body(msg["body"])
            transactions[path] = {"call": member, "flags": flags, "args": items, "events": []}
            continue
        if path not in transactions:
            continue
        events = transactions[path]["events"]
        if member == "PropertiesChanged":
            _, changed, _ = parse_body(msg["body"])
            kept = {k: v for k, v in changed if k in TRANSACTION_PROPERTIES}
            if kept:
                events.append(["properties", kept])
        elif member in SIGNALS:
            events.append([member, parse_body(msg["body"])])
        elif msg["kind"] == "signal":
            sys.exit(f"scrub-pk: unlisted signal {member} on a transaction: look at it first")

    if properties is None:
        sys.exit("scrub-pk: no root GetAll reply found")
    if len(clients) != len(steps):
        sys.exit(f"scrub-pk: {len(clients)} clients on the bus but {len(steps)} steps")
    out = {}
    for name, step, client in zip(names, steps, clients):
        out[name] = {
            "source": f"recorded on Ubuntu 26.04 (plan §1.2 item 3): pk-drive {step}",
            "properties": properties,
            "transactions": [t for p, t in transactions.items() if owner_of[p] == client],
        }
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    record, dest = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    dest.mkdir(parents=True, exist_ok=True)
    for name, data in scrub(record).items():
        if name in MISRECORDED:
            print(f"scrub-pk: {name} skipped: {MISRECORDED[name]}")
            continue
        (dest / f"{name}.json").write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n",
                                          encoding="utf-8")
        print(f"scrub-pk: {dest}/{name}.json: {len(data['transactions'])} transactions")


if __name__ == "__main__":
    main()
