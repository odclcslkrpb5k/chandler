#!/usr/bin/env bash
# Install Chandler into ~/.local (no root needed):
#
#   curl -fsSL https://raw.githubusercontent.com/odclcslkrpb5k/chandler/main/install.sh | bash
#
# Environment (all optional):
#   CHANDLER_VERSION=<hash>   install that release instead of the latest
#   CHANDLER_PREFIX=<dir>     install under <dir> instead of ~/.local
#   CHANDLER_FROM=<dir>       install files from <dir> instead of downloading (./build install)
#   CHANDLER_ANY_OS=1         skip the Ubuntu 26.04 check
#   CHANDLER_NO_SHIM=1        don't install the gnome-software shim
#   CHANDLER_UNINSTALL=1      remove what this script installed, and nothing else
#
# The shim is <prefix>/bin/gnome-software, a two-line script that runs Chandler: GTK's "Find
# New Applications" button runs gnome-software, so the button opens Chandler. It is written
# only where it can't hide anything: never over a file it didn't write (it carries a marker
# line), and not at all while a real gnome-software is on PATH.
#
# Everything is inside main(), called on the last line: if the download of this script is
# cut short, bash has nothing complete to run.
set -euo pipefail

main() {
  local repo=odclcslkrpb5k/chandler
  local prefix=${CHANDLER_PREFIX:-$HOME/.local}
  local version=${CHANDLER_VERSION:-}
  local app_id=io.github.odclcslkrpb5k.Chandler
  local marker='# chandler-shim:'

  say() { printf '%s\n' "$*" >&2; }
  die() { say "chandler install: $*"; exit 1; }

  # The prefix goes verbatim into the .desktop Exec line, the shim and a sed replacement:
  # allow only characters that need no quoting in any of them.
  [[ $prefix == /* && ! $prefix =~ [^A-Za-z0-9/._+-] ]] \
    || die "install location must be an absolute path of [A-Za-z0-9/._+-]: $prefix"
  [[ -z $version || $version =~ ^[0-9a-f]{12}$ ]] \
    || die "CHANDLER_VERSION must be a 12-character build hash, got: $version"

  local bindir=$prefix/bin appdir=$prefix/share/applications
  local icondir=$prefix/share/icons/hicolor/scalable/apps
  local desktop=$app_id.desktop icon=$app_id.svg shim=$bindir/gnome-software

  # A shim of Chandler's: a regular file (not a symlink) whose second line is the marker.
  is_shim() { [[ -f $1 && ! -L $1 && $(sed -n 2p "$1") == "$marker"* ]]; }
  is_our_shim() { is_shim "$shim"; }
  # Anything else at the shim's path is someone else's: a file, a directory, or a symlink,
  # dangling or not.
  is_foreign() { [[ -e $shim || -L $shim ]] && ! is_our_shim; }

  if [[ -n ${CHANDLER_UNINSTALL:-} ]]; then
    rm -f "$bindir/chandler" "$appdir/$desktop" "$icondir/$icon"
    say "removed $bindir/chandler, $appdir/$desktop, $icondir/$icon"
    if is_our_shim; then
      rm -f "$shim"
      say "removed $shim"
    elif is_foreign; then
      say "left $shim alone: Chandler didn't write it"
    fi
    return 0
  fi

  local src
  if [[ -n ${CHANDLER_FROM:-} ]]; then
    src=$CHANDLER_FROM
  else
    # Release binaries are built on Ubuntu 26.04 (x86_64) against its GTK, GLib,
    # libappstream and packagekit-glib2.
    [[ $(uname -m) == x86_64 ]] || die "release binaries are x86_64 only (this is $(uname -m))"
    if [[ -z ${CHANDLER_ANY_OS:-} ]]; then
      local id="" ver=""
      if [[ -r /etc/os-release ]]; then
        # shellcheck source=/dev/null
        id=$(. /etc/os-release && printf '%s' "${ID:-}")
        # shellcheck source=/dev/null
        ver=$(. /etc/os-release && printf '%s' "${VERSION_ID:-}")
      fi
      [[ $id == ubuntu && $ver == 26.04 ]] || die "release binaries are built for Ubuntu 26.04;
this is ${id:-unknown} ${ver:-}. They may not start here. To try anyway: CHANDLER_ANY_OS=1.
Or build from source: see https://github.com/$repo#building"
    fi
    command -v curl >/dev/null || die "curl is required"
    command -v sha256sum >/dev/null || die "sha256sum is required"

    local base
    if [[ -n $version ]]; then
      base=https://github.com/$repo/releases/download/$version
    else
      base=https://github.com/$repo/releases/latest/download
    fi
    src=$(mktemp -d)
    # Expanded now: `src` is local and gone by the time the EXIT trap runs.
    # shellcheck disable=SC2064
    trap "rm -rf '$src'" EXIT
    local f
    for f in chandler "$desktop.in" "$icon" gnome-software.in SHA256SUMS; do
      curl -fsSL --proto '=https' --tlsv1.2 -o "$src/$f" "$base/$f" \
        || die "download failed: $base/$f"
    done
    # Catches corrupted or truncated downloads. (The sums come from the same release, so
    # this is not a defence against a compromised GitHub account.)
    (cd "$src" && sha256sum --quiet --strict -c SHA256SUMS) || die "checksum mismatch"
    for f in chandler "$desktop.in" "$icon" gnome-software.in; do
      grep -qE "^[0-9a-f]{64}  $f\$" "$src/SHA256SUMS" || die "SHA256SUMS doesn't list $f"
    done
  fi

  [[ -f $src/chandler && -f $src/$desktop.in && -f $src/$icon && -f $src/gnome-software.in ]] \
    || die "missing files in $src"
  chmod 755 "$src/chandler"
  local got
  got=$("$src/chandler" --version) || die "the binary doesn't run here (missing libraries?)"

  mkdir -p "$bindir" "$appdir" "$icondir"
  # Copy beside the target, then rename over it: atomic, and running instances keep the old
  # file instead of seeing a half-written one.
  cp "$src/chandler" "$bindir/.chandler.new"
  chmod 755 "$bindir/.chandler.new"
  mv -f "$bindir/.chandler.new" "$bindir/chandler"
  sed "s|@BIN@|$bindir/chandler|g" "$src/$desktop.in" > "$appdir/.$desktop.new"
  mv -f "$appdir/.$desktop.new" "$appdir/$desktop"
  cp "$src/$icon" "$icondir/.$icon.new"
  mv -f "$icondir/.$icon.new" "$icondir/$icon"
  touch "$prefix/share/icons/hicolor"     # tells icon caches to look again
  say "installed $got"
  say "  $bindir/chandler"
  say "  $appdir/$desktop"
  say "  $icondir/$icon"

  # The shim. Another gnome-software on PATH is the real one (or someone's own): the shim
  # would hide it if it came first, and be useless if it came after, so either way: none.
  # A shim of Chandler's found there (this one under another spelling of its directory, or
  # another prefix's) is not "another".
  local other="" dir dirs
  IFS=: read -ra dirs <<<"$PATH"
  for dir in "${dirs[@]}"; do
    if [[ -n $dir && -f $dir/gnome-software && -x $dir/gnome-software ]] \
      && ! is_shim "$dir/gnome-software"; then
      other=$dir/gnome-software
      break
    fi
  done
  if [[ -n ${CHANDLER_NO_SHIM:-} ]]; then
    say "no gnome-software shim (CHANDLER_NO_SHIM)"
  elif is_foreign; then
    say "no gnome-software shim: $shim is already there and Chandler didn't write it"
  elif [[ -n $other ]]; then
    say "no gnome-software shim: $other is installed, and the shim would hide it"
    if is_our_shim; then
      rm -f "$shim"
      say "removed the earlier shim $shim"
    fi
  else
    sed "s|@BIN@|$bindir/chandler|g" "$src/gnome-software.in" > "$bindir/.gnome-software.new"
    chmod 755 "$bindir/.gnome-software.new"
    mv -f "$bindir/.gnome-software.new" "$shim"
    say "  $shim (GTK's \"Find New Applications\" button now opens Chandler)"
    case ":$PATH:" in
      *":$bindir:"*) ;;
      *) say "note: $bindir isn't on PATH here, so apps won't find the shim until it is" ;;
    esac
  fi
}

main "$@"
