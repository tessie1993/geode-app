#!/usr/bin/env python3
"""Diff-scoped mutation testing of the C++ core (core/), report-only by default.

Why this exists: the host tests in core/tests prove the engine runs; they do not prove they
would notice a wrong `<` or a dropped clamp. This script mutates only the lines a pull request
changed under core/ (never core/tests/), rebuilds only the host tests that compile those lines,
and reports which mutants the tests failed to kill, so a test author can kill them.

Where the tests come from: .github/workflows/native-host-tests.yml is the one place that says how each
host test is built (`g++ ... -o X`) and run (`./X args`). This script parses those `run:` blocks
instead of keeping a second copy, so a new host test is picked up the day it lands in CI.

Engines (see docs/testing/MUTATION.md for the reasoning):
  source (primary)  a small source-level mutator on the changed lines, built with the very g++
                    flags CI uses. No third-party packages, no compiler swap.
  mull   (opt-in)   Mull's LLVM pass plugin with its git-diff mode; needs `mull-<N>` and
                    clang-<N> installed (the workflow does it when asked).

Python 3 standard library only. Runs on CI; locally it is a dev-machine tool (never in an
agent container: the owner rule is that CI builds and tests).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import glob
import itertools
import json
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

CXX_SUFFIXES = (".cpp", ".hpp", ".h")
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")
EXCLUDED_PREFIX = "core/tests/"
COMPILERS = ("g++", "clang++", "c++")


# --------------------------------------------------------------------------------------------
# native-host-tests.yml parsing


@dataclasses.dataclass
class HostTest:
    """One host test: how CI compiles it and how CI runs it."""

    name: str  # the -o output, e.g. viz_math_test
    step: str  # the workflow step that holds it
    compile_argv: list[str]  # g++ argv, globs not yet expanded
    run_argv: list[str] = dataclasses.field(default_factory=list)  # ./name args

    def flags(self) -> list[str]:
        """Compiler argv minus the compiler, the sources and `-o name`."""
        out: list[str] = []
        skip = False
        for tok in self.compile_argv[1:]:
            if skip:
                skip = False
                continue
            if tok == "-o":
                skip = True
                continue
            if tok.endswith(SOURCE_SUFFIXES):
                continue
            out.append(tok)
        return out

    def raw_sources(self) -> list[str]:
        return [t for t in self.compile_argv[1:] if t.endswith(SOURCE_SUFFIXES)]

    def include_dirs(self) -> list[str]:
        dirs: list[str] = []
        args = self.compile_argv[1:]
        for i, tok in enumerate(args):
            for flag in ("-I", "-iquote", "-isystem"):
                if tok == flag and i + 1 < len(args):
                    dirs.append(args[i + 1])
                elif tok.startswith(flag) and len(tok) > len(flag):
                    dirs.append(tok[len(flag):])
        return dirs


def extract_run_blocks(workflow_text: str) -> list[tuple[str, str]]:
    """Every `run: |` block of a workflow as (step name, script text).

    A deliberately small YAML subset: block scalars (`|`, `|-`, `>`) and one-line `run:`. The
    block ends at the first non-blank line indented no deeper than the `run:` key.
    """
    lines = workflow_text.splitlines()
    blocks: list[tuple[str, str]] = []
    step = ""
    i = 0
    run_re = re.compile(r"^(\s*)(?:-\s+)?run:\s*(.*)$")
    name_re = re.compile(r"^\s*-?\s*name:\s*(.+?)\s*$")
    while i < len(lines):
        line = lines[i]
        m_name = name_re.match(line)
        if m_name:
            step = m_name.group(1).strip("'\"")
        m = run_re.match(line)
        if not m:
            i += 1
            continue
        key_indent = len(m.group(1))
        rest = m.group(2).strip()
        if rest and rest[0] not in "|>":
            blocks.append((step, rest))
            i += 1
            continue
        body: list[str] = []
        i += 1
        while i < len(lines):
            nxt = lines[i]
            if nxt.strip() and (len(nxt) - len(nxt.lstrip())) <= key_indent:
                break
            body.append(nxt)
            i += 1
        indents = [len(b) - len(b.lstrip()) for b in body if b.strip()]
        cut = min(indents) if indents else 0
        blocks.append((step, "\n".join(b[cut:] for b in body)))
    return blocks


def logical_lines(script: str) -> list[str]:
    """Join backslash continuations, drop comments and blank lines."""
    out: list[str] = []
    buf = ""
    for raw in script.splitlines():
        line = raw.rstrip()
        if not buf and line.lstrip().startswith("#"):
            continue
        if line.endswith("\\"):
            buf += line[:-1] + " "
            continue
        buf += line
        if buf.strip():
            out.append(buf.strip())
        buf = ""
    if buf.strip():
        out.append(buf.strip())
    return out


def parse_host_tests(workflow_text: str) -> list[HostTest]:
    """Pair each `g++ ... -o X` with the `./X ...` lines that run it (same run block)."""
    tests: list[HostTest] = []
    for step, script in extract_run_blocks(workflow_text):
        by_name: dict[str, HostTest] = {}
        for line in logical_lines(script):
            try:
                argv = shlex.split(line, comments=True)
            except ValueError:
                continue
            if not argv:
                continue
            if Path(argv[0]).name in COMPILERS and "-o" in argv:
                idx = argv.index("-o")
                if idx + 1 >= len(argv):
                    continue
                name = argv[idx + 1]
                test = HostTest(name=name, step=step, compile_argv=argv)
                by_name[name] = test
                tests.append(test)
            elif argv[0].startswith("./"):
                name = argv[0][2:]
                if name in by_name and not by_name[name].run_argv:
                    by_name[name].run_argv = argv
    return [t for t in tests if t.run_argv]


def expand_sources(test: HostTest, root: Path) -> list[str]:
    """The test's sources with shell globs expanded (sorted, as bash does), repo-relative."""
    out: list[str] = []
    for src in test.raw_sources():
        if any(ch in src for ch in "*?["):
            matches = sorted(glob.glob(src, root_dir=root))
            out.extend(matches if matches else [src])
        else:
            out.append(src)
    return out


# --------------------------------------------------------------------------------------------
# git diff


HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def parse_diff_added_lines(diff_text: str) -> dict[str, set[int]]:
    """{path: new-side line numbers} from `git diff -U0` output. Deleted files are dropped."""
    out: dict[str, set[int]] = {}
    current: str | None = None
    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            path = line[4:].strip()
            current = None if path == "/dev/null" else re.sub(r"^b/", "", path)
            if current is not None:
                out.setdefault(current, set())
            continue
        m = HUNK_RE.match(line)
        if m and current is not None:
            start = int(m.group(1))
            count = int(m.group(2)) if m.group(2) is not None else 1
            out[current].update(range(start, start + count))
    return {
        p: ls
        for p, ls in out.items()
        if ls and p.startswith("core/") and not p.startswith(EXCLUDED_PREFIX) and p.endswith(CXX_SUFFIXES)
    }


def changed_lines(root: Path, base: str) -> dict[str, set[int]]:
    cmd = [
        "git", "diff", "-U0", "--no-color", "--no-ext-diff", f"{base}...HEAD", "--",
        # the suffix filter (.cpp/.hpp/.h) is applied in parse_diff_added_lines
        "core/", f":(exclude){EXCLUDED_PREFIX}",
    ]
    res = subprocess.run(cmd, cwd=root, capture_output=True, text=True, check=True)
    return parse_diff_added_lines(res.stdout)


# --------------------------------------------------------------------------------------------
# include graph (which test sources reach a changed header)


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def include_closure(src: str, include_dirs: list[str], root: Path, cache: dict[str, set[str]] | None = None) -> set[str]:
    """Every repo-relative file `src` includes with quotes, transitively."""
    seen: set[str] = set()
    stack = [src]
    while stack:
        cur = stack.pop()
        if cache is not None and cur in cache and cur != src:
            seen |= cache[cur]
            continue
        try:
            text = (root / cur).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for inc in INCLUDE_RE.findall(text):
            for d in [str(Path(cur).parent), *include_dirs]:
                cand = os.path.normpath(os.path.join(d, inc))
                if (root / cand).is_file():
                    if cand not in seen:
                        seen.add(cand)
                        stack.append(cand)
                    break
    if cache is not None:
        cache[src] = seen
    return seen


def relevant_tests(tests: list[HostTest], changed: list[str], root: Path) -> dict[str, list[HostTest]]:
    """{changed file: tests whose build compiles it, directly or through an include}."""
    cache: dict[str, set[str]] = {}
    out: dict[str, list[HostTest]] = {f: [] for f in changed}
    for t in tests:
        srcs = expand_sources(t, root)
        reach: set[str] = set(srcs)
        for s in srcs:
            reach |= include_closure(s, t.include_dirs(), root, cache)
        for f in changed:
            if f in reach:
                out[f].append(t)
    return out


# --------------------------------------------------------------------------------------------
# the source-level mutator


def mask_source(text: str) -> str:
    """Same-length copy of C++ source with comments and string/char literal bodies blanked.

    Operators found in the mask sit at the same offsets in the original, so a mutation never
    lands inside a comment or a string. Raw strings are rare in core/ and left as code.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            # a digit separator (1'000) is not a char literal
            if c == "'" and i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum():
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


@dataclasses.dataclass
class Mutant:
    file: str
    line: int  # 1-based
    operator: str
    original: str  # the whole original line
    mutated: str  # the whole mutated line
    status: str = "pending"  # killed | survived | timeout | invalid | skipped
    detail: str = ""

    def short(self) -> str:
        return f"{self.file}:{self.line}  {self.original.strip()} → {self.mutated.strip()}"


SPACED = r"(?<=\s){op}(?=\s)"
SWAPS: list[tuple[str, str, str]] = [
    # (operator name, regex on the mask, replacement)
    ("rel_boundary", SPACED.format(op="<"), "<="),
    ("rel_boundary", SPACED.format(op="<="), "<"),
    ("rel_boundary", SPACED.format(op=">"), ">="),
    ("rel_boundary", SPACED.format(op=">="), ">"),
    ("equality", r"(?<![=!<>])==(?!=)", "!="),
    ("equality", r"!=(?!=)", "=="),
    ("arith", SPACED.format(op=r"\+"), "-"),
    ("arith", SPACED.format(op="-"), "+"),
    ("arith", SPACED.format(op=r"\*"), "/"),
    ("arith", SPACED.format(op="/"), "*"),
    ("logical", SPACED.format(op="&&"), "||"),
    ("logical", SPACED.format(op=r"\|\|"), "&&"),
]
NUMBER_RE = re.compile(r"(?<![\w.])(\d+(?:\.\d*)?(?:[eE][+-]?\d+)?)([fFuUlL]*)(?![\w.'])")
CALL_RE = re.compile(r"\bstd::(clamp|min|max)\s*\(")
IF_RE = re.compile(r"\bif\s*\(")
RETURN_RE = re.compile(r"\breturn\s+([^;]+);")


def _close_paren(mask: str, open_idx: int) -> int:
    """Index of the `)` matching mask[open_idx] == '(' on this line, or -1."""
    depth = 0
    for k in range(open_idx, len(mask)):
        if mask[k] == "(":
            depth += 1
        elif mask[k] == ")":
            depth -= 1
            if depth == 0:
                return k
    return -1


def _split_args(mask: str, start: int, end: int) -> list[tuple[int, int]]:
    """Top-level comma-separated spans in mask[start:end]."""
    spans, depth, s = [], 0, start
    for k in range(start, end):
        ch = mask[k]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            spans.append((s, k))
            s = k + 1
    spans.append((s, end))
    return spans


def mutate_line(line: str, mask: str) -> list[tuple[str, str]]:
    """Every (operator, mutated line) for one source line. `mask` is the line's masked twin."""
    stripped = mask.strip()
    if not stripped or stripped.startswith("#") or stripped.startswith("static_assert"):
        return []
    found: list[tuple[str, str]] = []

    def put(op: str, a: int, b: int, rep: str) -> None:
        new = line[:a] + rep + line[b:]
        if new != line:
            found.append((op, new))

    for op, pattern, rep in SWAPS:
        for m in re.finditer(pattern, mask):
            put(op, m.start(), m.end(), rep)

    for m in IF_RE.finditer(mask):
        open_idx = m.end() - 1
        close = _close_paren(mask, open_idx)
        if close > open_idx + 1:
            put("negate_if", open_idx, close + 1, "(!" + line[open_idx:close + 1] + ")")

    for m in NUMBER_RE.finditer(mask):
        digits, suffix = m.group(1), m.group(2)
        if digits == "0":
            put("literal", m.start(), m.end(), "1" + suffix)
        elif digits == "1":
            put("literal", m.start(), m.end(), "0" + suffix)
        else:
            put("literal", m.start(), m.end(), f"({digits}{suffix} + 1)")

    for m in RETURN_RE.finditer(mask):
        expr = m.group(1).strip()
        if expr and expr != "{}":
            put("return_default", m.start(), m.end(), "return {};")

    for m in CALL_RE.finditer(mask):
        open_idx = m.end() - 1
        close = _close_paren(mask, open_idx)
        if close < 0:
            continue
        args = _split_args(mask, open_idx + 1, close)
        keep = args[:1] if m.group(1) == "clamp" else args[:2]
        for a, b in keep:
            put("drop_" + m.group(1), m.start(), close + 1, "(" + line[a:b].strip() + ")")

    seen: set[str] = set()
    unique = []
    for op, new in found:
        if new not in seen:
            seen.add(new)
            unique.append((op, new))
    return unique


def generate_mutants(root: Path, changed: dict[str, set[int]]) -> list[Mutant]:
    mutants: list[Mutant] = []
    for path in sorted(changed):
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        lines = text.split("\n")
        masked = mask_source(text).split("\n")
        for ln in sorted(changed[path]):
            if 1 <= ln <= len(lines):
                for op, new in mutate_line(lines[ln - 1], masked[ln - 1]):
                    mutants.append(Mutant(path, ln, op, lines[ln - 1], new))
    return mutants


def interleave(mutants: list[Mutant]) -> list[Mutant]:
    """Round-robin across files so a cap still samples every file."""
    per: dict[str, list[Mutant]] = {}
    for m in mutants:
        per.setdefault(m.file, []).append(m)
    rounds = itertools.zip_longest(*per.values())
    return [m for rnd in rounds for m in rnd if m is not None]


# --------------------------------------------------------------------------------------------
# running: baseline objects once, then per mutant recompile only the TUs it touches


@dataclasses.dataclass
class Built:
    test: HostTest
    sources: list[str]
    objects: list[str]  # absolute, baseline
    closures: list[set[str]]  # per source: files it includes
    run_seconds: float


def _run(cmd: list[str], cwd: Path, timeout: float) -> tuple[str, str]:
    """('ok'|'fail'|'timeout', tail of output)."""
    try:
        res = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, errors="replace")
    except subprocess.TimeoutExpired:
        return "timeout", ""
    tail = (res.stdout + res.stderr)[-400:]
    return ("ok" if res.returncode == 0 else "fail"), tail


def build_baseline(test: HostTest, root: Path, work: Path, compile_timeout: float, log) -> Built | None:
    srcs = expand_sources(test, root)
    flags = test.flags()
    cxx = test.compile_argv[0]
    objdir = work / "base" / test.name
    objdir.mkdir(parents=True, exist_ok=True)
    objs = []
    for i, s in enumerate(srcs):
        obj = objdir / f"{i}_{Path(s).stem}.o"
        st, tail = _run([cxx, *flags, "-c", s, "-o", str(obj)], root, compile_timeout)
        if st != "ok":
            log(f"baseline compile of {test.name} failed on {s}: {st} {tail}")
            return None
        objs.append(str(obj))
    binary = objdir / test.name
    st, tail = _run([cxx, *flags, *objs, "-o", str(binary)], root, compile_timeout)
    if st != "ok":
        log(f"baseline link of {test.name} failed: {tail}")
        return None
    t0 = time.monotonic()
    st, tail = _run([str(binary), *test.run_argv[1:]], root, 600)
    secs = time.monotonic() - t0
    if st != "ok":
        log(f"baseline run of {test.name} is not green ({st}); its mutants are skipped. {tail}")
        return None
    cache: dict[str, set[str]] = {}
    closures = [include_closure(s, test.include_dirs(), root, cache) for s in srcs]
    return Built(test, srcs, objs, closures, secs)


def run_mutant(m: Mutant, builds: list[Built], wdir: Path, compile_timeout: float, min_timeout: float, factor: float) -> None:
    target = wdir / m.file
    original = target.read_text(encoding="utf-8")
    lines = original.split("\n")
    lines[m.line - 1] = m.mutated
    target.write_text("\n".join(lines), encoding="utf-8")
    try:
        any_built = False
        timed_out = False
        for b in builds:
            flags = b.test.flags()
            cxx = b.test.compile_argv[0]
            objs = list(b.objects)
            bad = False
            for i, s in enumerate(b.sources):
                if s == m.file or m.file in b.closures[i]:
                    obj = wdir / "obj" / f"{b.test.name}_{i}.o"
                    obj.parent.mkdir(parents=True, exist_ok=True)
                    st, _ = _run([cxx, *flags, "-c", s, "-o", str(obj)], wdir, compile_timeout)
                    if st != "ok":
                        bad = True
                        break
                    objs[i] = str(obj)
            if bad:
                m.status, m.detail = "invalid", "does not compile"
                return
            binary = wdir / "bin" / b.test.name
            binary.parent.mkdir(parents=True, exist_ok=True)
            st, _ = _run([cxx, *flags, *objs, "-o", str(binary)], wdir, compile_timeout)
            if st != "ok":
                m.status, m.detail = "invalid", "does not link"
                return
            any_built = True
            st, tail = _run([str(binary), *b.test.run_argv[1:]], wdir, max(min_timeout, factor * b.run_seconds))
            if st == "fail":
                m.status, m.detail = "killed", b.test.name
                return
            if st == "timeout":
                timed_out = True
                m.detail = b.test.name
        if not any_built:
            m.status, m.detail = "skipped", "no green test builds this file"
        else:
            m.status = "timeout" if timed_out else "survived"
            if not timed_out:
                m.detail = ", ".join(b.test.name for b in builds)
    finally:
        target.write_text(original, encoding="utf-8")


def run_source_engine(args, root: Path, changed: dict[str, set[int]], tests: list[HostTest], report: dict, log) -> list[Mutant]:
    t_start = time.monotonic()
    rel = relevant_tests(tests, sorted(changed), root)
    mutants = generate_mutants(root, changed)
    report["generated_total"] = len(mutants)
    for m in mutants:
        if not rel.get(m.file):
            m.status, m.detail = "skipped", "no host test compiles this file"
    runnable = interleave([m for m in mutants if m.status == "pending"])
    if len(runnable) > args.max_mutants:
        for m in runnable[args.max_mutants:]:
            m.status, m.detail = "skipped", "over MUTATION_MAX_MUTANTS"
        report["notes"].append(f"Capped at {args.max_mutants} of {len(runnable)} runnable mutants (MUTATION_MAX_MUTANTS); files sampled round-robin.")
        runnable = runnable[: args.max_mutants]
    if args.dry_run:
        for m in runnable:
            m.status = "skipped"
            m.detail = "dry run"
        return mutants

    work = Path(tempfile.mkdtemp(prefix="geode-mutation-"))
    needed = {t.name: t for f in changed for t in rel.get(f, [])}
    builds: dict[str, Built] = {}
    for name, t in sorted(needed.items()):
        log(f"baseline: {name}")
        b = build_baseline(t, root, work, args.compile_timeout, log)
        if b is None:
            report["notes"].append(f"`{name}`: baseline build or run is not green, so it cannot judge mutants.")
        else:
            builds[name] = b

    workers = max(1, args.jobs)
    pool: queue.Queue[Path] = queue.Queue()
    for k in range(workers):
        wdir = work / f"w{k}"
        shutil.copytree(root / "core", wdir / "core")
        pool.put(wdir)
    budget_hit = threading.Event()
    done = [0]
    lock = threading.Lock()

    def job(m: Mutant) -> None:
        if time.monotonic() - t_start > args.time_budget:
            m.status, m.detail = "skipped", "time budget"
            budget_hit.set()
            return
        bs = [builds[t.name] for t in rel[m.file] if t.name in builds]
        if not bs:
            m.status, m.detail = "skipped", "no green test builds this file"
            return
        wdir = pool.get()
        try:
            run_mutant(m, bs, wdir, args.compile_timeout, args.min_timeout, args.timeout_factor)
        finally:
            pool.put(wdir)
        with lock:
            done[0] += 1
            log(f"[{done[0]}/{len(runnable)}] {m.status:8} {m.short()}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        list(ex.map(job, runnable))
    if budget_hit.is_set():
        report["notes"].append(f"Time budget of {int(args.time_budget)} s reached; the remaining mutants were not run.")
    shutil.rmtree(work, ignore_errors=True)
    return mutants


# --------------------------------------------------------------------------------------------
# Mull (opt-in): LLVM pass plugin + git-diff mode, one Mull run per relevant test


MULL_STATUS = {"Killed": "killed", "Survived": "survived", "Timeout": "timeout", "NoCoverage": "survived", "CompileError": "invalid", "RuntimeError": "killed"}


def mull_config_text(base: str, root: Path) -> str:
    return "\n".join([
        "mutators:",
        "  - cxx_default",
        "  - cxx_boundary",
        "  - cxx_logical",
        f"gitDiffRef: {base}",
        f"gitProjectRoot: {root}",
        "gitDiffPaths:",
        "  - core/",
        "excludePaths:",
        "  - '.*core/tests/.*'",
        "  - '.*third_party/.*'",
        "",
    ])


def parse_elements_report(data: dict, root: Path) -> list[Mutant]:
    """Mutants from a mutation-testing-elements JSON report (what Mull's Elements reporter writes)."""
    out: list[Mutant] = []
    for path, info in (data.get("files") or {}).items():
        rel = os.path.relpath(path, root) if os.path.isabs(path) else path
        source = (info.get("source") or "").split("\n")
        for mu in info.get("mutants") or []:
            loc = (mu.get("location") or {}).get("start") or {}
            line = int(loc.get("line", 0))
            orig = source[line - 1] if 0 < line <= len(source) else ""
            m = Mutant(rel, line, mu.get("mutatorName", "?"), orig, mu.get("replacement") or mu.get("description") or "?")
            m.status = MULL_STATUS.get(mu.get("status", ""), "skipped")
            out.append(m)
    return out


def run_mull_engine(args, root: Path, changed: dict[str, set[int]], tests: list[HostTest], report: dict, log) -> list[Mutant]:
    rel = relevant_tests(tests, sorted(changed), root)
    needed = {t.name: t for f in changed for t in rel.get(f, [])}
    work = Path(tempfile.mkdtemp(prefix="geode-mull-"))
    cfg = work / "mull.yml"
    cfg.write_text(mull_config_text(args.base, root), encoding="utf-8")
    env = dict(os.environ, MULL_CONFIG=str(cfg))
    clang = f"clang++-{args.llvm}"
    plugin = f"/usr/lib/mull-ir-frontend-{args.llvm}"
    by_key: dict[tuple, Mutant] = {}
    for name, t in sorted(needed.items()):
        # Mull's docs: -grecord-command-line does not work for several files in one go, so each
        # TU is compiled alone. -Werror is dropped: clang warns where g++ does not, and this is a
        # mutation build, not the CI build.
        flags = [f for f in t.flags() if f != "-Werror"]
        objs = []
        ok = True
        for i, s in enumerate(expand_sources(t, root)):
            obj = work / name / f"{i}.o"
            obj.parent.mkdir(parents=True, exist_ok=True)
            res = subprocess.run([clang, f"-fpass-plugin={plugin}", "-g", "-grecord-command-line", *flags, "-c", s, "-o", str(obj)],
                                 cwd=root, env=env, capture_output=True, text=True)
            if res.returncode != 0:
                report["notes"].append(f"`{name}`: Mull build of {s} failed: {res.stderr[-300:]}")
                ok = False
                break
            objs.append(str(obj))
        if not ok:
            continue
        binary = work / name / name
        res = subprocess.run([clang, *flags, *objs, "-o", str(binary)], cwd=root, env=env, capture_output=True, text=True)
        if res.returncode != 0:
            report["notes"].append(f"`{name}`: Mull link failed: {res.stderr[-300:]}")
            continue
        outdir = work / name / "report"
        cmd = ["mull-runner", "--reporters", "Elements", "--reporters", "IDE", "--report-dir", str(outdir),
               "--report-name", name, "--allow-surviving", "--minimum-timeout", str(int(args.min_timeout * 1000)),
               str(binary), *t.run_argv[1:]]
        log("$ " + " ".join(shlex.quote(c) for c in cmd))
        try:
            subprocess.run(cmd, cwd=root, env=env, timeout=args.time_budget)
        except subprocess.TimeoutExpired:
            report["notes"].append(f"`{name}`: mull-runner hit the time budget; its report may be partial.")
        for jf in outdir.glob("*.json"):
            for m in parse_elements_report(json.loads(jf.read_text(encoding="utf-8")), root):
                if m.file.startswith(EXCLUDED_PREFIX):
                    continue
                key = (m.file, m.line, m.operator, m.mutated)
                prev = by_key.get(key)
                # a mutant killed by any test is killed
                if prev is None or (prev.status != "killed" and m.status == "killed"):
                    by_key[key] = m
    report["generated_total"] = len(by_key)
    return list(by_key.values())


# --------------------------------------------------------------------------------------------
# reporting


def score(counts: dict[str, int]) -> float | None:
    """Killed (timeouts count as killed) over judged mutants; None when nothing was judged."""
    judged = counts["killed"] + counts["timeout"] + counts["survived"]
    return None if judged == 0 else 100.0 * (counts["killed"] + counts["timeout"]) / judged


def tally(mutants: list[Mutant]) -> dict[str, dict[str, int]]:
    per: dict[str, dict[str, int]] = {}
    for m in mutants:
        c = per.setdefault(m.file, {"generated": 0, "killed": 0, "survived": 0, "timeout": 0, "invalid": 0, "skipped": 0})
        c["generated"] += 1
        if m.status in c:
            c[m.status] += 1
    return per


def render_summary(mutants: list[Mutant], report: dict, max_survivors: int = 200) -> str:
    per = tally(mutants)
    total = {"killed": 0, "survived": 0, "timeout": 0}
    for c in per.values():
        for k in total:
            total[k] += c[k]
    s = score(total)
    out = [f"## Mutation testing ({report['engine']}) vs `{report['base']}`", ""]
    if not mutants:
        out.append("No changed C++ lines under `core/` (outside `core/tests/`) to mutate.")
    else:
        out.append(f"**Score: {'n/a' if s is None else f'{s:.1f} %'}** (killed + timed out over killed + timed out + survived; "
                   "mutants that do not compile are left out).")
        out += ["", "| File | Generated | Killed | Survived | Timed out | Not compiling | Not run |", "|---|---:|---:|---:|---:|---:|---:|"]
        for f in sorted(per):
            c = per[f]
            out.append(f"| `{f}` | {c['generated']} | {c['killed']} | {c['survived']} | {c['timeout']} | {c['invalid']} | {c['skipped']} |")
        survivors = [m for m in mutants if m.status == "survived"]
        if survivors:
            out += ["", f"### Survivors ({len(survivors)}): write a test that fails on each", "", "```"]
            out += [m.short() for m in survivors[:max_survivors]]
            if len(survivors) > max_survivors:
                out.append(f"... {len(survivors) - max_survivors} more in the JSON artifact")
            out.append("```")
    if report["notes"]:
        out += ["", "### Notes", ""] + [f"- {n}" for n in report["notes"]]
    threshold = report.get("min_score")
    out += ["", f"Gate: {'report only' if threshold is None else f'fails below {threshold} %'}."]
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------------------------
# main


def main(argv: list[str] | None = None) -> int:
    env = os.environ
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--base", default=env.get("MUTATION_BASE_REF", "origin/main"), help="git ref to diff against (merge-base ...HEAD)")
    p.add_argument("--engine", choices=["source", "mull"], default=env.get("MUTATION_ENGINE") or "source")
    p.add_argument("--workflow", default=".github/workflows/native-host-tests.yml")
    p.add_argument("--root", default=".")
    p.add_argument("--report", default="mutation-report.json")
    p.add_argument("--max-mutants", type=int, default=int(env.get("MUTATION_MAX_MUTANTS") or 300))
    p.add_argument("--time-budget", type=float, default=float(env.get("MUTATION_TIME_BUDGET") or 1200), help="seconds for the whole run")
    p.add_argument("--jobs", type=int, default=int(env.get("MUTATION_JOBS") or (os.cpu_count() or 2)))
    p.add_argument("--compile-timeout", type=float, default=300)
    p.add_argument("--min-timeout", type=float, default=10, help="per-mutant test timeout floor, seconds")
    p.add_argument("--timeout-factor", type=float, default=10, help="per-mutant test timeout = factor x baseline run")
    p.add_argument("--llvm", default=env.get("MULL_LLVM") or "18", help="LLVM major for --engine mull")
    p.add_argument("--dry-run", action="store_true", help="list the mutants; build and run nothing")
    args = p.parse_args(argv)

    root = Path(args.root).resolve()
    min_score_env = (env.get("MUTATION_MIN_SCORE") or "").strip()
    report: dict = {"engine": args.engine, "base": args.base, "notes": [], "min_score": float(min_score_env) if min_score_env else None}

    def log(msg: str) -> None:
        print(msg, flush=True)

    tests = parse_host_tests((root / args.workflow).read_text(encoding="utf-8"))
    if not tests:
        report["notes"].append(f"No host tests parsed from `{args.workflow}`; nothing can judge mutants.")
    changed = changed_lines(root, args.base)
    log(f"{len(tests)} host tests; changed C++ files: {sorted(changed) or 'none'}")

    if not changed:
        mutants: list[Mutant] = []
    elif args.engine == "mull":
        mutants = run_mull_engine(args, root, changed, tests, report, log)
    else:
        mutants = run_source_engine(args, root, changed, tests, report, log)

    per = tally(mutants)
    total = {k: sum(c[k] for c in per.values()) for k in ("killed", "survived", "timeout")}
    report["score"] = score(total)
    report["files"] = per
    report["mutants"] = [dataclasses.asdict(m) for m in mutants]
    Path(args.report).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")

    summary = render_summary(mutants, report)
    print(summary)
    if env.get("GITHUB_STEP_SUMMARY"):
        with open(env["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as fh:
            fh.write(summary)

    if report["min_score"] is not None and report["score"] is not None and report["score"] < report["min_score"]:
        print(f"Mutation score {report['score']:.1f} % is below MUTATION_MIN_SCORE {report['min_score']} %", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
