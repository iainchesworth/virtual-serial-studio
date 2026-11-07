# Continuous integration

Development lands on `develop`, the trunk and default branch, through pull
requests and a merge queue. `main` is the release branch: the release workflow
moves it to each final release tag.

## The pull request gate

`.github/workflows/ci.yml` first runs the shared policy checks (branch name,
commit and pull request text, file text, action pinning, changelog fragment,
vcpkg baseline and REUSE) and a planner that skips the builds for
documentation-only changes. It then builds with GCC, Clang, AppleClang and
MSVC, runs the GUI self-test and journey tests on Linux with Qt, enforces the
coverage gates, runs the performance floors, captures a Tracy profile of the
end-to-end path, runs the tests, the GUI suites included, under
AddressSanitizer with UBSan, replays the fuzz corpora, and analyses the
workflows with zizmor. ThreadSanitizer, GUI suites included, runs when threading
code changed and for merge-queue entries (see
[Testing](testing.md#sanitizers)). Boost is fetched by `tools/fetch_boost.py`, so no
job depends on a system Boost.

The branch protection ruleset requires two checks, `CI Status` and
`Scan dependency diff`. `CI Status` fails when any job the plan asked for failed,
was cancelled or was skipped.

## Other workflows

| Workflow | Runs | Does |
|---|---|---|
| `fuzz.yml` | Push to `develop`, every night | Fuzzes each target for 60 seconds, or 10 minutes at night; see [Fuzzing](fuzzing.md) |
| `release.yml` | A `v*` tag, every Monday, on request | Builds the packages and publishes them for a tag; see [Releasing](releasing.md) |
| `docs.yml` | A pull request that changes the docs, a push to `main` | Builds this site with `mkdocs build --strict` and, from `main`, publishes it |
| `dependabot-auto-merge.yml` | Dependabot pull requests | Turns on auto-merge for minor and patch updates of actions |

## Windows signing

Release packaging and the driver job sign the Windows programs with the
`sign-windows` action of `icl-common-infra`, through the local action
`.github/actions/sign-windows-binaries`, which also verifies the result. The
`SIGN_WINDOWS` repository variable chooses the mode (`false`, `test`,
`production`); the secrets `WIN_TESTSIGN_PFX_BASE64` and
`WIN_TESTSIGN_PFX_PASSWORD` hold the test certificate.

- **Never on a pull request build** of the packages (`release.yml` passes mode
  `false` on a pull request), and never for a pull request from a fork, which has no
  secrets.
- **Without the secrets** the step writes a warning and a note in the job summary
  and leaves the files unsigned; the leg passes.
- **With them** every file is checked: a signature, an RFC 3161 timestamp and an
  intact hash, plus a manifest that never asks for administrator rights. A
  failure fails the leg. In `production` mode the chain must also be trusted
  and `signtool verify /pa` must pass.

`tools/ci/verify-windows-signatures.ps1` runs by hand too. How to test it with a
self-signed certificate is in [the Windows driver notes](windows-com-driver.md#signing-the-studios-windows-programs).

## Nightly analysis

These run on a schedule and on request, never on a pull request, so a finding
never blocks a merge. Only CodeQL and MSVC code analysis report to Security >
Code scanning. The others fail their job and open or update one issue labelled
`nightly-analysis`; the owner closes it once the findings are fixed.

| Analysis | Workflow | Where the result lands |
|---|---|---|
| CodeQL: C++ (libraries, command line tool, GUI), Python and Actions | `codeql.yml` | Code scanning alerts; a failed run opens an issue |
| MSVC code analysis (PREfast) over the Windows build | `msvc-analysis.yml` | Code scanning alerts under the tool PREfast; new alerts open an issue |
| clang-tidy: bugprone, concurrency, performance, portability, misc, analyzer | `static-analysis.yml` | The job and an issue |
| Newest GCC and Clang, GCC ASan/UBSan, TSan, MSVC ASan | `nightly.yml` | The job and an issue |
| Fuzzing, 10 minutes per target | `fuzz.yml` | The job and an issue |
| Hosted platform matrix | `platform-matrix.yml` | The job and an issue |

The sanitizers, clang-tidy and fuzzing do not upload to code scanning. Their
findings are failed tests and reproducers, not alerts that can be dismissed.
Include-what-you-use, OSV-Scanner and OpenSSF Scorecard are not run yet.
