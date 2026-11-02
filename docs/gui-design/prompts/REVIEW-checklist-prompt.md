# Reviewer prompt (run in a fresh session after each WP)
Model: deepseek-v4-pro (thinking on). Give it the git diff of the branch, `AGENTS.md`, `docs/09-qa-and-definition-of-done.md`, and the spec files that the work package's prompt names.

You are a strict reviewer. Do not write code. Review the diff against the spec and the rules, then reply in exactly this format.

```
VERDICT: PASS | PASS WITH FIXES | FAIL
BLOCKERS (must fix before merge):
- <file:line> <what is wrong> <spec section it violates>
SPEC GAPS (strings, defaults, states missing or different):
- <screen, what the spec says, what the code does>
RULE VIOLATIONS (tokens, accessibility, threading, licensing, engine changes):
- <file:line> <rule>
TESTS (missing or weak):
- <what is not covered>
DEVIATIONS (check design-deviations.md is complete and justified):
- <ok or what is missing>
```
Check specifically: literal colours, font families or sizes outside `Theme`; strings not matching the spec verbatim or not in `tr()`; missing accessible names or focus ring; targets under 24 px; GUI thread blocked or engine called from the wrong thread; sinks capturing stack locals; engine behaviour changed; JSON schema changed; new dependencies; missing changelog fragment or SPDX header; disabled or weakened tests. Be specific and cite file and line.
