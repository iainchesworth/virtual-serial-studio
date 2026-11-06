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
end-to-end path, runs the tests under AddressSanitizer with UBSan, replays the
fuzz corpora, and analyses the workflows with zizmor. ThreadSanitizer runs for
merge-queue entries and pushes. Boost is fetched by `tools/fetch_boost.py`, so no
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
