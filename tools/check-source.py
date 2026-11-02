#!/usr/bin/env python3
"""Source rules the compiler can't state, run by ./build check.

  check-source.py            check src/ and tests/
  check-source.py self-test  prove each rule refuses its bad examples and passes its good ones

Rules:
  nodiscard   a function that reports failure through GError ** and returns a value is
              [[nodiscard]]: a declaration in a header, or a static function. (An exported
              function's definition is covered by its header's declaration.)
  discarded   a call passes NULL for a GError ** parameter and also discards its return
              value: the failure leaves no trace at all. (NULL is fine where the return
              value says what failed.) Which parameters are GError ** comes from the
              preprocessed prelude (every GLib and GTK prototype) and our own headers.
  suppression no local overrides: #pragma GCC/clang diagnostic, NOLINT, cppcheck-suppress,
              and no (void) or (void) ! cast on a call, which silences [[nodiscard]] and
              warn_unused_result. Exceptions are listed in VOID_CASTS below, per file, with
              their exact count and their reason, so one can't be added in passing.
  logpath     a log or abort message (g_debug ... g_critical, g_log, gbk_fatal) carries no
              path, folder, URI or display name: users paste logs into public bug
              reports, and a path holds their user name (DP-3). Matched by the argument's
              name (path, dir, uri, filename, display, dest, src, root, location). src/
              only: the tests' messages name their own temporary folders.
  message     no branching on an error's message text (ER-8): ->message is not an
              argument of a comparison or search (g_str_equal, strcmp, g_str_has_prefix,
              strstr, g_strstr_len, g_regex_match_simple, ...). Branch on domain and code.
  hidden      no literal hidden characters (no-break and zero-width spaces, bidirectional
              controls, line and paragraph separators) in source: write them as escapes
              (CD-7). GCC rejects the bidirectional ones itself; this covers the rest, in
              every file the compiler doesn't read too (meson.build, scripts).
  widening    a #define that multiplies integer literals says its type (a cast, or INT64_C and
              the like): `64u * 1024u` is an unsigned int, silently widened (or overflowed)
              wherever it meets a size or a 64-bit time. clang-tidy finds the uses one by one
              (bugprone-implicit-widening-of-multiplication-result); this finds the cause.
  adapters    AppStream and PackageKit (as_*, As*, AS_*, pk_*, Pk*, PK_*) appear only in their
              adapters (ADAPTER_FILES): the rest of the program sees Chandler's own structs
              (ER-3, plan §3.1).
  nobans      CHANDLER_PRELUDE_NO_BANS, which turns every ban in the prelude off, appears
              nowhere but meson.build's ban probes.
  markup      no "use-markup" property set by name: the markup functions are poisoned
              (prelude.h), and this is the one way around them (plan §3.6).
  fixtures    committed test data (tests/data/) holds no home path, user name or host name
              (DP-1; the words come from ./build, CHANDLER_PRIVATE_WORDS). Fails closed when
              the words are missing.
  claude-md   CLAUDE.md links the standard and names the 40-hex commit it conforms to (RP-6).
  async       every call to a library's `*_async` function passes chandler_async_ready as its
              callback (src/async.h): the result is dropped, and the call cancelled, when its
              owner goes first, and the completion is stall-timed (plan §4.2).
  unsequenced one argument of a call hands &x to a nested call while another argument reads
              x: the order the arguments are evaluated in is unspecified, so x may be read
              before the nested call has written it. No compiler warning or analyzer here
              catches it (GCC -Wsequence-point, Clang -Wunsequenced, clang-tidy, cppcheck).
              Likewise two calls to a stream reader (a function whose first parameter is a
              `Reader *`, which advances it) in one initializer or one argument list: their
              order is unspecified too, so fields come out swapped (proto.c, 2026-10-17).

The matching is textual (comments removed, then regular expressions), so it is written to
fail closed: a declaration it can't read is not silently passed, it is simply not seen.
That gap is what the self-test's examples pin down.
"""

import glob
import os
import re
import subprocess
import sys

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
# return type and qualifiers, function name, a parameter list holding GError **, ; or {
FUNCTION = re.compile(
    r"(?:^|[;}\n])\s*((?:\[\[nodiscard\]\]\s*)?(?:static\s+)?(?:inline\s+)?"
    r"[A-Za-z_][A-Za-z_0-9 \t\n*]*?)\b([a-z_][a-z_0-9]*)\s*\(([^;{)]*\bGError\s*\*\*[^;{)]*)\)\s*([;{])")


def nodiscard(path, text):
    problems = []
    header = path.endswith(".h")
    for m in FUNCTION.finditer(COMMENT.sub(" ", text)):
        ret = " ".join(m.group(1).split())
        name = m.group(2)
        if ret in ("", "return", "else") or ret.startswith("typedef") or ret.endswith(" void") \
                or ret == "void" or ret.startswith("extern"):
            continue
        if not header and not ret.startswith("[[nodiscard]] static") and "static" not in ret:
            continue
        if "[[nodiscard]]" not in ret:
            problems.append(f"{path}: {name} reports failure through GError ** but isn't "
                            "[[nodiscard]]")
    return problems


def top_level_args(text, start):
    """The arguments of the call whose '(' is at `start`, split at top-level commas, and the
    index just past its ')'; None if unbalanced."""
    depth, args, begin = 0, [], start + 1
    for i in range(start, len(text)):
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                args.append(text[begin:i])
                return args, i + 1
        elif ch == "," and depth == 1:
            args.append(text[begin:i])
            begin = i + 1
    return None


LVALUE = r"[A-Za-z_][A-Za-z_0-9]*(?:(?:->|\.)[A-Za-z_][A-Za-z_0-9]*)*"
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def unsequenced(path, text):
    problems = []
    code = STRING.sub('""', COMMENT.sub(" ", text))
    for m in re.finditer(r"\b[A-Za-z_][A-Za-z_0-9]*\s*\(", code):
        parsed = top_level_args(code, m.end() - 1)
        if parsed is None:
            continue
        args = parsed[0]
        for i, arg in enumerate(args):
            # &x inside a nested call in this argument (an out-parameter being written)
            for out in re.findall(r"\(\s*[^()]*&\s*(" + LVALUE + ")", arg):
                read = re.compile(r"(?<![&\w>.])" + re.escape(out) + r"(?![\w(])")
                for j, other in enumerate(args):
                    # sizeof's operand is not evaluated: not a read
                    other = re.sub(r"\bsizeof\s*\(?\s*" + LVALUE + r"\s*\)?", "sizeof ()", other)
                    if j != i and read.search(other):
                        line = code.count("\n", 0, m.start()) + 1
                        problems.append(f"{path}:{line}: {out} is written through &{out} in one "
                                        "argument and read in another; call first, then read")
    problems += reader_order(path, code)
    return sorted(set(problems))


READER_FN = re.compile(r"\b([a-z_][a-z_0-9]*)\s*\(\s*(?:const\s+)?Reader\s*\*")
LIST_START = re.compile(r"=\s*\{|\(\s*(?:const\s+)?[A-Z][A-Za-z_0-9]*\s*\)\s*\{|\b[A-Za-z_][A-Za-z_0-9]*\s*\(")


def reader_order(path, code):
    readers = set(READER_FN.findall(code))
    if not readers:
        return []
    call = re.compile(r"\b(" + "|".join(sorted(readers)) + r")\s*\(")
    problems = []
    for m in LIST_START.finditer(code):
        parsed = top_level_args(code, m.end() - 1)
        if parsed is None:
            continue
        with_reader = [i for i, el in enumerate(parsed[0]) if call.search(el)]
        if len(with_reader) > 1:
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"{path}:{line}: two stream reads in one initializer or argument "
                            "list: their order is unspecified; read into variables first")
    return problems


CALL = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(")
GERROR_PARAM = re.compile(r"\bGError\s*\*\s*\*")


def gerror_params(text):
    """{function: positions of its GError ** parameters}, from declarations in `text`."""
    table = {}
    for m in CALL.finditer(text):
        parsed = top_level_args(text, m.end() - 1)
        if parsed is not None:
            for i, arg in enumerate(parsed[0]):
                if GERROR_PARAM.search(arg):
                    table.setdefault(m.group(1), set()).add(i)
    return table


def prototypes():
    """Every GLib, GTK and project function taking GError **, as the compiler sees them."""
    flags = subprocess.run(["pkg-config", "--cflags", "gtk+-3.0", "appstream", "packagekit-glib2"],
                           capture_output=True,
                           text=True, check=True).stdout.split()
    builds = sorted(glob.glob("out/*/chandler-config.h"))
    if not builds:
        sys.exit("check-source: no configured build (chandler-config.h) to preprocess the prelude with")
    pre = subprocess.run(["gcc", "-E", "-P", "-std=gnu23", "-D_GNU_SOURCE",
                          "-I" + builds[0].rsplit("/", 1)[0], "-Ivendor/quoin/src"] + flags
                         + ["-include", "src/prelude.h", "-x", "c", "/dev/null"],
                         capture_output=True, text=True)
    if pre.returncode != 0:
        sys.exit("check-source: preprocessing the prelude failed\n" + pre.stderr[-2000:])
    ours = "".join(COMMENT.sub(" ", open(h).read())
                   for h in sorted(glob.glob("src/*.h") + glob.glob("vendor/quoin/src/*.h")))
    table = gerror_params(pre.stdout + ours)
    if len(table) < 100:
        sys.exit(f"check-source: only {len(table)} functions take GError **; the prelude parse broke")
    return table


GERROR_TABLE = {}


def discarded(path, text):
    problems = []
    code = STRING.sub('""', COMMENT.sub(" ", text))
    for m in CALL.finditer(code):
        params = GERROR_TABLE.get(m.group(1))
        if not params:
            continue
        parsed = top_level_args(code, m.end() - 1)
        if parsed is None:
            continue
        args = [a.strip() for a in parsed[0]]
        if not any(i < len(args) and args[i] in ("nullptr", "NULL") for i in params):
            continue
        before = code[:m.start()].rstrip()
        after = code[parsed[1]:].lstrip()
        if (before == "" or before[-1] in ";{}" or before.endswith("(void)")) and after.startswith(";"):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"{path}:{line}: {m.group(1)} with no GError and its result discarded:"
                            " a failure here leaves no trace")
    return problems


# Files allowed (void) casts on calls: exact count, and why.
VOID_CASTS: dict[str, tuple[int, str]] = {}
VOID_CAST = re.compile(r"\(\s*void\s*\)\s*!?\s*[A-Za-z_][A-Za-z_0-9]*\s*\(")
SUPPRESSION = re.compile(r"#\s*pragma\s+(GCC|clang)\s+diagnostic|\bNOLINT|cppcheck-suppress")


def suppression(path, text):
    problems = []
    for m in SUPPRESSION.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        problems.append(f"{path}:{line}: suppression ({m.group(0)}): put the exception in the "
                        "tool's configuration, scoped to its files")
    code = STRING.sub('""', COMMENT.sub(" ", text))
    casts = [code.count("\n", 0, m.start()) + 1 for m in VOID_CAST.finditer(code)]
    allowed = VOID_CASTS.get(path, (0, ""))[0]
    if len(casts) != allowed:
        where = ", ".join(str(n) for n in casts)
        problems.append(f"{path}: {len(casts)} (void) casts on calls (lines {where or 'none'}), "
                        f"{allowed} allowed by VOID_CASTS in tools/check-source.py")
    return problems


LOG_CALL = re.compile(r"\b(g_debug|g_info|g_message|g_warning|g_critical|g_log|gbk_fatal)\s*\(")
PATHY = re.compile(r"(?<![A-Za-z_0-9])(?:[A-Za-z_0-9]+(?:->|\.))*"
                   r"(path|dir|uri|filename|display|dest|src|root|location)\b")


def logpath(path, text):
    problems = []
    if not path.startswith("src/"):
        return problems                # tests log their own temporary folders
    code = STRING.sub('""', COMMENT.sub(" ", text))
    for m in LOG_CALL.finditer(code):
        parsed = top_level_args(code, m.end() - 1)
        if parsed is None:
            continue
        for arg in parsed[0][1:]:
            hit = PATHY.search(arg)
            if hit:
                line = code.count("\n", 0, m.start()) + 1
                problems.append(f"{path}:{line}: {m.group(1)} logs {hit.group(0)}: logs name "
                                "kinds and codes, never paths (they hold the user's name)")
    return problems


COMPARE_CALL = re.compile(r"\b(g_str_equal|strcmp|strncmp|g_strcmp0|g_str_has_prefix|"
                          r"g_str_has_suffix|strstr|g_strstr_len|g_strrstr|strcasecmp|"
                          r"g_ascii_strcasecmp|g_regex_match_simple|g_pattern_match_simple)\s*\(")


def message(path, text):
    problems = []
    code = COMMENT.sub(" ", text)
    for m in COMPARE_CALL.finditer(code):
        parsed = top_level_args(code, m.end() - 1)
        if parsed is not None and any("->message" in a for a in parsed[0]):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"{path}:{line}: {m.group(1)} on an error's message: branch on "
                            "its domain and code (g_error_matches)")
    return problems


DEFINE_MUL = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(.*\b[0-9]+[uUlL]*[ \t]*\*[ \t]*[0-9]+.*)$",
                        re.M)
TYPED = re.compile(r"\(\s*(?:const\s+)?[A-Za-z_][A-Za-z_0-9]*\s*\)\s*[0-9(]|\b[A-Z0-9]+_C\s*\(")


def widening(path, text):
    code = COMMENT.sub(" ", text)
    return [f"{path}:{code.count(chr(10), 0, m.start()) + 1}: #define {m.group(1)} multiplies "
            "literals without saying the type: write ((size_t) 64 * 1024), INT64_C (...)"
            for m in DEFINE_MUL.finditer(code) if not TYPED.search(m.group(2))]


ADAPTER_FILES = {
    "src/catalog.c": "the AppStream adapter (milestone 2)",
    "src/packages.c": "the PackageKit adapter (milestone 2)",
    "tests/test-catalog.c": "tests the AppStream adapter against real libappstream",
    "tests/test-packages.c": "tests the PackageKit adapter against the fake daemon",
    "tests/pk-drive.c": "drives the real daemon for recording its traffic (never shipped)",
}
LIBRARY_NAME = re.compile(r"\b(?:as|pk)_[a-z][a-z0-9_]*|\b(?:As|Pk)[A-Z][A-Za-z0-9]*\b"
                          r"|\b(?:AS|PK)_[A-Z][A-Z0-9_]*")


# The prelude is where the libraries' functions are banned and the bans' off-switch is defined.
PRELUDE = "src/prelude.h"


def adapters(path, text):
    if path in ADAPTER_FILES or path == PRELUDE:
        return []
    code = STRING.sub('""', COMMENT.sub(" ", text))
    return [f"{path}:{code.count(chr(10), 0, m.start()) + 1}: {m.group(0)} outside the adapters "
            "(ADAPTER_FILES): use Chandler's own types"
            for m in LIBRARY_NAME.finditer(code)]


# Macros that lift bans, and the only files that may name them.
BAN_LIFTERS = {
    "CHANDLER_PRELUDE_NO_BANS": {PRELUDE, "meson.build", "tools/check-source.py"},
    # Lifts the raw signal/source bans; set for vendor/quoin/src/lifetime.c alone.
    "GBK_LIFETIME_IMPL": {PRELUDE, "src/meson.build", "tests/test-contract.c",
                          "tools/check-source.py"},
}


def nobans(path, text):
    code = COMMENT.sub(" ", text)
    problems = []
    for macro, allowed in BAN_LIFTERS.items():
        if path in allowed:
            continue
        problems += [f"{path}:{code.count(chr(10), 0, m.start()) + 1}: {macro} lifts bans; it "
                     f"belongs only in {', '.join(sorted(allowed))}"
                     for m in re.finditer(macro + r"\b", code)]
    return problems


def markup(path, text):
    code = COMMENT.sub(" ", text)
    return [f"{path}:{code.count(chr(10), 0, m.start()) + 1}: names the {m.group(0)} property "
            "or attribute: Chandler shows text, never markup"
            for m in re.finditer(r'"[a-z_-]*markup"', code)]


def data_files():
    """Every file under tests/data, dotfiles included (glob would skip them)."""
    found = []
    for base, _, files in os.walk("tests/data"):
        found += [os.path.join(base, f) for f in files]
    return sorted(found)


def fixtures(path, text, words):
    if not path.startswith("tests/data/"):
        return []
    problems = []
    for word in ["/home/"] + words:
        if word and word in text:
            problems.append(f"{path}: contains {word!r}: committed test data must not name "
                            "the user, the host or a home folder (scrub it)")
    return problems


# Calls taking a GAsyncReadyCallback: every *_async, and those whose names don't say so.
ASYNC_CALL = re.compile(r"\b([a-z_][a-z0-9_]*_async|g_dbus_proxy_call|g_dbus_connection_call|"
                        r"g_dbus_proxy_call_with_unix_fd_list|g_bus_get|g_task_new|"
                        r"g_dbus_connection_call_with_unix_fd_list|g_dbus_proxy_new|"
                        r"g_dbus_proxy_new_for_bus)\s*\(")


def async_rule(path, text):
    if path in ("src/async.c", "src/async.h"):
        return []
    code = STRING.sub('""', COMMENT.sub(" ", text))
    problems = []
    for m in ASYNC_CALL.finditer(code):
        if m.group(1).startswith("chandler_async"):
            continue
        parsed = top_level_args(code, m.end() - 1)
        if parsed is None:
            continue
        if not any(a.strip() == "chandler_async_ready" for a in parsed[0]):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"{path}:{line}: {m.group(1)} without chandler_async_ready as its "
                            "callback: use chandler_async_new (src/async.h)")
    return problems


STANDARD = "https://github.com/odclcslkrpb5k/paranoid-typescript/blob/"


def claude_md(path, text):
    link = re.search(re.escape(STANDARD) + r"([0-9a-f]{40})/appendices/c-gtk\.md", text)
    if not link:
        return [f"{path}: does not link the standard's C/GTK appendix at a commit"]
    named = re.search(r"Last conformed to commit: ([0-9a-f]{40})\b", text)
    if not named:
        return [f"{path}: does not name the commit it last conformed to (RP-6)"]
    if link.group(1) != named.group(1):
        return [f"{path}: links the standard at {link.group(1)[:12]} but says it conforms to "
                f"{named.group(1)[:12]}"]
    return []


HIDDEN = re.compile("[\u00a0\u00ad\u180e\u2000-\u200f\u2028-\u202f\u205f-\u206f\ufeff]")


def hidden(path, text):
    return [f"{path}:{text.count(chr(10), 0, m.start()) + 1}: hidden character "
            f"U+{ord(m.group(0)):04X}: write it as an escape"
            for m in HIDDEN.finditer(text)]


RULES = [nodiscard, unsequenced, discarded, suppression, logpath, message, hidden, widening,
         adapters, nobans, markup, async_rule]
EXEMPT: set[str] = set()


def check():
    GERROR_TABLE.update(prototypes())
    problems = []
    words = os.environ.get("CHANDLER_PRIVATE_WORDS", "").split()
    if len(words) < 2:
        sys.exit("check-source: CHANDLER_PRIVATE_WORDS is not set (run through ./build)")
    files = sorted(glob.glob("src/*.[ch]") + glob.glob("tests/*.[ch]") + glob.glob("tests/*/*.[ch]"))
    other = sorted(glob.glob("meson.build") + glob.glob("*/meson.build") + glob.glob("tools/*")
                   + glob.glob("tests/*.py") + ["build", "Dockerfile", "compose.yaml",
                                                ".clang-tidy", "CLAUDE.md", "README.md"])
    data = data_files()
    # TS-6: say what was selected, and refuse a selection missing files known to exist.
    expected = {"src/main.c", "src/prelude.h", "src/window.c", "src/catalog.c", "src/packages.c",
                "tests/test-contract.c", "tests/test-window.c", "tests/fuzz/driver.c"}
    if not expected <= set(files):
        sys.exit("check-source: selection misses " + " ".join(sorted(expected - set(files)))
                 + "; run from the repository root")
    for path in files:
        if path in EXEMPT:
            continue
        text = open(path, encoding="utf-8").read()
        for rule in RULES:
            problems += rule(path, text)
    for path in (p for p in other if os.path.isfile(p)):
        problems += hidden(path, open(path, encoding="utf-8").read())
        problems += nobans(path, open(path, encoding="utf-8").read())
    for path in data:
        problems += fixtures(path, open(path, encoding="utf-8", errors="replace").read(), words)
    problems += claude_md("CLAUDE.md", open("CLAUDE.md", encoding="utf-8").read())
    for p in problems:
        print("  " + p)
    if problems:
        sys.exit(f"check-source: {len(problems)} problems")
    print(f"check-source: {len(files)} C files, {len(other)} other files, {len(data)} test data "
          f"files, {len(RULES)} rules, no problems")


# (rule, file name, source, expected number of problems)
EXAMPLES = [
    (nodiscard, "x.h", "bool f (const char *p, GError **error);", 1),
    (nodiscard, "x.h", "[[nodiscard]] bool f (const char *p, GError **error);", 0),
    (nodiscard, "x.h", "GbkThing *f (int a,\n           GError **error);", 1),
    (nodiscard, "x.h", "void f (GError **error);", 0),
    (nodiscard, "x.h", "/* bool f (GError **error); */", 0),
    (nodiscard, "x.h", "typedef bool (*Fn) (GError **error);", 0),
    (nodiscard, "x.c", "static bool\nf (GError **error)\n{\n  return true;\n}", 1),
    (nodiscard, "x.c", "[[nodiscard]] static bool\nf (GError **error)\n{\n  return true;\n}", 0),
    (nodiscard, "x.c", "bool\ngbk_f (GError **error)\n{\n  return true;\n}", 0),
    (nodiscard, "x.c", "  if (f (&error))\n    return g (x, &error);", 0),
    (unsequenced, "x.c", "agree (gbk_ops_run (a, &done->error), done->error);", 1),
    (unsequenced, "x.c", "agree (run (&e), e);", 1),
    (unsequenced, "x.c", "show (parse (s, &n), n + 1);", 1),
    (unsequenced, "x.c", "ok = run (&e);\nagree (ok, e);", 0),
    (unsequenced, "x.c", "if (!f (a, &error))\n  show (error->message);", 0),
    (unsequenced, "x.c", "g (f (&e), e2);", 0),
    (unsequenced, "x.c", "g (f (&d->e), d->error);", 0),
    (unsequenced, "x.c", "static int get_u8 (Reader *r);\nT t = { .a = get_u8 (r), .b = get_u8 (r) };", 1),
    (unsequenced, "x.c", "static int get_u8 (Reader *r);\nT t = (T) { get_u8 (r), 0, get_u8 (r) };", 1),
    (unsequenced, "x.c", "static int get_u8 (Reader *r);\nshow (get_u8 (r), get_u8 (r));", 1),
    (unsequenced, "x.c", "static int get_u8 (Reader *r);\nint a = get_u8 (r);\nint b = get_u8 (r);\nT t = { .a = a, .b = b };", 0),
    (unsequenced, "x.c", "static int get_u8 (Reader *r);\nT t = { .a = get_u8 (r), .b = 2 };", 0),
    (unsequenced, "x.c", "int get_u8 (int *r);\nshow (get_u8 (r), get_u8 (r));", 0),
    (unsequenced, "x.c", "g (&e, f (e));", 0),
    (unsequenced, "x.c", "g (f (\"&e\"), e);", 0),
    (unsequenced, "x.c", "check (read (fd, &v, sizeof v), sizeof v);", 0),
    (unsequenced, "x.c", "check (read (fd, &v, sizeof (v)), v);", 1),
    (discarded, "x.c", "  load (path, nullptr);", 1),
    (discarded, "x.c", "{ load (path, NULL); }", 1),
    (discarded, "x.c", "(void) load (path, nullptr);", 1),
    (discarded, "x.c", "  if (!load (path, nullptr))\n    return;", 0),
    (discarded, "x.c", "  ok = load (path, nullptr);", 0),
    (discarded, "x.c", "  load (path, &error);", 0),
    (discarded, "x.c", "  g_autofree char *s = read (0, 1, nullptr);", 0),
    (discarded, "x.c", "  return load (path, nullptr);", 0),
    (suppression, "x.c", "#pragma GCC diagnostic ignored \"-Wshadow\"", 1),
    (suppression, "x.c", "# pragma clang diagnostic push", 1),
    (suppression, "x.c", "int x = f (); // NOLINT", 1),
    (suppression, "x.c", "// cppcheck-suppress nullPointer\nint x;", 1),
    (suppression, "x.c", "  (void) load (p, &e);", 1),
    (suppression, "x.c", "  (void) !write (fd, b, n);", 1),
    (suppression, "x.c", "  (void) 0;\n  (void) unused_param;", 0),
    (suppression, "x.c", "  x = (void *) f (y);", 0),
    (suppression, "x.c", "  /* (void) f (); */", 0),
    (logpath, "src/x.c", "g_warning (\"readdir %s: %s\", s->path, g_strerror (errno));", 1),
    (logpath, "src/x.c", "gbk_fatal (\"no button for %s\", dir);", 1),
    (logpath, "src/x.c", "g_debug (\"opened %s\", it->display);", 1),
    (logpath, "src/x.c", "g_warning (\"readdir: %s\", g_strerror (errno));", 0),
    (logpath, "src/x.c", "gbk_fatal (\"bad kind %d\", (int) kind);", 0),
    (logpath, "src/x.c", "g_warning (\"no path here\");", 0),
    (logpath, "tests/x.c", "gbk_fatal (\"opendir %s\", dir);", 0),
    (widening, "x.c", "#define CHUNK (64u * 1024u)", 1),
    (widening, "x.c", "#define WAIT_MS (5 * 60 * 1000)   /* five minutes */", 1),
    (widening, "x.c", "#define CHUNK ((size_t) 64 * 1024)", 0),
    (widening, "x.c", "#define LIMIT (INT64_C (10) * 1000)", 0),
    (widening, "x.c", "#define SHIFT (1u << 20)", 0),
    (widening, "x.c", "#define N 64", 0),
    (widening, "x.c", "/* #define CHUNK (64 * 1024) */", 0),
    (message, "x.c", "if (g_str_has_prefix (error->message, \"No such\"))", 1),
    (message, "x.c", "if (strcmp (e->message, m) == 0)", 1),
    (message, "x.c", "if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))", 0),
    (message, "x.c", "label = g_strdup (error->message);", 0),
    (hidden, "x.c", "const char *s = \"a\u00a0b\";", 1),
    (hidden, "x.c", "/* a\u200bb */", 1),
    (hidden, "x.c", "const char *s = \"a\\u00a0b\";", 0),
    (adapters, "src/window.c", "AsPool *pool = as_pool_new ();", 2),
    (adapters, "src/window.c", "if (code == PK_ERROR_ENUM_NO_NETWORK)", 1),
    (adapters, "src/catalog.c", "AsPool *pool = as_pool_new ();", 0),
    (adapters, "src/window.c", "bool has_pool = alias_count > 0; /* as_pool_new */", 0),
    (adapters, "src/window.c", "g_print (\"pk_client\");", 0),
    (nobans, "src/x.c", "#define CHANDLER_PRELUDE_NO_BANS 1", 1),
    (nobans, "tools/x.sh", "CFLAGS=-DCHANDLER_PRELUDE_NO_BANS", 1),
    (nobans, "src/x.c", "/* CHANDLER_PRELUDE_NO_BANS */", 0),
    (nobans, "src/prelude.h", "#ifndef CHANDLER_PRELUDE_NO_BANS", 0),
    (adapters, "src/prelude.h", "#pragma GCC poison pk_task_new", 0),
    (markup, "src/x.c", "g_object_set (label, \"use-markup\", true, nullptr);", 1),
    (markup, "src/x.c", "g_object_set (label, \"use_markup\", true, nullptr);", 1),
    (markup, "src/x.c", "gtk_label_set_text (label, name);", 0),
    (markup, "src/x.c", "g_object_set (w, \"tooltip-markup\", s, nullptr);", 1),
    (markup, "src/x.c", "g_object_set (d, \"secondary-use-markup\", true, nullptr);", 1),
    (markup, "src/x.c", "gtk_tree_view_column_add_attribute (c, r, \"markup\", 0);", 1),
    (nobans, "src/meson.build", "c_args : '-DGBK_LIFETIME_IMPL',", 0),
    (nobans, "src/x.c", "#define GBK_LIFETIME_IMPL", 1),
    (async_rule, "src/x.c", "as_pool_load_async (pool, c, on_loaded, data);", 1),
    (async_rule, "src/x.c", "as_pool_load_async (pool, chandler_async_cancellable (a),\n"
     "                    chandler_async_ready, a);", 0),
    (async_rule, "src/x.c", "ChandlerAsync *a = chandler_async_new (o, f, d, nullptr);", 0),
    (async_rule, "src/async.c", "g_file_read_async (f, 0, nullptr, cb, d);", 0),
    (async_rule, "src/x.c", "/* as_pool_load_async (pool, c, cb, d); */", 0),
    (async_rule, "src/x.c", "g_task_new (nullptr, c, on_done, data);", 1),
    (async_rule, "src/x.c", "g_task_new (nullptr, chandler_async_cancellable (a), chandler_async_ready, a);", 0),
    (async_rule, "src/x.c", "g_dbus_connection_call (c, n, p, i, m, v, t, 0, -1, nullptr, cb, d);", 1),
    (async_rule, "src/x.c", "g_bus_get (G_BUS_TYPE_SYSTEM, nullptr, cb, d);", 1),
    (nobans, "tests/meson.build", "c_args : '-DGBK_LIFETIME_IMPL',", 1),
    (claude_md, "CLAUDE.md", "Last conformed to commit: " + "a" * 40, 1),
    (claude_md, "CLAUDE.md", STANDARD + "a" * 40 + "/appendices/c-gtk.md\nLast conformed to "
     "commit: " + "a" * 40, 0),
    (claude_md, "CLAUDE.md", STANDARD + "a" * 40 + "/appendices/c-gtk.md\nLast conformed to "
     "commit: " + "b" * 40, 1),
]
FIXTURE_EXAMPLES = [
    ("tests/data/pk/resolve.txt", "uid 1000 /home/someone/x", 1),
    ("tests/data/pk/resolve.txt", "sender :1.42 by alice on box", 2),
    ("tests/data/pk/resolve.txt", "Package hello;2.10;amd64;ubuntu", 0),
    ("tests/test-x.c", "/home/alice", 0),
]


def self_test():
    GERROR_TABLE.update(gerror_params("bool load (const char *p, GError **error);\n"
                                       "char *read (int fd, int n, GError **error);"))
    failed = 0
    for rule, name, source, expected in EXAMPLES:
        got = len(rule(name, source))
        if got != expected:
            failed += 1
            print(f"  {rule.__name__}: expected {expected}, got {got}: {source!r}")
    for name, source, expected in FIXTURE_EXAMPLES:
        got = len(fixtures(name, source, ["alice", "box"]))
        if got != expected:
            failed += 1
            print(f"  fixtures: expected {expected}, got {got}: {source!r}")
    total = len(EXAMPLES) + len(FIXTURE_EXAMPLES)
    if failed:
        sys.exit(f"check-source self-test: {failed} of {total} examples wrong")
    print(f"check-source self-test: {total} examples")


if __name__ == "__main__":
    {"self-test": self_test}.get(sys.argv[1] if len(sys.argv) > 1 else "", check)()
