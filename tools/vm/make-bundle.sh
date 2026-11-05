#!/usr/bin/env bash
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the programs for a Linux guest cell outside the guest and packs what the guest needs.
#
#   tools/vm/make-bundle.sh <repository> <output folder>
#
# Needs a Linux build environment (a compiler, CMake, Boost, Qt: the same as
# `cmake --preset linux-release-gui`), with Qt found through VSS_QT_ROOT, ICL_QT_ROOT,
# QT_ROOT_DIR or QTDIR. It writes bundle.tgz and bundle.env in the output folder, which
# tools/vm/guest-cell.ps1 copies into the guest. The guest has no compiler, so a guest cell
# proves that the build runs and passes on that system, not that the guest can build it.

set -euo pipefail

repository="$(realpath "${1:?usage: make-bundle.sh <repository> <output folder>}")"
output="$(realpath -m "${2:?usage: make-bundle.sh <repository> <output folder>}")"
preset=linux-release-gui

qt_root="${VSS_QT_ROOT:-${ICL_QT_ROOT:-${QT_ROOT_DIR:-${QTDIR:-}}}}"
[ -d "$qt_root" ] || { echo "make-bundle.sh: set VSS_QT_ROOT to the Qt installation" >&2; exit 1; }

# The tests read sources through the absolute path they were built with, so the guest gets the
# same path. The Qt files go to /tmp/vss/qtship, where linux-cell.sh looks for them.
source_root=/tmp/vss-bundle/src
ship=/tmp/vss/qtship
rm -rf /tmp/vss-bundle "$ship"
mkdir -p "$source_root" "$ship/lib" "$ship/plugins" "$output"

(cd "$repository" && tar --exclude=.git --exclude=build -cf - .) | tar -xf - -C "$source_root"
cd "$source_root"
cmake --preset "$preset"
cmake --build --preset "$preset" -j "$(nproc)"

# Every Qt library any built program loads, and the plugins with the libraries they load.
libraries() { ldd "$@" 2> /dev/null | awk -v root="$qt_root" 'index($3, root) == 1 {print $3}' | sort -u; }
for program in "build/$preset/bin/"*; do
  [ -f "$program" ] && [ -x "$program" ] && libraries "$program" | while read -r library; do cp -L "$library" "$ship/lib/"; done
done
for folder in platforms imageformats iconengines styles platformthemes tls; do
  [ -d "$qt_root/plugins/$folder" ] && cp -r "$qt_root/plugins/$folder" "$ship/plugins/"
done
for plugin in "$ship"/plugins/*/*.so; do
  libraries "$plugin" | while read -r library; do cp -Ln "$library" "$ship/lib/"; done
done

python_path="$(grep -rhoE '"/[^" ]*python3[^" ]*"' "build/$preset" --include=CTestTestfile.cmake | tr -d '"' | sort -u | head -1 || true)"
cat > "$output/bundle.env" << EOF
SOURCE_ROOT=$source_root
BUILD_DIR=$source_root/build/$preset
PYTHON=${python_path:-/usr/bin/python3}
EOF
tar -C / -czf "$output/bundle.tgz" "${source_root#/}" "${ship#/}"
echo "wrote $output/bundle.tgz ($(du -h "$output/bundle.tgz" | cut -f1)) and bundle.env"
