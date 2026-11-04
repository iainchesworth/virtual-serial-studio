# Releasing

A release is a `vMAJOR.MINOR.PATCH` tag on a `develop` commit, such as `v0.1.0`
or `v0.1.0-rc.1`. A tag with a suffix is a pre-release. The standard that this
follows is section 13 of `docs/standard.md` in `icl-common-infra`.

## What a release holds

`.github/workflows/release.yml` builds one package for each system and the
shared publish workflow adds the checks.

| Package | Built on | Holds |
|---|---|---|
| `virtual-serial-studio-<tag>-linux-x86_64.tar.gz` | Ubuntu 26.04 | `bin/vss_gui`, `bin/vss_cli`, the Qt libraries in `lib/` and `plugins/`, the licence texts |
| `virtual-serial-studio-<tag>-macos-arm64.tar.gz` | macOS 26 | `vss_gui.app`, `bin/vss_cli`, the licence texts |
| `virtual-serial-studio-<tag>-windows-x86_64.zip` | Windows 2025 | `bin/vss_gui.exe`, `bin/vss_cli.exe`, the Qt DLLs, the licence texts |

The release also has `SHA512SUMS`, a detached GPG signature (`.asc`) for every
file, an SPDX software bill of materials and a build provenance attestation for
every package. The Linux package is checked against glibc 2.39, so it starts on
Ubuntu 24.04. The libraries and headers (the `development` install component)
are not in the packages.

## Signing

| System | State |
|---|---|
| Windows | The `SIGN_WINDOWS` repository variable chooses: `false` (the default) leaves the files unsigned and names the package `-unsigned`, `test` signs with a leaf certificate from the owner's own certificate authority (`-testsigned`), and `production` is reserved for the signing service that has not been chosen yet |
| macOS | Not signed or notarised yet. Gatekeeper asks the user to allow the app |
| Linux | Not signed beyond the GPG signature on the file |

A test-signed Windows package is trusted only on a machine that has the owner's
certificate authority installed. The release notes say so.

## Making a release

1. Raise the version in `CMakeLists.txt` (`project(VERSION)`) and `vcpkg.json`
   in a pull request. `tools/check_release_version.py` fails the release when
   the tag and these two disagree.
2. Run `python3 tools/changelog.py --release <version>` to fold the fragments
   in `changelog.d/` into `CHANGELOG.md` for a final release, in the same pull
   request. A pre-release takes its notes from the pending fragments.
3. Tag the merged commit on `develop` and push the tag. The `package` jobs
   build, the owner approves the `release` environment, and the `publish` job
   signs, attests and publishes. For a final release it then moves `main` to
   the tag.

## Trying it first

`gh workflow run release.yml --ref develop` builds every package and keeps them
as workflow artifacts without publishing. The same run happens every Monday and
on a pull request that changes the workflow, so a change to the install rules
cannot go unseen for long. Unpack each package on its system and run
`vss_cli selftest`; start `vss_gui` on a machine with a display.

## What the owner provides

These are not in the repository and an agent does not create them.

- `docs/signing-key.asc`, the public GPG key whose private half is the
  organisation secret `REPO_GPG_PRIVATE_KEY`, and its fingerprint in
  `SECURITY.md` with the commands to verify a download.
- The organisation secrets `REPO_GPG_PASSPHRASE`, `RELEASE_BOT_APP_ID` and
  `RELEASE_BOT_PRIVATE_KEY`.
- The `release` environment with the owner as required reviewer.
- For a test-signed Windows package: `SIGN_WINDOWS=test` and the secrets
  `WIN_TESTSIGN_PFX_BASE64` and `WIN_TESTSIGN_PFX_PASSWORD`.

## Known gaps

- On Windows the GUI keeps a console window, because `vss_gui --selftest` has
  to print to its caller. A double click shows the console beside the window.
- The GUI package has no installer. Unpack it where you like.
