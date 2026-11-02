#!/usr/bin/env bash
# Copy the CMake builds into the portable foobar2000 installs next to the repo.
set -euo pipefail
cd "$(dirname "$0")"
repo=..
deploy() { # build-dir fb2k-dir user-components-subdir
  [ -f "$2/foobar2000.exe" ] || { echo "skip: $2 not found"; return; }
  local dst="$2/profile/$3/foo_winamp_skins"
  mkdir -p "$dst" && cp "$1/foo_winamp_skins.dll" "$dst/" && echo "-> $dst"
}
deploy build-x64 $repo/../foobar2000_x64 user-components-x64
deploy build-x86 $repo/../foobar2000_x32 user-components
