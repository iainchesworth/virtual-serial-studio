#!/usr/bin/env bash
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs one platform-matrix cell inside a Linux guest. tools/vm/guest-cell.ps1 copies this file
# and the bundle in and runs it; run it by hand only to repeat a cell.
#
#   linux-cell.sh <cell> <vm name> [commit]
#
# The bundle (tools/vm/make-bundle.sh) is /tmp/vss/bundle.tgz with bundle.env beside it: the
# source tree and the build tree, built outside the guest at the path they were configured
# with, and the Qt libraries and plugins they need. For T9, /tmp/vss/package.tar.gz.
# It leaves /tmp/vss/cell-out.tgz.

set -uo pipefail
cell="$1"
vm_name="$2"
commit="${3:-}"
root=/tmp/vss

# shellcheck disable=SC1091
. "$root/bundle.env"   # SOURCE_ROOT, BUILD_DIR, PYTHON

# What the program needs that a minimal server install lacks: cmake for ctest, and the
# libraries Qt loads even on the offscreen platform.
packages=(cmake libgl1 libegl1 libopengl0 libfontconfig1 libxkbcommon0 libdbus-1-3 fonts-dejavu-core
  xvfb xauth libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0
  libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0)
missing=()
for package in "${packages[@]}"; do
  dpkg -s "$package" > /dev/null 2>&1 || missing+=("$package")
done
if [ "${#missing[@]}" -gt 0 ]; then
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${missing[@]}"
fi

if [ ! -d "$SOURCE_ROOT" ]; then
  tar -xzf "$root/bundle.tgz" -C /
fi
# The build holds the path of the Python that configured it; make that path work here.
if [ ! -x "$PYTHON" ]; then
  sudo mkdir -p "$(dirname "$PYTHON")"
  sudo ln -sf "$(command -v python3)" "$PYTHON"
fi

export LD_LIBRARY_PATH="$root/qtship/lib" QT_PLUGIN_PATH="$root/qtship/plugins" QT_QPA_PLATFORM=offscreen
# The GUI tests write a picture of each window and dialog they check, in both themes, when this
# is set. They come back with the cell output, rendered with this system's own fonts and style.
export VSS_QA_SHOTS="$root/gui-shots"
rm -rf "$VSS_QA_SHOTS"
cd "$SOURCE_ROOT" || exit 1
ctest --test-dir "$BUILD_DIR" --output-on-failure -j2 > "$root/t1.log" 2>&1
t1=$?
echo "ctest exit $t1"
tail -12 "$root/t1.log"

# A few GUI tests wait a fixed few milliseconds for an event, which a slow guest can miss. Run the
# failed suites once more, and say so in the result rather than hide it.
note=""
if [ "$t1" -ne 0 ]; then
  failed="$(sed -nE 's/^[[:space:]]+[0-9]+ - ([^ ]+) \(.*/\1/p' "$root/t1.log" | paste -sd, -)"
  echo "rerunning the failed suites once: $failed"
  ctest --test-dir "$BUILD_DIR" --output-on-failure --rerun-failed >> "$root/t1.log" 2>&1
  t1=$?
  note="first pass failed $failed; the rerun $([ "$t1" -eq 0 ] && echo passed || echo failed too)"
fi

rm -rf "$root/cell-out"
arguments=(tools/matrix/run_cell.py --cell "$cell" --kind vm --vm "$vm_name" --snapshot clean
  --build-dir "$BUILD_DIR" --out "$root/cell-out" --t1-exit "$t1" --t1-log "$root/t1.log")
[ -n "$commit" ] && arguments+=(--commit "$commit")
[ -n "$note" ] && arguments+=(--t1-note "$note")
[ -f "$root/package.tar.gz" ] && arguments+=(--package "$root/package.tar.gz")
python3 "${arguments[@]}"
echo "cell exit $?"
[ -d "$root/gui-shots" ] && cp -r "$root/gui-shots" "$root/cell-out/gui-shots"
tar -C "$root" -czf "$root/cell-out.tgz" cell-out
exit 0
