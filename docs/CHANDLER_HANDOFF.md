# Chandler: Handoff Context

Chandler is a small C + GTK app that answers one question: "which installable applications can open files of this type?" It shows matching apps from the distro's AppStream metadata and installs the chosen one through PackageKit. It exists to replace the GNOME Software launch behind GTK's "Find New Applications" button.

The name follows the developer's nautical naming theme (sibling apps: Quoin, a file manager; Galley, a markdown viewer; Fife, a music player). A ship's chandler supplies vessels with what they need.

## Background and decision

- Environment: Xubuntu (Xfce, X11). The developer writes C + GTK apps and develops headlessly on Linux.
- Quoin uses the stock GTK "Select Application" dialog (`GtkAppChooserDialog`). Its "Find New Applications" button spawns `gnome-software`, which is not installed and will not be.
- Existing alternatives were evaluated and rejected:
  - Synaptic and gnome-packagekit (`gpk-application`) cannot search by MIME type, which is the dialog's main use.
  - Plasma Discover can (`--mime`), but pulls in KDE/Qt and shares GNOME Software's design philosophy.
  - Ubuntu App Center is Flutter and snap-first.
  - mintinstall is not packaged for Ubuntu and is tied to Mint's infrastructure.
- Decision: write a narrow, single-purpose tool. Chandler is a MIME-type app finder, not a general package manager. Synaptic and apt remain the tools for browsing, updates, removal, and repo management.

## Step zero: verify assumptions before writing code

These came from memory and must be confirmed on the target machine.

1. **Exact arguments GTK sends.** Expected: `gnome-software --search=<content-type>` when the dialog has a content type, `gnome-software --mode=overview` otherwise. Verify with a logging stub:
   ```sh
   #!/bin/sh
   echo "$(date -Is) $*" >> /tmp/gnome-software-args.log
   ```
   Install it as `gnome-software` on PATH, click the button from Quoin with a few file types, read the log, then remove the stub. Also confirm whether the button is shown when no `gnome-software` binary exists (some GTK versions may hide it).
2. **AppStream data is populated.** Run `appstreamcli what-provides mediatype application/pdf` and `appstreamcli --version`. If results are empty, check that the `appstream` package is installed and `apt update` has fetched DEP-11 metadata.
3. **Library versions.** Note the installed versions of GTK, libappstream, and packagekit-glib2. AppStream's C API changed at 1.0 (for example, some pool queries return `AsComponentBox` instead of `GPtrArray`). Code against the installed headers, not against memory.
4. **GTK major version.** Match Quoin's. If Quoin is on GTK 4, note that `GtkAppChooser*` is deprecated since 4.10, which affects the Quoin-side integration but not Chandler itself.

## Scope

### In scope (v1)

- Accept the gnome-software-compatible flags so Chandler can stand in via a shim:
  - `--search=<term>`: if the term looks like a MIME type (contains `/`), do a mediatype lookup; otherwise do a text search over app names and summaries.
  - `--mode=overview`: open with an empty search box.
  - Unknown gnome-software flags are ignored, not fatal.
- Native invocation: `chandler <mime-type>` and bare `chandler`.
- Query AppStream for components of kind `desktop-application` that provide the given mediatype.
- Show only components installable through apt/PackageKit (skip snap/flatpak bundles, if present in the pool).
- For each result: icon, name, summary, package name, and an Install button (or an "Installed" state).
- Install via PackageKit with polkit authentication, with per-row progress and clear error reporting.
- A header that shows the human-readable type description (`g_content_type_get_description`) alongside the raw MIME type.
- Useful empty and error states: no matches, AppStream data missing, PackageKit unavailable, install failed.

### Out of scope

- Updates, removal, repo or PPA management, and browsing categories.
- Snap and Flatpak.
- Reviews, screenshots, ratings, and featured or curated content.
- Running anything as root directly; all privilege goes through PackageKit and polkit.

### Possible later additions (do not build in v1)

- A "broaden search" action that retries with the MIME type's parent types (for example, `text/x-csrc` to `text/plain`). This is opt-in because the parent types of text formats match nearly every editor.
- An "Open with" handoff after install.

## Architecture

- **Language and build:** C, Meson. Build dependencies are probably `libgtk-3-dev` or `libgtk-4-dev` (match Quoin), `libappstream-dev`, and `libpackagekit-glib2-dev`.
- **Query layer (libappstream):**
  - Create and load an `AsPool` at startup. Loading can take a moment, so do it asynchronously if the API allows it, and show a loading state.
  - Look up components by provided item: kind mediatype, value is the MIME type.
  - Filter to desktop applications.
  - Read the name, summary, icons, and package names (`pkgnames`) from each component.
  - Icons: prefer stock icons from the theme; fall back to AppStream cached icons (look up the on-disk cache paths for the installed AppStream version).
- **Package layer (packagekit-glib2):**
  - Resolve package names to PackageKit package IDs, using filters to learn installed state.
  - Install with `PkTask` asynchronously, using the progress callback to drive a per-row progress bar.
  - PackageKit is D-Bus activated; handle failures to reach it gracefully.
- **UI:**
  - Single window with a search entry at the top (prefilled when launched with a term), a result list, and an info bar for errors.
  - Utilitarian, minimal, and fast, consistent with Quoin, Galley, and Fife. No header-bar gimmicks or decorative chrome.
  - Rows stay usable during an install; other rows remain clickable. One install at a time is acceptable for v1.
- **Desktop integration:** a `.desktop` file and an icon. Install to the standard prefix via Meson.

## Integration with Quoin (separate task, in Quoin's repo)

- Preferred: replace `GtkAppChooserDialog` in Quoin with a custom dialog built around `GtkAppChooserWidget` (the "Find New Applications" button belongs to the dialog, not the widget). Add Quoin's own button that spawns `chandler --search=<content-type>`.
- After Chandler installs an app, check whether the app chooser picks it up automatically (it may watch `GAppInfoMonitor`). If it does not, refresh the widget when Chandler exits or when Quoin's dialog regains focus.
- Optional, system-wide: a `gnome-software` shim on PATH that execs Chandler, so other GTK apps' dialogs also use it. Caveat: software that detects `gnome-software` may assume the real program and try its D-Bus interface or other flags. Do this only if other apps' dialogs prove to be a nuisance.

## Milestones

1. Step zero verification, with findings recorded in the repo (for example, in `NOTES.md`).
2. CLI-only prototype: `chandler --list <mime>` prints matching apps with package names and installed state. This proves the AppStream and PackageKit layers without UI.
3. GTK window with results, icons, and installed state; no installing yet.
4. Install via PackageKit with progress and errors.
5. Flag compatibility (`--search=`, `--mode=overview`) and text search.
6. Quoin integration (in Quoin's repo).

## Test cases

- `application/pdf`, `image/png`, `text/markdown`, `audio/flac`: common types that should return several apps.
- An obscure or vendor type with no matches, to exercise the empty state.
- A type whose only handler is already installed, to exercise the "Installed" state.
- AppStream cache missing or empty, to exercise the explanatory error.
- Cancelling the polkit prompt, which should produce a clean "cancelled" state and not a scary error.
- No network during install.

## Working notes for the Claude Code session

- The developer relies on Claude to write the code and wants honest disclosure: if an API detail is uncertain, check the installed headers or say so rather than guessing.
- If Quoin's source is available, match its conventions (GTK version, build layout, code style).
- Keep the scope tight. If a feature seems to belong to a general software center, it probably belongs in Synaptic instead.
