# Virtual Serial Studio GUI: agent implementation bundle

Everything a set of coding agents needs to implement the redesigned Qt 6 GUI in `apps/vss_gui`, plus how to run them on DeepSeek.

Visual design canvas (37 boards, light and dark): https://claude.ai/artifact/QaFTB9zSfLqAMoxyfS5Niy . It is for humans. The agents work from the text in this bundle.

## Contents
```
README.md                      this file
agents/AGENTS.md               rules loaded by every agent (copy to the repo root)
agents/deepseek/               DeepSeek configuration, profiles, smoke test, runner
prompts/                       orchestration guide, shared preamble, work packages WP0 to WP9, reviewer prompt
docs/00 to 09                  brief, principles and WCAG checklist, tokens and Qt architecture,
                               information architecture, component library, accessibility,
                               copy deck, engine gaps and adapter rule, QA and definition of done
docs/screens/01 to 06          one spec per screen group: exact copy, defaults, rules, acceptance
tokens/tokens.json             colours (light and dark), type, size, layout, motion, contrast results
tokens/style.qss.in            Qt style sheet template with placeholders
tokens/style.light.qss, style.dark.qss   generated previews (do not commit these to the repo)
icons/*.svg                    26 icons, 16px, currentColor
reference/mockups/*.html       37 mockups as HTML for exact pixel sizes (reference only, never ship)
reference/vss2.css             the mockups' shared styles
reference/canvas.json          board sizes
tools/check_contrast.py        recomputes every contrast pair, exits 1 on failure
tools/gen_qss.py               renders the QSS template for both themes and checks placeholders
```

## How to run it
1. Set up DeepSeek with `agents/deepseek/README.md`, and run `smoke-test.sh`.
2. Read `prompts/00-orchestration.md`. The order is WP0, WP1, WP2, then WP3 to WP8 in parallel, then WP9.
3. For each package: fresh session, `AGENTS.md` plus `prompts/_common-preamble.md` plus the package prompt. Build and test it yourself. Run `prompts/REVIEW-checklist-prompt.md` in a fresh session against the diff. Merge only when both pass.
4. WP0 copies this bundle into the repo as `docs/gui-design/` and writes `implementation-plan.md`. Read that plan before releasing WP3 to WP8. It is where the design meets the real code.

## Things to know before you start
- The design was made from the project's README and docs, not from the Qt source. `docs/08-engine-gaps.md` is a hypothesis that WP0 must verify. Where the engine lacks a capability, the GUI degrades visibly rather than faking it.
- Some designs make claims the engine may not be able to support, notably "Waiting for program" (needs peer-open detection), the per-link line matrix, undo, and rename. Each has a fallback in the specs.
- The mockups are static. Numbers, port names and byte counts in them are samples. Placeholders such as `<version>` are filled at runtime.
- The project docs disagree about whether a standard null-modem preset also wires DTR to DCD. The specs include it. Confirm with the maintainer.
- `tools/check_contrast.py` currently reports `FAILURES: 0` for both themes.
