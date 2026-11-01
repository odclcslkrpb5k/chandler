# Chandler

Finds the applications that can open a file type, from your distribution's AppStream
metadata, and installs the one you choose through PackageKit. It stands in for GNOME
Software behind GTK's "Find New Applications" button. It is not a package manager: for
browsing, updates, removal and repositories, use Synaptic or apt.

Status: under construction (see `docs/plan.md`). Milestone 1, the build skeleton.

## Building

Everything builds and runs in Docker; the workstation needs nothing else.

    ./build check     every check (sanitizer tests, analyzers, hardened build)
    ./build format    format the C sources
    ./build shell     a shell in the build image

Builds target Ubuntu 26.04 (x86_64), the release the image is pinned to.

## Configuration

Chandler reads no configuration and no environment variables of its own. Tests read
`GBK_STALL_MS` (set by `tests/meson.build`).
