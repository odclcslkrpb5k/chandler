# Build environment for Chandler. Everything (build, tests, analyzers) runs in here;
# nothing on the workstation matters beyond Docker itself.
#
# The base must match the host distro release: the binary links against the system
# glibc, GTK, libappstream and packagekit-glib2 at runtime, so we build against the same
# versions the host runs. When the host upgrades, bump this digest (docker pull ubuntu:<ver>;
# docker image inspect).
FROM ubuntu:26.04@sha256:f144425ff09be612d6d9ad965196e9cdc23dae1f42110a8a11a3e9a8198759f7

# Every package comes from one dated snapshot of the Ubuntu archive, so a rebuild next year
# installs the same versions; move the date to take updates. ca-certificates alone comes
# from the live archive: the snapshot service is HTTPS only, and the base image trusts no
# certificate. --error-on=any: without it, apt-get update exits 0 when an index fails to
# download, and the install then quietly uses whatever lists it has.
ARG SNAPSHOT=20261016T000000Z
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update --error-on=any \
 && apt-get install -y --no-install-recommends ca-certificates \
 && apt-get update --error-on=any --snapshot "$SNAPSHOT" \
 && apt-get install -y --no-install-recommends \
      gcc libc6-dev meson ninja-build pkgconf \
      clang llvm libclang-rt-dev clang-tidy clang-format cppcheck \
      desktop-file-utils appstream \
      libgtk-3-dev libappstream-dev libpackagekit-glib2-dev \
      python3 python3-dbusmock \
      xvfb xauth dbus-daemon \
      fonts-dejavu-core adwaita-icon-theme librsvg2-common \
 && rm -rf /var/lib/apt/lists/*

# A decoy "system" catalog: tests that load only their fixture must not see it, and one test
# loads the system catalog to prove it is there (tests/test-catalog.c). Without it, a fixture
# test could never notice the system's catalog joining in, since the image has no other.
# Its summary carries a terminal escape and a bidirectional override (YAML "\e", "\u202e")
# for the end-to-end --list test (tests/check-list.py).
RUN mkdir -p /usr/share/swcatalog/yaml \
 && printf '%s\n' '---' 'File: DEP-11' "Version: '1.0'" 'Origin: decoy-system' '---' \
      'Type: desktop-application' 'ID: chandler.decoy.System' 'Package: decoy-system-viewer' \
      'Name:' '  C: Decoy System Viewer' 'Summary:' \
      '  C: "From the image\e]0;pwned\a never\u202ethe fixture"' \
      'Provides:' '  mediatypes:' '  - application/pdf' \
      > /usr/share/swcatalog/yaml/decoy-system.yml

WORKDIR /src
ENTRYPOINT ["/src/tools/in-container.sh"]
