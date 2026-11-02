"""A fake PackageKit daemon, as a python-dbusmock template (plan §4.3).

Hand-written: dbusmock has no PackageKit template, and is used here only for the D-Bus
plumbing (Substitute). The behaviour is replayed from a scenario file, never modelled:

  {"about": "...",
   "transactions": [<transaction>, ...]}

A transaction is either written inline, with a "hand-written" key saying why no recording
could give it, or taken from a recording ({"recorded": "01-simulate", "index": 0}), which
tools/scrub-pk.py made from the real daemon's traffic:

  {"call": "Resolve", "flags": 2424840, "args": ["hello"],
   "events": [["properties", {"Status": 2}], ["Packages", [[[2, "id", "summary"]]]],
              ["Finished", [1, 711]], ...]}

A call is answered by the first unused transaction with the same method, flags and arguments
(in any order). A transaction may list "expect_hints": each must be among the hints the client
sent with SetHints on it ("interactive=true": only an install may prompt for a password).

The same process also stands in for the two services Chandler asks after an install, to
revoke polkit's kept authorization (plan §3.3): logind's "auto" session (Id SESSION_ID) and
polkit's RevokeTemporaryAuthorizations, whose calls must number the scenario's "revokes"
(default 0) and name that session. With "polkit": false neither object exists, so both calls
fail as they would on a system without them. The root properties come from the first recording. Everything the client did
that the scenario didn't foresee, and every transaction it didn't use, is reported by
Verdict() on the root object, which tests/with-fake-pk.py calls when the client has finished:
a test passes only when the client made exactly the calls its scenario lists.
"""
import json
import pathlib

import dbus
import dbus.service
import dbusmock
from gi.repository import GLib

BUS_NAME = "org.freedesktop.PackageKit"
MAIN_OBJ = "/org/freedesktop/PackageKit"
MAIN_IFACE = "org.freedesktop.PackageKit"
SYSTEM_BUS = True
TX_IFACE = "org.freedesktop.PackageKit.Transaction"
CHECK_IFACE = "io.github.odclcslkrpb5k.Chandler.FakePackageKit"

# D-Bus types of the properties (dbus-python would otherwise send every int as int32).
TYPES = {
    "VersionMajor": dbus.UInt32, "VersionMinor": dbus.UInt32, "VersionMicro": dbus.UInt32,
    "BackendName": dbus.String, "BackendDescription": dbus.String,
    "BackendAuthor": dbus.String, "Roles": dbus.UInt64, "Groups": dbus.UInt64,
    "Filters": dbus.UInt64, "Locked": dbus.Boolean, "NetworkState": dbus.UInt32,
    "DistroId": dbus.String,
    "Role": dbus.UInt32, "Status": dbus.UInt32, "LastPackage": dbus.String,
    "Uid": dbus.UInt32, "Sender": dbus.String, "Percentage": dbus.UInt32,
    "AllowCancel": dbus.Boolean, "CallerActive": dbus.Boolean, "ElapsedTime": dbus.UInt32,
    "RemainingTime": dbus.UInt32, "Speed": dbus.UInt32,
    "DownloadSizeRemaining": dbus.UInt64, "TransactionFlags": dbus.UInt64,
}
SIGNATURES = {
    "Package": "uss", "Packages": "a(uss)", "ItemProgress": "suu", "ErrorCode": "us",
    "Finished": "uu", "Destroy": "", "EulaRequired": "ssss", "RepoSignatureRequired": "ssssssu",
    "MediaChangeRequired": "uss", "RequireRestart": "us",
}
# pk-enum.h (packagekit-glib2 1.3.4)
ERROR_INTERNAL, ERROR_CANCELLED = 4, 17
SESSION_ID = "c7"
LOGIND_NAME, POLKIT_NAME = "org.freedesktop.login1", "org.freedesktop.PolicyKit1"
POLKIT_IFACE = "org.freedesktop.PolicyKit1.Authority"
EXIT_FAILED, EXIT_CANCELLED = 2, 3
STATUS_SETUP, STATUS_CANCEL, STATUS_FINISHED = 2, 19, 18


def typed(name, value):
    if name == "MimeTypes":
        return dbus.Array(value, signature="s")
    return TYPES[name](value)


def load_scenario(path):
    path = pathlib.Path(path)
    recorded_dir = path.parent.parent / "recorded"
    scenario = json.loads(path.read_text(encoding="utf-8"))
    properties, script = None, []
    for t in scenario["transactions"]:
        if "recorded" in t:
            recording = json.loads((recorded_dir / f"{t['recorded']}.json").read_text(encoding="utf-8"))
            properties = properties or recording["properties"]
            script.append(dict(recording["transactions"][t["index"]], **{
                k: v for k, v in t.items() if k not in ("recorded", "index")}))
        elif "hand-written" in t:
            script.append(dict(t))
        else:
            raise ValueError(f"{path}: a transaction is neither recorded nor marked hand-written")
    if properties is None:
        first = sorted(recorded_dir.glob("*.json"))[0]
        properties = json.loads(first.read_text(encoding="utf-8"))["properties"]
    return scenario, properties, script


class Fake:
    def __init__(self, mock, scenario_path):
        self.mock = mock
        self.scenario, properties, self.script = load_scenario(scenario_path)
        self.gap_ms = self.scenario.get("gap_ms", 1)
        self.problems = []
        self.count = 0
        self.states = []  # one per created transaction
        mock.AddProperties(MAIN_IFACE, {k: typed(k, v) for k, v in properties.items()})
        # dbusmock calls only plain functions (not bound methods) as method code.
        mock.AddMethod(MAIN_IFACE, "CreateTransaction", "", "o", lambda _self: self.create())
        mock.AddMethod(CHECK_IFACE, "Verdict", "", "s", lambda _self: self.verdict())
        self.revokes = []
        # Same connection, two more names: the objects' paths don't overlap PackageKit's.
        bus = mock.bus_name.get_bus()
        self.names = [dbus.service.BusName(n, bus, allow_replacement=False, replace_existing=False,
                                           do_not_queue=True) for n in (LOGIND_NAME, POLKIT_NAME)]
        if self.scenario.get("polkit", True):
            mock.AddObject("/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
                           {"Id": dbus.String(SESSION_ID)}, [])
            mock.AddObject("/org/freedesktop/PolicyKit1/Authority", POLKIT_IFACE, {}, [])
            dbusmock.mockobject.objects["/org/freedesktop/PolicyKit1/Authority"].AddMethod(
                POLKIT_IFACE, "RevokeTemporaryAuthorizations", "(sa{sv})", "",
                lambda _s, subject: self.revokes.append(subject))

    def verdict(self):
        problems = list(self.problems)
        expected = self.scenario.get("revokes", 0)
        if len(self.revokes) != expected:
            problems.append(f"{len(self.revokes)} polkit revoke(s), expected {expected}")
        for kind, details in self.revokes:
            if str(kind) != "unix-session" or str(details.get("session-id")) != SESSION_ID:
                problems.append(f"revoke for {kind} {dict(details)}, not session {SESSION_ID}")
        idle = sum(1 for st in self.states if not st["called"])
        if idle:
            problems.append(f"{idle} transaction(s) created but never given a call")
        for t in self.script:
            if not t.get("used"):
                problems.append(f"never called: {t['call']} flags={t['flags']} args={t['args']}")
            elif t.get("expect_cancel") and not t.get("cancelled"):
                problems.append(f"not cancelled: {t['call']} args={t['args']}")
        return "\n".join(problems)

    def create(self):
        self.count += 1
        path = f"/{self.count}_fake"
        props = {k: typed(k, v) for k, v in {
            "Role": 0, "Status": 1, "LastPackage": "", "Uid": 1000, "Sender": "",
            "Percentage": 101, "AllowCancel": True, "CallerActive": True, "ElapsedTime": 0,
            "RemainingTime": 0, "Speed": 0, "DownloadSizeRemaining": 0, "TransactionFlags": 0,
        }.items()}
        self.mock.AddObject(path, TX_IFACE, props, [])
        tx = dbusmock.mockobject.objects[path]
        state = {"timer": 0, "events": [], "script": None, "called": False, "hints": []}
        self.states.append(state)
        tx.AddMethod(TX_IFACE, "SetHints", "as", "",
                     lambda _s, hints: state["hints"].extend(str(h) for h in hints))
        tx.AddMethod(TX_IFACE, "Resolve", "tas", "",
                     lambda _s, flags, names: self.start(tx, state, "Resolve", flags, names))
        tx.AddMethod(TX_IFACE, "InstallPackages", "tas", "",
                     lambda _s, flags, ids: self.start(tx, state, "InstallPackages", flags, ids))
        tx.AddMethod(TX_IFACE, "Cancel", "", "", lambda _s: self.cancel(tx, state))
        return dbus.ObjectPath(path)

    def start(self, tx, state, call, flags, args):
        args = [str(a) for a in args]
        if state["called"]:
            self.problems.append(f"unexpected: a second call ({call}) on one transaction")
        state["called"] = True
        for t in self.script:
            if (not t.get("used") and t["call"] == call and t["flags"] == int(flags)
                    and sorted(t["args"]) == sorted(args)):
                t["used"] = True
                state["script"] = t
                missing = [h for h in t.get("expect_hints", []) if h not in state["hints"]]
                if missing:
                    self.problems.append(f"{call} args={args}: hints {state['hints']} lack {missing}")
                state["events"] = list(t["events"])
                break
        else:
            self.problems.append(f"unexpected: {call} flags={int(flags)} args={args}")
            state["events"] = [["ErrorCode", [ERROR_INTERNAL, "fake: unexpected call"]],
                               ["Finished", [EXIT_FAILED, 0]], ["Destroy", []]]
        gap = (state["script"] or {}).get("gap_ms", self.gap_ms)
        state["timer"] = GLib.timeout_add(gap, self.step, tx, state)

    def step(self, tx, state):
        if not state["events"]:
            state["timer"] = 0
            return False
        kind, body = state["events"].pop(0)
        self.emit(tx, kind, body)
        if kind == "Destroy":
            state["events"] = []
        return True

    def emit(self, tx, kind, body):
        if kind == "properties":
            changed = {k: typed(k, v) for k, v in body.items()}
            tx.props[TX_IFACE].update(changed)
            tx.EmitSignal(dbus.PROPERTIES_IFACE, "PropertiesChanged", "sa{sv}as",
                          [TX_IFACE, dbus.Dictionary(changed, signature="sv"), dbus.Array([], signature="s")])
        else:
            tx.EmitSignal(TX_IFACE, kind, SIGNATURES[kind], body)

    def cancel(self, tx, state):
        t = state["script"]
        if t is None or not state["events"]:
            self.problems.append("unexpected: Cancel on a transaction with nothing running")
            return
        t["cancelled"] = True
        if not t.get("expect_cancel"):
            self.problems.append(f"unexpected: Cancel of {t['call']} args={t['args']}")
        # Hand-written (no recording of a cancel yet): the daemon's documented answer.
        state["events"] = [["properties", {"Status": STATUS_CANCEL}],
                           ["ErrorCode", [ERROR_CANCELLED, "fake: cancelled"]],
                           ["properties", {"Status": STATUS_FINISHED}],
                           ["Finished", [EXIT_CANCELLED, 0]], ["Destroy", []]]
        if state["timer"]:
            GLib.source_remove(state["timer"])
        state["timer"] = GLib.timeout_add(1, self.step, tx, state)


def load(mock, parameters):
    mock.fake = Fake(mock, parameters["scenario"])
