# Chandler

Finds the applications that can open a file type, from your distribution's AppStream
metadata, and installs the one you choose through PackageKit. It stands in for GNOME
Software behind GTK's "Find New Applications" button. It is not a package manager: for
browsing, updates, removal and repositories, use Synaptic or apt.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/odclcslkrpb5k/chandler/main/install.sh | bash
```

This installs the latest release into `~/.local`. It needs no root and writes four files:

- `~/.local/bin/chandler`
- `~/.local/bin/gnome-software`, the shim (below)
- `~/.local/share/applications/io.github.odclcslkrpb5k.Chandler.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/io.github.odclcslkrpb5k.Chandler.svg`

Run the same command again to update.

**The shim.** GTK's "Open With" dialog has a "Find New Applications" button that runs
`gnome-software`. The shim is a two-line script by that name that runs Chandler instead, so
the button works in every GTK 3 application. It is installed only where it hides nothing:
not over a `gnome-software` that Chandler didn't write, and not while a real
`gnome-software` is on `PATH` (installing GNOME Software later? run the installer again and
it removes the shim). `~/.local/bin` must come before `/usr/bin` in the desktop session's
`PATH`, as it does on Ubuntu.

The installer takes these options, set as environment variables before `bash`. For
example, `curl … | CHANDLER_NO_SHIM=1 bash`.

| Variable | Effect |
| --- | --- |
| `CHANDLER_VERSION=<hash>` | Install a specific release instead of the latest. |
| `CHANDLER_PREFIX=<dir>` | Install under `<dir>` instead of `~/.local`. |
| `CHANDLER_NO_SHIM=1` | Don't install the `gnome-software` shim. |
| `CHANDLER_UNINSTALL=1` | Remove the files above; the shim only if Chandler wrote it. |
| `CHANDLER_ANY_OS=1` | Skip the Ubuntu 26.04 check (see below). |

**Supported systems.** Release binaries are built on **Ubuntu 26.04, x86_64**, against that
release's GTK, GLib, libappstream and packagekit-glib2. The installer refuses other systems
rather than leave a binary that may not start; build from source instead.

## Using it

- `chandler application/pdf` (or `--search=application/pdf`): the apps that open PDFs.
- `chandler --search=paint`: apps whose name or summary match.
- `chandler --list application/pdf`: the same as text, for a terminal. The format is not
  stable; don't parse it.

Each app shows Install or Installed. Install checks first what else would change. It goes
ahead without asking when only that app's packages are added. When others come with it, it
asks first, saying how many. It refuses, before any password prompt, when the install would
remove, downgrade, replace or reinstall a package, or install an unsigned one: use Synaptic
or apt for those. A package that asks a question while installing (a licence, say) may
fail through Chandler; install it with apt.

## Building

Everything builds and runs in Docker; the workstation needs nothing else.

    ./build check     every check (sanitizer tests, analyzers, hardened build)
    ./build format    format the C sources
    ./build shell     a shell in the build image
    ./build release   check, on a clean tree, marking the binary releasable
    ./build install   install the release build into ~/.local (runs release if needed)
    ./build publish   upload the release build as a GitHub release (maintainer)

Builds target Ubuntu 26.04 (x86_64), the release the image is pinned to.

## Configuration

Chandler reads no configuration and no environment variables of its own. Tests read
`GBK_STALL_MS` (set by `tests/meson.build`).
