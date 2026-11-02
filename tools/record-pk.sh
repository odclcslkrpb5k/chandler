#!/usr/bin/env bash
# Drives the recording of real PackageKit traffic (plan §1.2 item 3), one step at a time.
# Run by the owner in a terminal of their own, from the repository root, while a second
# terminal records the system bus:
#
#   sudo dbus-monitor --system "sender='org.freedesktop.PackageKit'" \
#     "destination='org.freedesktop.PackageKit'" > out/pk-record/bus.txt
#
# Each step prints what to do, waits for Enter, and logs what pk-drive saw to
# out/pk-record/<step>.txt. Raw output stays in out/ (never committed): it is scrubbed into
# tests/data/pk/ afterwards. Installs only `hello`; remove it afterwards with
# `sudo apt remove hello`.
#
#   tools/record-pk.sh          every step
#   tools/record-pk.sh 05       only the steps whose names start with 05 (a re-recording),
#                               into out/pk-record-05/ (dbus-monitor's bus.txt goes there too)
set -uo pipefail
only=${1:-}
cd "$(dirname "${BASH_SOURCE[0]}")/.."
drive=out/release/tests/pk-drive
out=out/pk-record${only:+-$only}
[[ -x $drive ]] || { echo "build first: ./build check (makes $drive)"; exit 1; }
mkdir -p "$out"

step() {  # step <name> <instructions> <pk-drive args...>
  local name=$1 say=$2; shift 2
  [[ -z $only || $name == "$only"* ]] || return 0
  echo
  echo "=== $name"
  echo "$say"
  read -r -p "Press Enter to run: pk-drive $* "
  echo "### $(date -Is) pk-drive $*" >> "$out/steps.txt"
  "$drive" "$@" 2>&1 | tee "$out/$name.txt"
  echo "    (exit ${PIPESTATUS[0]}; saved $out/$name.txt)"
}

echo "Is the dbus-monitor terminal recording into $out/bus.txt? (Ctrl+C here if not)"
step 01-simulate "Simulate installing hello. Note whether a password prompt appears." simulate hello
step 02-dismiss "A password prompt will appear: DISMISS it (Cancel)." install hello
step 03-wrong-password "A password prompt will appear: type a WRONG password (each time it asks)." install hello
step 04-no-network "Turn the network OFF now (nm-applet / unplug), then press Enter. Authenticate when asked." install hello
echo "Turn the network back ON before the next step."
step 05-locked "Open Synaptic now and leave it open, then press Enter. Authenticate if asked; close Synaptic after the step ends (or after a minute of waiting)." install hello
step 06-install "Close Synaptic. Install hello for real: authenticate properly." install hello
step 07-installed-again "hello is installed now: resolve it again." resolve "installed;arch;~source" hello
step 08-eula "Simulate only, a package with a licence prompt (nothing is installed)." simulate ttf-mscorefonts-installer
echo
echo "Done. Stop dbus-monitor (Ctrl+C in its terminal), then remove hello: sudo apt remove hello"
