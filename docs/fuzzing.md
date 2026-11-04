# Fuzzing

Code that reads input from outside has a libFuzzer harness in
`libs/<lib>/fuzz/`, with a seed corpus in `corpus/` and the inputs of past
crashes in `regressions/`.

| Harness | Reads |
|---|---|
| `libs/vss_config/fuzz/fuzz_topology.cpp` | A saved topology (JSON). It must be rejected or accepted without a crash, and the text written for an accepted one must read back and write again unchanged |
| `libs/vss_ports/fuzz/fuzz_endpoint_uri.cpp` | An endpoint URI such as `tcp://host:4001` or `serial://COM3?baud=9600` |

## Where it runs

| Run | When | What |
|---|---|---|
| Replay in the normal tests | Every build | CTest tests `fuzz_topology` and `fuzz_endpoint_uri` (label `fuzz`) run each corpus and regression file once. They work with any compiler and run under AddressSanitizer in the `linux-asan-ubsan` job |
| `fuzz-regress` | Every pull request | The `Fuzz regression replay` job builds the libFuzzer targets and replays the same files under libFuzzer |
| `fuzz-short` | Push to `develop` | 60 seconds a target (`.github/workflows/fuzz.yml`) |
| `fuzz-nightly` | Every night | 10 minutes a target; the working corpus is kept between runs in the Actions cache |

A crash fails the run, uploads the input as the `fuzz-crashes` artifact and, at
night, opens or updates a `nightly-analysis` issue.

## Running it yourself

Use Clang with libFuzzer. Configure with `CC=clang CXX=clang++`.

```bash
cmake --preset linux-fuzz
cmake --build --preset linux-fuzz
python3 tools/fuzz.py --mode short
```

`--mode regress` only replays the files, `--mode nightly` fuzzes for ten
minutes a target, and `--seconds` sets another time. New inputs go to
`.cache/fuzz/<harness>`, which Git ignores; crash files go to
`.cache/fuzz/artifacts`.

## After a crash

Fix the bug, then copy the crashing input into the harness's `regressions/`
directory with the fix, so the replay checks it from then on. Add a harness
for any new function that parses untrusted input: create `fuzz_<thing>.cpp`
beside the others, add a seed corpus and a `regressions/` directory (keep it in
Git with an empty `.gitkeep`), and declare it in `tests/fuzz/CMakeLists.txt`.
