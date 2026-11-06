# Mutation testing of the C++ core

`.github/workflows/mutation.yml` runs `tools/mutation/run_mutation.py` on every pull request that touches `core/**`, whatever its base (stacked PRs on a `claude/*` branch diff against that branch). It asks one question: if the C++ this PR changed were subtly wrong, would a host test notice?

## What it does

1. Diffs `origin/<base>...HEAD` under `core/` (`.cpp`, `.hpp`, `.h`) and keeps the added and changed lines. **`core/tests/` is excluded**: tests are never mutated.
2. Reads the host tests from `.github/workflows/android.yml`: each `g++ ... -o X` line (backslash continuations joined, `core/sound/*.cpp` globs expanded) paired with the `./X args` line in the same `run:` block. android.yml stays the one definition of how a host test builds and runs.
3. Keeps the tests whose build reaches a changed file: a listed `.cpp`, or a header any listed source includes, followed transitively through `#include "..."` and the test's `-I` dirs.
4. Builds each relevant test once, unmutated (one object per source, the same flags), and runs it. A test that is not green at baseline judges nothing and is named in the notes.
5. For each mutant, edits one line in a private copy of `core/`, recompiles only the objects that see that line, relinks and runs the tests. A failing test kills the mutant. A test that runs past `max(10 s, 10 × its baseline)` times it out. A mutant that does not compile is left out of the score.

Operators, applied on changed lines only and never inside comments or strings: `<`↔`<=`, `>`↔`>=`, `==`↔`!=`, `+`↔`-`, `*`↔`/`, `&&`↔`||` (binary operators only when spaced, so templates, `<<`, `->` and `T&&` are left alone), `if (c)`→`if (!(c))`, the literals `0`↔`1` and `x`→`(x + 1)`, `return x;`→`return {};`, and `std::clamp(v, lo, hi)`→`(v)`, `std::min/max(a, b)`→`(a)` or `(b)`.

## Engine: source mutator first, Mull opt-in

The primary engine is the small source mutator in the script. [Mull](https://mull.readthedocs.io) is supported behind `--engine mull` (or the workflow's manual run, `engine: mull`), for these reasons:

- **The same compiler and flags as CI.** The source engine reuses android.yml's g++ lines exactly. Mull needs clang with its `mull-ir-frontend-<N>` pass plugin, so every test would be built by a different compiler. Several tests use `-Werror`, and clang warns where g++ does not; the Mull path drops `-Werror`.
- **No package to trust or match.** Mull installs from a third-party Cloudsmith apt repo through a `curl | bash` setup script, and its `mull-<N>` package has to match an LLVM major that the runner image provides. That pairing moves with each Mull release and runner image (0.37.0 ships for Ubuntu 24.04 and 26.04). A silent mismatch would turn the check red for reasons unrelated to the PR.
- **Multi-file builds.** Mull's docs say `-grecord-command-line` does not work for several files in one go, and every host test is one multi-file g++ line. The Mull path splits each test into one compile per source to work around this.
- **The source engine scales the same way.** Only the touched objects are rebuilt per mutant, and the job runs one worker per core.

Mull's diff mode (`gitDiffRef`, `gitProjectRoot`, `gitDiffPaths` in `mull.yml`, passed through `MULL_CONFIG`) is wired up and worth trying when the source engine's operators run short. It has not been proven on CI yet.

## Reading the summary

The job summary has one row per changed file:

| Column | Meaning |
|---|---|
| Generated | Mutants made from the file's changed lines |
| Killed | A test failed: good |
| Survived | Every test passed: a gap |
| Timed out | Counted as killed (an infinite loop is noticed) |
| Not compiling | Left out of the score |
| Not run | No test builds the file, the baseline was red, or the cap or time budget was reached |

**Score** = (killed + timed out) / (killed + timed out + survived).

Each survivor is listed as `file:line  original → mutant`. To kill it, write or extend a test in `core/tests/` that passes on the original line and fails on the mutant. Some survivors are equivalent mutants: the mutated code behaves the same, for example `<`→`<=` where equality cannot happen. Ignore those, or simplify the code so the question disappears.

The full list, with the test that killed each mutant, is in the `mutation-report` artifact (`mutation-report.json`).

## Limits

- The run takes at most 300 mutants (`MUTATION_MAX_MUTANTS`), sampled round-robin across files, and at most 1200 s (`MUTATION_TIME_BUDGET`) inside a 25-minute job. The summary notes when either cap is hit.
- Only quoted includes are followed. A host test that is not in android.yml is invisible to the harness.

## Score target

The check is **report-only** for now: it exits 0 whatever the score. The proposal is to set the repository variable `MUTATION_MIN_SCORE` to `60` once a few PRs have shown what normal looks like, then raise it to `75` for `core/sound` and `core/viz` maths as the tests grow. Below the threshold, the job fails. A gate on changed lines only never punishes old code.

## Running it locally (dev machine only)

Never run it in an agent container: the owner rule is that CI builds and tests. On a dev machine with g++ and Python 3.10+:

```sh
git fetch origin main
python3 tools/mutation/run_mutation.py --base origin/main --dry-run   # list the mutants only
python3 tools/mutation/run_mutation.py --base origin/main --jobs 8    # run them
python3 -m unittest discover -s tools/mutation -p 'test_*.py'         # the harness's own tests
```

Other flags: `--max-mutants`, `--time-budget`, `--min-timeout`, `--timeout-factor`, `--engine mull --llvm 18`. Run `--help` for the full list.
