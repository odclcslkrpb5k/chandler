#!/usr/bin/env bash
# Runs inside the build image. Invoked via ./build on the host.
set -euo pipefail
cd /src

# TS-1 by construction (compose.yaml): the host's system bus must not exist in here. If a
# mount ever brings it in, every test that talks to "the system bus" would reach the real
# PackageKit, so stop before anything runs (TS-6: assert what was selected).
if [[ -e /run/dbus/system_bus_socket ]]; then
  echo "in-container: the host's system bus is visible (/run/dbus); refusing to run" >&2
  exit 1
fi

setup() {  # setup <dir> <meson options...>
  local dir=$1; shift
  mkdir -p out
  printf '%s\n' "$CHANDLER_VERSION" > out/VERSION    # meson.build reads it as the project version
  rm -f "$dir/released"    # any rebuild voids the release stamp until the gate passes again
  if [[ -d $dir/meson-private ]]; then
    meson setup --reconfigure "$dir" "$@" | tail -n 25
  else
    meson setup "$dir" "$@" | tail -n 25
  fi
}

# GUI tests need a display: a private Xvfb for the life of this script.
start_x() {
  [[ -n ${DISPLAY:-} ]] && return
  Xvfb :99 -screen 0 1280x800x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
  local pid=$!
  for _ in $(seq 100); do
    [[ -e /tmp/.X11-unix/X99 ]] && break
    sleep 0.05
  done
  export DISPLAY=:99 GDK_BACKEND=x11
  # A private session bus, as every desktop has (GtkApplication registers its unique name).
  local bus
  bus=$(dbus-daemon --session --fork --print-address=1 --print-pid=1)
  export DBUS_SESSION_BUS_ADDRESS=${bus%%$'\n'*}
  trap "kill $pid ${bus##*$'\n'} 2>/dev/null" EXIT
  # No a11y bus or dconf in the container: keep GTK from trying (and warning).
  export NO_AT_BRIDGE=1 GTK_A11Y=none GSETTINGS_BACKEND=memory
  mkdir -p out/artifacts
}

debug() {
  start_x
  echo "== debug (ASan + UBSan) =="
  setup out/debug -Dbuildtype=debug -Db_sanitize=address,undefined
  meson compile -C out/debug
  meson test -C out/debug --print-errorlogs
}

analyze() {
  echo "== analyze (gcc -fanalyzer) =="
  setup out/analyze -Dbuildtype=debug -Danalyzer=true
  meson compile -C out/analyze
}

# The C sources clang-format owns: `check` refuses them unformatted, `format` rewrites them.
# vendor/ is not ours to format (RP-4).
format_files() {
  ls src/*.c src/*.h tests/*.c
}

format() {
  format_files | xargs clang-format -i
  echo "formatted $(format_files | wc -l) files"
}

# Every check, in one command: sanitizer tests, the analyzers, then the hardened build that
# ships, tested again.
check() {
  debug
  analyze
  echo "== vendored code =="
  python3 tools/check-vendor.py self-test
  python3 tools/check-vendor.py
  echo "== formatting (clang-format) =="
  format_files | xargs clang-format --dry-run --Werror
  echo "== source rules =="
  python3 tools/check-source.py self-test
  python3 tools/check-source.py
  echo "== static analysis (clang-tidy, cppcheck) =="
  python3 tools/analysis.py run
  python3 tools/analysis.py compare
  echo "== release (hardened) =="
  start_x
  # b_ndebug=false: assertions stay on in the shipped binary.
  setup out/release -Dbuildtype=debugoptimized -Db_ndebug=false -Dhardening=true
  meson compile -C out/release
  tools/check-binary.sh out/release/src/chandler
  meson test -C out/release --print-errorlogs
  # The GUI suites again, several times: a test that reads state before the main loop has
  # delivered it passes or fails by timing, and the fast release build is where it shows.
  meson test -C out/release --print-errorlogs --repeat 3 window
}

release() {
  check
  printf '%s\n' "$CHANDLER_VERSION" > out/release/released
  echo
  echo "built: out/release/src/chandler ($(out/release/src/chandler --version))"
}

case "${1:-}" in
  check) check ;;
  debug) debug ;;
  analyze) analyze ;;
  release) release ;;
  format) format ;;
  shell) exec bash ;;
  *) echo "unknown command: ${1:-}" >&2; exit 2 ;;
esac
