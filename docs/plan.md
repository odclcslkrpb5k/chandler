# Chandler: plan

Status: APPROVED by the owner 2026-11-01, with the decisions in §8. Nothing is built yet.

Inputs:

- `docs/CHANDLER_HANDOFF.md`.
- The Paranoid TypeScript standard at commit `fecdf9cb06ca` (2026-11-01) and its C/GTK 3
  appendix.
- Quoin (`~/Projects/gbk-file`, commit `356344d2005f`), the project that appendix was written
  from.

Every control names its rung on the Hierarchy of Defect Controls. Administer and Protect
choices name what was considered above them. §9 records the red team and what changed
because of it.

## 1. Step zero

### 1.1 Verified

Verified on this machine (Ubuntu 26.04.1, Xubuntu). API facts were checked against
`libappstream-dev` 1.1.2 and `libpackagekit-glib2-dev` 1.3.4, installed in a throwaway
`ubuntu:26.04` container at the digest Quoin pins. The host has neither dev package. Milestone
0's image installs them, and the build compiles against them, which re-proves every API name
below.

| Fact | Finding |
|---|---|
| GTK | 3.24.52. GTK 4.22.4 is also installed; Quoin uses 3. |
| AppStream | 1.1.2 runtime and dev. |
| PackageKit | Daemon 1.3.4 with the apt backend, on the system bus. Headers are under `/usr/include/PackageKit/packagekit-glib2/`. |
| polkit | `package-install` is `auth_admin_keep` for the active session. The polkit-gnome agent is running, and the user is in `sudo`. |
| Architectures | **i386 is a foreign architecture here.** Packages have several candidate versions across suites; `libreoffice-draw` has three. |
| DEP-11 | Populated: `/var/lib/app-info` points to `/var/lib/swcatalog`, and icons are unpacked per origin. A query takes 26 ms with a warm cache. |
| Test types | `application/pdf`, `image/png` (63 results), `text/markdown` and `audio/flac` all return apps; a made-up type returns none. |
| **MIME aliases** | `audio/flac` returns 25 apps and its alias `audio/x-flac` returns 36. Rhythmbox and Quod Libet are listed only under the alias, so §3.2 queries every alias. |
| Pool flags | With `LOAD_OS_CATALOG` only, `application/pdf` returns 35 apps, all with package names and none with bundles. The package-less `google-chrome.desktop` result goes away; it came from `LOAD_OS_DESKTOP_FILES`. |
| Duplicates | `AsPool` already merges a component ID that appears in several suites (101 such IDs), so no union code is needed; a test pins the behaviour. |
| Icons | `as_icon_get_filename` gives the full path of a cached icon. Cached icons whose file is missing are dropped by the pool. Every component also carries a `REMOTE` icon URL, which Chandler never loads. |
| APIs | Present in the headers: `as_pool_load_async`, `as_pool_set_flags`, `as_pool_set_load_std_data_locations`, `as_pool_add_extra_data_location`, `as_pool_get_components_by_provided_item`, `as_pool_search`, `as_pool_is_empty` and `as_component_box_index_safe`. `as_component_box_index` is an unchecked macro and is not used. |
| PackageKit API | `pk_client_resolve_async`, `pk_client_install_packages_async` (which takes transaction flags) and `pk_control_get_properties_async`. Filters `ARCH`, `NEWEST`, `NOT_SOURCE`, `INSTALLED`, `NOT_INSTALLED`. **Daemon errors arrive in the `PK_CLIENT_ERROR` domain at code `0xff + PkErrorEnum`** (`pk-client.h:78`). |
| Test bus | `python3-dbusmock` 0.38.1 has **no PackageKit template**. The fake is hand-written (§4.3). |
| Ordering trap | `as_pool_set_load_std_data_locations (FALSE)` resets the flags to 0. A later `set_flags (LOAD_OS_CATALOG)` brings the host catalog back. §3.2 fixes the call order. |

### 1.2 Still open

These need a person at the display, and come **before** milestone 0 (§5).

1. ~~Is the stock "Find New Applications" button hidden when `gnome-software` is absent?~~
   **Answered by the owner, 2026-11-01: it is NOT hidden.** It shows in Quoin's dialog and
   tries to run `gnome-software`. The red team and I had both inferred "hidden" from a symbol
   import; that inference was wrong. Consequence for §6: a shim on PATH makes the stock button
   work in Quoin and in every other GTK 3 app, with no Quoin code.
2. ~~The exact argv GTK sends.~~ **Answered 2026-11-01** with a logging stub on PATH,
   driven from Quoin's dialog. GTK sends exactly **one argument**, `--search=<type>`:

   | Selection in Quoin | argv |
   |---|---|
   | file with no extension | `--search=application/octet-stream` |
   | `.svg` | `--search=image/svg+xml` (`+` intact) |
   | `.pdf` | `--search=application/pdf` |
   | two types (`.3mf` + `.blend`) | `--search=application/octet-stream` |

   `--mode=overview` never appeared: Quoin always gives the dialog a type. It stays supported
   (other apps may send it), but the `application/octet-stream` rule in §6 is what Quoin's
   unknown-type and several-types cases actually hit. These four lines are the seeds for
   `request.c`'s tests and fuzz corpus.
3. **The real PackageKit traffic.** `dbus-monitor --system` without root is refused
   `BecomeMonitor` and sees nothing. So **you** run `sudo dbus-monitor --system
   "sender='org.freedesktop.PackageKit'" "destination='org.freedesktop.PackageKit'"` (the `!`
   prefix in this session works). Meanwhile Chandler's milestone-2 PkClient logger, or `pkcon`
   if you prefer to install `packagekit-tools`, does the following:
   - resolve a known name, an unknown name, and one with an i386 candidate
   - simulate `hello`
   - install `hello` while dismissing polkit
   - install `hello` with a wrong password
   - install `hello` properly
   - install with the network down
   - install with Synaptic holding the lock
   - simulate a package that asks debconf questions (`ttf-mscorefonts-installer` is the EULA
     case)

   This settles:
   - whether `Package` or the plural `Packages` signal is used
   - whether simulate lists the requested package itself, and whether it prompts polkit
   - how an unknown name comes back (skipped, or the whole call fails)
   - what dismissal looks like (expected: `NOT_AUTHORIZED`, the same as a wrong password)
   - what debconf and EULA packages do through PackageKit

   The raw capture never enters the repository. It is scrubbed first (§4.2). You remove
   `hello` afterwards with apt.

   **Recorded 2026-11-01** (owner at the display; `tools/record-pk.sh`, scrubbed by
   `tools/scrub-pk.py` into `tests/data/pk/recorded/`). What it settled:
   - **Signals:** with the `supports-plural-signals` hint (packagekit-glib2 sends it), resolve
     and simulate answer with the plural `Packages`; a real install still sends singular
     `Package` plus `ItemProgress` while it works. The fake serves both.
   - **Simulate** needs no password (no polkit call), and lists every package it would touch,
     the requested one included (`ttf-mscorefonts-installer` brings `cabextract` and
     `libmspack0t64`).
   - **The package ID's data field changes between calls:** resolve says
     `ubuntu-resolute-main` (available) or `manual:ubuntu-resolute-main` (installed); simulate
     and install say `+manual:…` for what was asked and `+auto:…` for dependencies. So
     milestone 4 compares simulate's answer with the chosen IDs by name, version and
     architecture, never the whole ID; and `+auto` is how it tells "also installs" apart.
   - **Dismissing polkit and a wrong password look the same:** `ErrorCode` 48
     (`NOT_AUTHORIZED`, "Failed to obtain authentication."), then `Finished` with exit
     `FAILED`. Dismissal takes about 2 s; three wrong passwords about 14 s. This confirms the
     neutral `AUTH_INCOMPLETE` of §3.4.
   - **No network:** polkit is asked first, and *then* `ErrorCode` 2 (`NO_NETWORK`), "Cannot
     download packages whilst offline". So the password is spent before the network fails.
   - **The lock (Synaptic) was not captured:** the network was still off for that step, so it
     recorded a second no-network, and the scrubber leaves it out (`MISRECORDED`). The `locked`
     scenario is hand-written; one more recording of that step alone
     (`tools/record-pk.sh 05`) would replace it.
   - **Not recorded yet, for milestone 4:** a `Cancel` of a running transaction (the fake's
     answer to one is hand-written), and the hints a client sends (the fake accepts any).
     Milestone 4's pre-auth cancel gets a recording step of its own before it relies on them.
   - **EULA / debconf:** simulating `ttf-mscorefonts-installer` sends no `EulaRequired`. Its
     licence is a debconf question, and for an interactive install packagekit-glib2 sends a
     `frontend-socket` hint pointing at `pk-debconf-socket` in the user's runtime directory.
     What a real install does with that question is still unknown; milestone 4 decides (one
     option: refuse packages whose install would ask, if simulate can tell).
   - **No daemon:** found by the fake rather than the recording. With nothing on the bus,
     `pk_control_get_properties` *succeeds* from defaults. `packages.c` therefore also
     requires `connected` and a version (Engineer; the `no-daemon` scenario pins it).

## 2. Ground rules and deviations

These are taken from Quoin; please confirm them. They go into `CLAUDE.md`, which names the
standard's commit (RP-6).

- **GTK 3.** This is a deviation from the appendix's "a new project weighs GTK 4 first". The
  reasons: it matches Quoin, the appendix's settings were proven on GTK 3, and it is what the
  handoff asks for.
- **No CI and no git hooks; the gate is `./build release`.** This is a deviation from SH-11's
  commit-time gate, and it is the owner's rule for Quoin. As in Quoin, the release build
  refuses a dirty tree and stamps the git hash. It adds an SH-12 scan for paths, user names and
  host names over the tree.
- **LN-5 has no C tool.** This is the appendix's known gap, recorded as such.
- Durable context goes in `docs/claude-notes.md` (RP-3), not Claude's memory. Writes stay
  inside the repository. Docker is pinned by digest and an apt snapshot, and `./build check` is
  the one command (TC-1, TC-5, TC-9).
- Distribution is Quoin's model: a release binary, `install.sh` into `~/.local`, and no
  root.
- Prefix `chandler_`. The vendored lifetime module keeps `gbk_`.
- Git: `git init`; public repository `github.com/odclcslkrpb5k/chandler` (owner, 2026-11-01).
- Quoin's own Claude session runs nearby and can be messaged (AG-10, AG-13) when Quoin-side
  facts or changes are needed. Its messages inform; only the owner authorizes (AG-7).

## 3. Design

### 3.1 Shape

```
src/
  main.c         GtkApplication (unique, §3.3), argv -> ChandlerRequest
  request.c/h    PURE: argv (native + gnome-software flags) -> ChandlerRequest. Fuzzed.
  mediatype.c/h  PURE: RFC 6838 media type -> ChandlerMediaType. Fuzzed.
  catalog.c/h    adapter: libappstream. Owned ChandlerApp structs out. No GTK.
  packages.c/h   adapter: packagekit-glib2 (PkClient, PkControl). No GTK.
  outcome.c/h    PURE: (GError domain, code) -> ChandlerOutcome -> one sentence.
  async.c/h      owner-scoped async completion (§4.2): weak owner, cancellable, stall-timed.
  results.c/h    ChandlerResults GObject: apps + per-app state; owns the install.
  window.c/h     view; row.c/h one row; cli.c/h `--list`.
  prelude.h      force-included: system headers, then the bans.
vendor/quoin/    lifetime.c/h, fatal.h, analyzer-glib.h, tests/test-lifetime.c + MANIFEST.
```

Libraries are met only in the adapters (ER-3). The rule is **Engineer**: `check-source.py`
refuses an AppStream or PackageKit include, or an `as_`/`pk_` identifier, outside its adapter.

### 3.2 Query (catalog.c)

- **There is one function that opens a pool, with a fixed call order.** It sets flags to
  `LOAD_OS_CATALOG` only. Tests use `chandler_catalog_open_at (dir)`, which turns off the
  standard locations and then adds the directory, inside that same function. **Engineer:**
  a test asserts that every component the fixture pool returns has the fixture's
  `as_component_get_origin`, so a host catalog leaking in fails the test. The test can fail
  even inside Docker, because the fixture names an origin no real catalog uses. The pool's
  locale defaults from the environment.
- Loading uses `as_pool_load_async` with an owner-scoped completion (§4.2) and shows a
  loading state.
- **A MIME lookup queries the type and each of its aliases**, read from shared-mime-info
  through GIO (`g_content_type_*`, or `/usr/share/mime/aliases` if GIO exposes no alias
  list; checked in milestone 2), in both directions. Results are merged by component ID.
  Text search uses `as_pool_search`. Both are filtered to `DESKTOP_APP`, at least one
  pkgname, and no bundles.
- Strings are validated as UTF-8 and copied on the way out (BD-1).
- An empty pool (`as_pool_is_empty`) shows "No app metadata: run `sudo apt update`". A load
  failure is shown as a different message.
- **Icons:** a `STOCK` icon is shown if the theme has it. Otherwise the `CACHED` file at
  `as_icon_get_filename`, read asynchronously, then a generic icon. **`REMOTE` and `LOCAL`
  icons are never read** (Eliminate). Metadata cannot cause a network fetch or a read from a
  path it chooses. A cached path must also lie under the swcatalog root (Engineer).

### 3.3 Packages (packages.c)

This is where the blast radius is. **Chandler uses `PkClient`, not `PkTask`.** `PkTask`
exists to ask questions, such as installing untrusted packages anyway or accepting a
simulation. Chandler wants neither, so `PkTask` is not used (Eliminate).

- **No package name from outside.** Package IDs are only minted from PackageKit's own Resolve
  answer, as `ChandlerPackageId`, a single-member struct built only in `packages.c` (BD-5).
  Install takes only that type. There is no path from argv to install. This is Eliminate
  and Engineer.
- **Resolve with `ARCH | NEWEST | NOT_SOURCE`.** It must return **exactly one native-arch ID
  per pkgname**, or the row shows Unavailable (Engineer). Without this, a resolve with no
  filter on this machine returns i386 IDs and old versions, and installing all of them would
  pull in an i386 stack.
- **Installed state** comes from a separate resolve with `INSTALLED`. A component is
  Installed when every pkgname is installed. It is Unavailable when any pkgname does not
  resolve, shown as "Not available from your software sources", with no button.
- **Install is click, then check, then confirm, then install:**
  1. The row shows "Checking…". Chandler resolves again, so a recent apt update can't leave a
     stale ID, and simulates `SIMULATE | ONLY_TRUSTED` with that exact ID set.
  2. **What the simulation may contain is an allowlist** (Engineer, a switch over
     `PkInfoEnum` with no default): `INSTALLING`, plus `UPDATING` (see §8). Everything else
     refuses, with "This would also change other software: use Synaptic or apt". That covers
     `REMOVING`, `OBSOLETING`, `DOWNGRADING`, `REINSTALLING`, `UNTRUSTED` and anything
     unknown.
  3. If the simulation installs only the app's own packages, Chandler goes straight on. If it
     brings more, the row shows "Install (also installs 14 packages)", and **a second click
     confirms**, before any password prompt.
  4. Install `ONLY_TRUSTED` with **the same ID set**. There is no "install anyway".
  5. Resolve again. The row reads Installed only if PackageKit now says so.
- **Residual risk, accepted and flagged (Protect):** apt can change its plan between simulate
  and install, which is a window of seconds. PackageKit cannot execute a fixed plan, so
  there is no higher rung. The re-resolve in step 5 shows the outcome; it cannot prevent it.
- **Closing the window during an install:** if the transaction hasn't passed
  `WAITING_FOR_AUTH` / `WAITING_FOR_LOCK` (no download or install yet), Chandler cancels it.
  That way a password prompt can never appear after Chandler's window has gone. A prompt
  with no visible owner looks like phishing, and it trains people to type their password
  blind. Past authorization, the application holds itself until the transaction finishes.
  Nothing is notified: the app chooser shows the new app when it lands. The install's
  cancellable belongs to `ChandlerResults`, not to a row.
- **One install at a time, enforced across processes.** The `GtkApplication` is unique
  (Eliminate). A second launch, such as Quoin's button pressed again, re-searches the open
  window and raises it. Other rows' buttons are disabled during an install. That deviates from
  the handoff's "other rows remain clickable", but scrolling and searching still work.
- **polkit's 5-minute `auth_admin_keep`:** after Chandler's install completes, it revokes the
  temporary authorization it obtained (polkit `RevokeTemporaryAuthorizations` for its
  session; the method is to be verified in milestone 4). Otherwise every process in the
  session can install packages for five minutes without a prompt. This narrows the
  window (Engineer); see §8.
- **PackageKit availability:** at startup, `pk_control_get_properties_async`. Any failure
  gives `NO_DAEMON`, whatever its cause: not installed, masked, or not starting.

### 3.4 Errors (outcome.c)

Errors are classified by domain and code only (ER-8), decoding `PK_CLIENT_ERROR` codes
above `0xff` as `PkErrorEnum`. `ChandlerOutcome` is an enum switched without a default
(LN-3). A test feeds every `PkErrorEnum` value through, so a new daemon code still maps
(Engineer).

| Outcome | From | Shown as |
|---|---|---|
| `CANCELLED` | `G_IO_ERROR_CANCELLED`, `TRANSACTION_CANCELLED` | The row returns to Install. No message (ER-10). |
| `AUTH_INCOMPLETE` | `NOT_AUTHORIZED`, `FAILED_AUTH`, `DECLINED_INTERACTION` | "Not installed: authentication didn't complete." Neutral styling, because dismissing the prompt and a wrong password look the same (§1.2, item 3). |
| `NO_NETWORK` | `NO_NETWORK`, `PACKAGE_DOWNLOAD_FAILED` | "Couldn't download the package." |
| `LOCKED` | `CANNOT_GET_LOCK`, `LOCK_REQUIRED` | "Another package manager is running…" |
| `GONE` | `PACKAGE_NOT_FOUND` | "No longer available: run `sudo apt update`." |
| `ALREADY` | `PACKAGE_ALREADY_INSTALLED` | Success: resolve again. |
| `BROKEN` | `DEP_RESOLUTION_FAILED`, `TRANSACTION_ERROR` | "Your package system needs attention: use Synaptic or apt." |
| `NO_SPACE` | `NO_SPACE_ON_DEVICE` | "Not enough disk space." |
| `WOULD_CHANGE` | The simulation allowlist (§3.3) | The list, then "use Synaptic or apt". |
| `UNTRUSTED` | `*_GPG_*`, `CANNOT_INSTALL_REPO_UNSIGNED`, an `UNTRUSTED` info | "Not signed by a trusted source." |
| `CANNOT_CANCEL` | `CANNOT_CANCEL` | "Too late to cancel." The install continues. |
| `NO_DAEMON` | §3.3 | An info bar, with every button disabled. |
| `OTHER` | Anything else | "Something went wrong in PackageKit." The error's name and PackageKit's own text sit behind an expander (milestone 4). |

PackageKit's message text appears only in that expander. It is never logged (DP-3).

### 3.5 Command line

| Invocation | Request |
|---|---|
| `chandler`, `--mode=overview` | Empty search |
| `chandler <mime>`, `--search=<valid media type>` | MIME lookup with aliases |
| `--search=<other text>` | Text search |
| `--list <mime>` | Text output, exit 0/1/2. **Unstable format; nothing may parse it.** Control characters, including TAB, are stripped. |
| `--version` | `chandler <hash>` |
| Other gnome-software flags (`--install=`, `--local-filename=`, `--details=`, …) | The window opens with a one-line info bar: "Chandler only finds apps by file type or name; use apt for this." |
| Any other flag | Ignored, logged at debug level |

The parser is hand-written and fuzzed. The media-type check uses the RFC 6838 grammar, not
the handoff's "contains a `/`".

### 3.6 UI

- One window: a search entry, an info bar, and a list of rows. Each row shows a 48 px icon,
  the name, the summary, the package names, and a button or state. There is no header bar.
  The heading shows `g_content_type_get_description` and the raw type.
- **Markup is poisoned:** `gtk_label_set_markup`, `gtk_label_set_use_markup`,
  `gtk_label_set_markup_with_mnemonic`, `gtk_widget_set_tooltip_markup` and
  `g_markup_printf_escaped` are banned in the prelude. Metadata can't inject Pango markup.
  This is Eliminate: Chandler shows no markup. If bold text is needed it comes from CSS or
  attributes, not markup.
- Search re-queries after 200 ms idle. **Enter on a row focuses its button and does not
  press it**, so a stray Enter can't install anything during polkit's keep window.
- Esc closes the window.

## 4. Applying the standard

The full level applies from the first commit (AD-1).

### 4.1 Taken from Quoin

| Piece | How |
|---|---|
| `lifetime.c/h`, `fatal.h`, `analyzer-glib.h`, **`tests/test-lifetime.c`** | **Vendored unchanged** in `vendor/quoin/`, with a MANIFEST giving the commit, file hashes and modes. **Vendor contract test (Engineer):** it checks the manifest, with a planted mismatch that must fail (TS-7), and builds and runs `test-lifetime` under Chandler's flags (CP-2). It asserts that `GBK_LIFETIME_IMPL` is defined only for `vendor/quoin/lifetime.c`, that the test environment sets `GBK_STALL_MS=250`, and that a planted 300 ms callback aborts. Without that last check, an unset variable silently turns the stall watch off. |
| `meson.build`: warnings plus fixtures, ban probes, hardening, analyzer options | Copied and edited. `-Wl,-z,nodlopen` is **dropped**: Quoin's notes say it is a no-op for a PIE. |
| `prelude.h` | Rewritten for Chandler's headers, with Quoin's poison list plus §4.2's additions. |
| `tests/meson.build` test environment | `G_DEBUG=fatal-warnings,fatal-criticals,gc-friendly`, `GDK_GL=disable`, `NO_AT_BRIDGE=1`, `GSETTINGS_BACKEND=memory`, `GBK_STALL_MS=250`, and `lsan.supp` with `leak:libfontconfig.so` only. |
| `Dockerfile`, `compose.yaml` (`network_mode: none`), `build`, `in-container.sh` | Copied and edited. Adds `libappstream-dev`, `libpackagekit-glib2-dev`, `appstream` and `python3-dbusmock`. **`build` ends every run with a PASS/FAIL line and exits with the check's status.** This carries over Quoin's recurring slip of filtered or echoed exit codes. |
| `check-source.py`, `analysis.py`, `check-binary.sh`, cppcheck config | Copied and edited. The generic rules are kept and `locpath` is dropped. New rules: `adapters`, `async` (§4.2), `fixtures` (§4.2), and `claude-md` (RP-6: `CLAUDE.md` links the standard and names a commit). |

**Drift (Administer, deliberate).** The copied tools and flag lists can diverge from Quoin.
The Engineer alternative is a shared "paranoid-c" repository that both projects vendor. It
isn't worth it for two projects; build it if a third C project appears.

### 4.2 Hazards particular to Chandler

| Hazard | Control | Rung |
|---|---|---|
| Installing what the user didn't choose | Package IDs only from Resolve; `ChandlerPackageId` type | Eliminate + Engineer |
| Wrong architecture or several versions installed | Resolve filters; exactly one ID per name | Engineer |
| An install that removes, downgrades or obsoletes | The simulate allowlist. TOCTOU residual is Protect | Engineer |
| Untrusted packages | `ONLY_TRUSTED`; `PkClient` without `PkTask` | Eliminate |
| Password prompt with no visible owner | Cancel pre-auth transactions when the window closes | Eliminate |
| Silent installs in polkit's keep window | Revoke after install; Enter doesn't install; unique app | Engineer + Eliminate |
| Async callback after its owner has gone | `async.c`: every `*_async` call in an adapter goes through `chandler_async_*`, which holds a `GWeakRef` and `GCancellable` and drops a late result. `check-source` rule `async` refuses a raw `*_async` call without it. Vendored lifetime for signals and sources. | Engineer |
| Frozen window | **Poisoned:** `as_pool_load`, `pk_client_*_sync`, `pk_control_*_sync`, `g_dbus_connection_call_sync`, `gdk_pixbuf_new_from_file*`, `g_file_load_contents`, each with a configure probe. The stall watch times `chandler_async_*` completions too. | Eliminate + Engineer |
| Markup injection | Markup APIs poisoned | Eliminate |
| Terminal escape injection (`--list`) | Strip at the CLI boundary; fuzzed | Engineer |
| Error text chosen by string | Codes only; `message` rule | Engineer |
| Fixtures leaking the user's data (public repo) | Raw captures stay out of the repo. A scrubber keeps only members, signatures and needed values. The `fixtures` rule refuses `/home/`, the user name and the host name under `tests/data/`. There is also a release-time scan. | Engineer |
| Tests reaching the real system bus | The container has `network_mode: none` and **no `/run/dbus` mount**, so the real bus does not exist. `in-container.sh` also asserts the private address (TS-6). | Eliminate + Engineer |
| Stale or missing metadata | Explicit states | Protect. Considered: a "Refresh sources" button (PackageKit `RefreshCache`), but that is software-center scope. Later. |

### 4.3 Testing

- **Pure modules:** GTest plus libFuzzer, with seeds replayed in normal builds.
- **catalog.c** runs against real libappstream with a fixture cut from the real DEP-11 YAML
  (TS-11), with real PNGs for cached icons. It covers: the four types, an alias-only app
  (FLAC), an addon, a duplicate ID across two suites (testing that `AsPool` merges it),
  invalid UTF-8, markup in a name, ESC and TAB in a summary, and a `REMOTE` icon that must
  not be fetched. It runs the origin assertion of §3.2, with `XDG_CACHE_HOME` in a temporary
  directory.
- **packages.c** runs against a **hand-written fake PackageKit** on a private system bus.
  It uses python-dbusmock's generic object API (`AddMethod`, `AddProperty`, `EmitSignal`) as
  the scaffolding (Substitute for the D-Bus plumbing only), scripted from the scrubbed
  transcripts. The surface it serves, from `busctl introspect` and
  `org.freedesktop.PackageKit.Transaction.xml`:
  - Root properties: `VersionMajor`, `VersionMinor`, `VersionMicro`, `Filters`, `Roles`,
    `Locked`, `NetworkState`.
  - Root method: `CreateTransaction` returning a path.
  - Transaction properties `Status`, `Percentage`, `AllowCancel`, `CallerActive` and
    `TransactionFlags`, announced through `PropertiesChanged`.
  - Transaction methods `SetHints`, `Resolve`, `InstallPackages` and `Cancel`, with the
    `CannotCancel` error.
  - Signals `Package` and `Packages` (§1.2 item 3: both are used), `ItemProgress`,
    `ErrorCode`, `Finished` and `Destroy`.
  - **Built 2026-11-01:** `tests/fake-packagekit.py` replays scenarios
    (`tests/data/pk/scenarios/`), each transaction either taken from a recording or marked
    `hand-written` with its reason. `tests/with-fake-pk.py` gives each scenario its own private
    bus and fake, requires the case list to equal the scenario files, and fails a case when
    the client made any call the scenario doesn't list, or skipped one it does.
- **The fake is checked against reality:** milestone 2's `chandler --list`, run by hand on
  the host, exercises the same code against the real daemon. Its output is compared with the
  fake's on the same names. This is Administer, done each release. The Engineer alternative
  is a VM with a real apt archive, which is not proportionate.
- **Hand-written fixtures are labelled as such.** A real "would remove" or "would downgrade"
  answer can't be produced on demand, so those transcripts are written by hand from the
  recorded format. TS-11 is only partly met there, and the fixture says so.
- **No skips (TS-10):** a missing dbusmock or Xvfb fails the run.
- **Widget tests** run under Xvfb. They cover every state in §3.4, the check/confirm flow,
  close-before-auth (cancelled) against close-after-auth (completes), and the narrowest
  window with the longest real summary (TS-14).
- **Mutation (TS-5):** plant a bug for every guard in §4.2 and every check-source rule. That
  includes the allowlist, the arch filter, the origin assertion, the pre-auth cancel, the
  "Installed only after a re-resolve" check, the vendor contract, and the stall probe.
- **Release acceptance by hand (Administer, deliberate):** install `hello` with Chandler,
  dismiss polkit once, pull the network once, then compare `--list` with the fake. The
  Engineer alternative is the VM above, which is not proportionate for a single-user tool.
- `desktop-file-validate` and `appstreamcli validate` run on Chandler's own data.

## 5. Milestones

Each milestone ends with: `./build check` green, separately briefed reviews (SH-4), a
harvest into `docs/claude-notes.md`, and a commit.

0. ~~Step zero, by eye~~: done 2026-11-01 (§1.2 items 1 and 2).
1. **Skeleton:** `git init`, `CLAUDE.md` with deviations, notes (seeded with Quoin's
   build-output lessons), Docker, `build`, meson with fixtures and bans, vendored lifetime
   with its contract test, `check-source`, `analysis`, an empty window, and the full gate.
2. **Query + CLI + resolve:** `request`, `mediatype`, `catalog` (with aliases), `packages`
   resolve, `--list`, and the PkClient logger. Then **§1.2 item 3, recorded with you**, and
   the scrubbed transcripts written into the fake.
3. **Window:** results, icons, states, search. No installing.
4. **Install:** check/confirm/install, the allowlist, outcomes, the close rules, polkit
   revoke, and the acceptance script.
5. **Flags and polish:** gnome-software flags, `.desktop`, icon, `install.sh`, release.
6. **Quoin integration:** in Quoin's repository, with its own plan (§6).
7. **Optional:** the shim.

## 6. Quoin and the shim

**Revised after §1.2 item 1:** the stock button is visible and spawns `gnome-software`. So
the shim is no longer "optional, if other apps prove a nuisance": it is the cheapest way to
make the button Quoin already shows do the right thing, and it covers every GTK 3 app at once.
Whether Quoin still needs its own button (for `--mode=overview` with several types, below) is
decided once §1.2 item 2 shows the exact argv. The shim moves up to milestone 5.

Confirmed by Quoin's session (2026-11-01): Quoin never names `gnome-software`. The button
and its spawn are stock `GtkAppChooserDialog`, and GTK 3.24 has no public API to hide or
retarget it. So "Quoin just launches Chandler instead" is not a one-line change. It means
Quoin replaces the stock dialog with its own around `GtkAppChooserWidget` (the handoff's
original preference). The desktop session's PATH, read from the running Quoin and
`xfce4-panel`, includes `~/.local/bin` ahead of `/usr/bin`, so a shim there is found.

**Decided (owner, 2026-11-01): build the shim; no Quoin change.** It is the integration, not an
option. It ships with milestone 5 (`install.sh` installs it next to `chandler`). Chandler
treats `--search=application/octet-stream` as an empty search, because that is what Quoin
sends for several types, and "apps that open arbitrary bytes" is never the question asked.

- **Quoin (`appchoose.c`):** add a "Find New Applications…" button. It spawns
  `chandler --search=<type>` through Quoin's `launch` module. When the dialog has no single
  type (Quoin substitutes `application/octet-stream`), it uses `--mode=overview`, so the user
  isn't shown firmware tools. `on_response` currently destroys the dialog on every response,
  so the new button's response returns early. GTK's chooser imports `g_app_info_monitor_get`,
  which should refresh the list by itself; milestone 6 tests that, including an app that
  declares only an alias type. It also checks that Xfwm puts Chandler above the modal dialog.
- **The shim** (`~/.local/bin/gnome-software`, which `exec`s `chandler "$@"`): `~/.local/bin`
  comes early in PATH, so it catches every caller. Flags Chandler doesn't handle get the info
  bar of §3.5, not a silent empty window. It doesn't claim `org.gnome.Software`. Built only if
  needed.

## 7. Not in v1

- Removal, updates, repositories, snap and flatpak.
- Screenshots, reviews and categories.
- "Broaden search" to parent types.
- "Open with" after an install.
- A "Refresh sources" button.
- Desktop notifications.

## 8. Decisions (owner, 2026-11-01)

1. **GTK 3.**
2. **`git init`; public repository `odclcslkrpb5k/chandler`.**
3. **`UPDATING` is allowed** and listed in the confirm step. **No downgrades, removals or
   obsoletes, ever.** The owner's stated concern is a package manager removing software they
   rely on; this is the reason the simulation is an allowlist (§3.3), and it outranks
   convenience in any later trade-off.
4. **Revoke** polkit's temporary authorization after each install.
5. **Approved:** the real `hello` install with `sudo dbus-monitor` recording (§1.2 item 3).
6. **python-dbusmock** as scaffolding for the hand-written fake.
7. **Copy Quoin's tools.**

## 9. Red team (2026-11-01)

Three reviewers, each on a different model with its own brief (SH-4):

- API correctness against the real headers and the live read-only system
- standard conformance and rung placement
- handoff fidelity, the user's flow, and abuse

I verified the key claims before accepting them: i386 foreign architecture, several versions,
the `0xff` offset, the FLAC alias counts, no dbusmock template, and the filter enums.

| # | Finding | Severity | Disposition |
|---|---|---|---|
| 1 | Unfiltered Resolve returns i386 and older IDs, and installing them pulls in an i386 stack | Blocker | Fixed in §3.3 (filters, exactly one ID) |
| 2 | Removal check missed `OBSOLETING`/`REINSTALLING`/`UNKNOWN`; it should be an allowlist | Major | Fixed in §3.3. Relabelled Engineer, not Eliminate |
| 3 | polkit dismissal is indistinguishable from refusal (`NOT_AUTHORIZED`) | Major | Fixed in §3.4 (neutral `AUTH_INCOMPLETE`) |
| 4 | Daemon errors sit at `0xff` + code; the error table was missing nine codes | Major | Fixed in §3.4, with an every-code test |
| 5 | **My error:** I claimed dbusmock "has the transaction pattern" without checking. It has no PackageKit template. | Major | Fixed in §4.3 (hand-written, surface listed). Decision 6 reworded |
| 6 | `set_load_std_data_locations` resets the flags, so the host catalog can leak into tests | Major | Fixed in §3.2 (one open function, origin assertion) |
| 7 | `PkClient` or `PkTask` unstated; `PkTask` asks to install untrusted packages | Major | Fixed in §3.3 (`PkClient`) |
| 8 | Alias types missed: Rhythmbox appears only under `audio/x-flac` | Major | Fixed in §3.2, fixture in §4.3 |
| 9 | "Let it finish" lets a password prompt appear after the window has gone | Major | Fixed in §3.3 (cancel before auth); notifications dropped |
| 10 | `auth_admin_keep` plus Enter-to-install plus several instances can install silently | Major | Fixed in §3.3 and §3.6 (unique app, revoke, Enter focuses) |
| 11 | Simulate timing unclear; no way to decline extra packages before the password | Major | Fixed in §3.3 (simulate on click, second click to confirm) |
| 12 | Unprivileged `dbus-monitor` sees nothing | Major | Fixed in §1.2 (`sudo` run by you) |
| 13 | Raw bus captures would put user data in a public repository | Major | Fixed in §4.2 (scrubber, `fixtures` rule, release scan) |
| 14 | Vendored lifetime's tests and implicit contracts weren't carried over | Major | Fixed in §4.1 (vendor `test-lifetime.c`, contract test) |
| 15 | GWeakRef per call is Administer; library finish callbacks aren't stall-timed; sync calls aren't banned | Major | Fixed in §4.2 (`async.c` plus rule, poison list) |
| 16 | No-CI/no-hooks and GTK 3 were not recorded as deviations; no secrets scan | Major | Fixed in §2 |
| 17 | Quoin with several types would search `application/octet-stream` | Major | Fixed in §6 (`--mode=overview`) |
| 18 | "Checked in the headers" when no headers are on the host | Major | **Disputed in part.** They were checked in a 26.04 container, but the plan didn't say so. Wording fixed in §1.1 |
| 19 | Quoin's test environment, LSan suppression and build-output lessons weren't carried over | Minor | Fixed in §4.1 and §5 |
| 20 | Markup control could be a poison instead of a script | Minor | Fixed in §3.6 (Eliminate) |
| 21 | `nodlopen` copied though it is a no-op | Minor | Dropped |
| 22 | Never load `REMOTE`/`LOCAL` icons; theme-then-cache fallback order | Minor | Fixed in §3.2 |
| 23 | Dedupe code unnecessary: `AsPool` merges | Minor | Fixed in §3.2 (pinned by test instead) |
| 24 | `NO_DAEMON` should come from any control failure | Minor | Fixed in §3.3 |
| 25 | Step-zero stub should come before the skeleton | Minor | Fixed in §5 |
| 26 | The shim silently ignores `--install=` and similar flags | Minor | Fixed in §3.5 (info bar) |
| 27 | `--list` format undefined; TAB not stripped | Minor | Fixed in §3.5 |
| 28 | Scope additions (notifications, headless hold) | Minor | Notifications dropped; headless only after auth; "other rows clickable" deviation recorded |
| 29 | Hand-written "would remove" fixture can't meet TS-11 | Minor | Recorded in §4.3 |
| 30 | debconf/EULA packages through PackageKit unknown | Minor | Added to §1.2 item 3 |
| 31 | Poison `g_object_new` | — | **Rejected:** it doesn't block |
| 32 | Assert the bus address only (TS-1) | — | **Already Eliminate:** the container has no network and no `/run/dbus`; the assertion stays as TS-6 |
| 33 | Compare copied flag lists against Quoin's (TS-7) | Minor | **Not taken:** covered by the Administer drift choice in §4.1, and the fixtures keep each flag honest |

| 34 | (Owner, after review) The stock button is not hidden; the "hidden" inference from a symbol import was wrong | — | §1.2 item 1 answered; §6 revised: the shim becomes the main integration |

Still unverified: polkit's `RevokeTemporaryAuthorizations`, which is checked in milestone 4,
and everything listed in §1.2.
