#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sign vss_gui.app and vss_cli with the hardened runtime, submit the app to
# Apple's notary service and staple the ticket, so Gatekeeper opens the download
# without a warning. Run it on a Mac, by the owner, after the Release workflow
# has built the package (docs/releasing.md, "macOS signing and notarisation").
#
#   tools/macos/sign-and-notarize.sh --app path/to/vss_gui.app \
#       [--cli path/to/vss_cli] [--dry-run]
#
# What it needs comes from the environment. Nothing secret is in this repository
# and the script never prints a secret:
#
#   VSS_SIGN_IDENTITY   the name of a "Developer ID Application" certificate in
#                       the keychain, for example
#                       "Developer ID Application: Example Ltd (ABCDE12345)"
#   VSS_NOTARY_PROFILE  the name of a notarytool credential profile, made once
#                       with "xcrun notarytool store-credentials"
#
# --dry-run prints each command and runs none of them.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
entitlements="${here}/../../apps/vss_gui/macos/vss_gui.entitlements"

app=""
cli=""
dry_run=0
while [ $# -gt 0 ]; do
  case "$1" in
    --app) app="${2:?--app needs a path}"; shift 2 ;;
    --cli) cli="${2:?--cli needs a path}"; shift 2 ;;
    --dry-run) dry_run=1; shift ;;
    -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -z "${app}" ]; then
  echo "--app is required" >&2
  exit 2
fi
: "${VSS_SIGN_IDENTITY:?set VSS_SIGN_IDENTITY to the Developer ID Application certificate name}"
: "${VSS_NOTARY_PROFILE:?set VSS_NOTARY_PROFILE to a notarytool credential profile}"

run() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  if [ "${dry_run}" -eq 0 ]; then
    "$@"
  fi
}

sign() {
  # --options runtime is the hardened runtime, which notarisation requires.
  # --timestamp asks Apple's timestamp server, which notarisation also requires.
  run codesign --force --timestamp --options runtime \
    --entitlements "${entitlements}" --sign "${VSS_SIGN_IDENTITY}" "$1"
}

# Sign from the inside out: every nested library, plug-in and framework first,
# then the program, then the bundle. A signature on the outside does not cover
# code that was signed with another identity (or ad hoc, as macdeployqt does).
if [ -d "${app}" ]; then
  while IFS= read -r nested; do
    sign "${nested}"
  done < <(find "${app}/Contents" \( -name '*.dylib' -o -name '*.so' \) -type f | sort)
  while IFS= read -r framework; do
    sign "${framework}"
  done < <(find "${app}/Contents/Frameworks" -maxdepth 1 -name '*.framework' -type d 2>/dev/null | sort)
  sign "${app}"
else
  echo "not a directory: ${app}" >&2
  exit 1
fi
if [ -n "${cli}" ]; then
  sign "${cli}"
fi

run codesign --verify --deep --strict --verbose=2 "${app}"

# Notarise a zip of the app, then staple the ticket to the app itself.
zip="$(mktemp -d)/vss_gui.zip"
run ditto -c -k --keepParent "${app}" "${zip}"
run xcrun notarytool submit "${zip}" --keychain-profile "${VSS_NOTARY_PROFILE}" --wait
run xcrun stapler staple "${app}"
run xcrun stapler validate "${app}"
run spctl --assess --type execute --verbose=2 "${app}"
if [ "${dry_run}" -eq 0 ]; then
  echo "done: ${app} is signed and notarised"
else
  echo "dry run: nothing was signed or submitted"
fi
