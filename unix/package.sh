#!/usr/bin/env bash
# Build .fb2k-component from the CMake builds: x86 DLL at the root, x64 DLL in x64/.
set -euo pipefail
cd "$(dirname "$0")"
repo=..
ver=$(grep -oP 'COMPONENT_VERSION\s+"\K[^"]+' $repo/src/version.h)
stage=$(mktemp -d); trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/x64" $repo/out
cp build-x86/foo_winamp_skins.dll "$stage/"
cp build-x64/foo_winamp_skins.dll "$stage/x64/"
cp $repo/LICENSE "$stage/LICENSE.txt"
cp $repo/3rdParty/miniz/LICENSE "$stage/LICENSE-miniz.txt"
pkg="$repo/out/foo_winamp_skins-$ver.fb2k-component"
rm -f "$pkg"
pkg=$(realpath -m "$pkg")
(cd "$stage" && 7z a -tzip -bso0 "$pkg" .)
echo "$pkg"
