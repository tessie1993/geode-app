#!/usr/bin/env python3
"""Fast pre-push checks, so a branch doesn't wait in the CI queue only to fail on something seconds can catch.

Usage: python3 tools/precheck.py [base]   (base defaults to origin/main; run from the repo root)

What it runs (the owner allows these locally; Gradle, the APK, lint and the emulator stay CI-only):
  1. No conflict markers in files the branch changed.
  2. `{`/`}` balance in changed C/C++ files (a lost brace broke every PR once, #477).
  3. The two gates exactly as arch-map.yml runs them: engine Kotlin only shrinks, and the layer rule.
  4. Every native host test, taken from .github/workflows/android.yml's steps (the g++ line and the
     ./binary line that runs it), so this never drifts from CI. Binaries go to a temp dir; the tests
     run in parallel. Only tests whose sources the branch touched run, unless --all is given.

Exit code 0 means CI's gates and host tests should pass; it says nothing about the Gradle build,
Kotlin unit tests, lint, shaders or the emulator.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys
import tempfile

import yaml

ROOT = os.getcwd()
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
ALL = "--all" in sys.argv
BASE = ARGS[0] if ARGS else "origin/main"
HOST_STEPS = ("host tests", "export DSP tests")


def sh(cmd, inp=None):
    return subprocess.run(cmd, shell=True, input=inp, capture_output=True, text=True, cwd=ROOT)


def changed():
    mb = sh(f"git merge-base {BASE} HEAD").stdout.strip()
    names = sh(f"git diff --name-only {mb} HEAD").stdout.split()
    names += sh("git diff --name-only HEAD").stdout.split()  # uncommitted edits count too
    return mb, sorted(set(n for n in names if os.path.exists(n)))


def host_tests():
    """(name, compile command, run command, source files) for each host test in android.yml."""
    wf = yaml.safe_load(open(".github/workflows/android.yml"))
    tests = []
    for job in wf["jobs"].values():
        for step in job.get("steps", []):
            if not any(k in step.get("name", "") for k in HOST_STEPS):
                continue
            text = re.sub(r"\\\n\s*", " ", step.get("run", ""))
            lines = [l.strip() for l in text.splitlines() if l.strip() and not l.strip().startswith("#")]
            for i, line in enumerate(lines):
                m = re.match(r"(g\+\+|clang\+\+)\s.*-o\s+(\S+)\s*$", line)
                if not m:
                    continue
                exe = m.group(2)
                run = next((l for l in lines[i + 1:] if l.startswith("./" + exe)), None)
                if run:
                    srcs = re.findall(r"(core/\S+\.(?:cpp|hpp|h|inc))", line)
                    tests.append((exe, line, run, srcs))
    return tests


def main():
    mb, files = changed()
    fails = []

    marks = [f for f in files if sh(f"grep -lE '^(<<<<<<<|>>>>>>>)( |$)' -- '{f}'").stdout]
    if marks:
        fails.append("conflict markers: " + ", ".join(marks))

    for f in files:
        if re.search(r"\.(c|cc|cpp|h|hpp)$", f):
            s = open(f, errors="replace").read()
            if s.count("{") != s.count("}"):
                fails.append(f"unbalanced braces: {f} ({s.count('{')} {{ vs {s.count('}')} }})")

    numstat = sh(f"git diff --numstat {mb} HEAD").stdout
    for name, cmd, inp in (
        ("engine Kotlin gate", "bash tools/engine-kotlin-gate.sh", numstat),
        ("layer gate", "bash tools/layer-gate.sh", None),
    ):
        r = sh(cmd, inp)
        if r.returncode:
            fails.append(f"{name}:\n{r.stdout}{r.stderr}")

    native = [f for f in files if f.startswith("core/")]
    tests = host_tests()
    if not ALL:
        touched_dirs = {os.path.dirname(f) for f in native}
        tests = [t for t in tests if any(s in native or os.path.dirname(s) in touched_dirs for s in t[3])
                 or f"core/tests/{t[0]}.cpp" in native or ".github/workflows/android.yml" in files]
    tmp = tempfile.mkdtemp(prefix="precheck-")

    def one(t):
        exe, comp, run, _ = t
        out = os.path.join(tmp, exe)
        c = sh(re.sub(r"-o\s+\S+\s*$", f"-o {out}", comp))
        if c.returncode:
            return f"host test {exe} does not compile:\n{c.stderr[-3000:]}"
        r = sh(out + run[len("./" + exe):])
        if r.returncode:
            return f"host test {exe} failed:\n{(r.stdout + r.stderr)[-3000:]}"
        return None

    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        fails += [f for f in pool.map(one, tests) if f]

    print(f"precheck vs {BASE}: {len(files)} changed file(s), {len(tests)} host test(s) run")
    if fails:
        print("\nFAILED:\n" + "\n\n".join(fails))
        sys.exit(1)
    print("OK: gates and host tests pass. Push; CI does the Gradle build, unit tests, lint, shaders and emulator.")


if __name__ == "__main__":
    main()
