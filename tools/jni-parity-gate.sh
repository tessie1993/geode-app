#!/usr/bin/env bash
# Fails when a Kotlin `external fun` (or a Java `native` method) has no JNI function, or a `Java_…`
# function has no declaration. libgeode binds by name (no RegisterNatives) and nothing here runs on a
# device, so a mismatch would otherwise show only as UnsatisfiedLinkError on a user's phone.
#
# The name is the JNI spec's ("Resolving Native Method Names"): Java_<class>_<method>, each escaped
# ('/' -> _, '_' -> _1, ';' -> _2, '[' -> _3, anything else not A-Za-z0-9 -> _0xxxx, so '$' is _00024),
# plus __<descriptor> when two natives of one class share a name. It reads nested classes, companions
# (with @JvmStatic the method is the outer class's), top-level functions (<File>Kt or @file:JvmName),
# @JvmName and `internal` (name$<module>, matched by prefix). It also wants each function JNIEXPORT
# (the library is built -fvisibility=hidden), inside extern "C", and its file named in CMakeLists.txt.
#
# A text check. It cannot see: parameter and return types (a wrong signature, or which overload a
# __<descriptor> name is: only their number is counted), static against instance, names built by
# macros, code under #if, a value-class parameter's mangled name, whether the .so really links.
# It refuses what it cannot read: RegisterNatives, external properties, accessors and extension
# functions, and an external fun in a local or anonymous class.
#
# Usage: tools/jni-parity-gate.sh [repo-root]. Scans app/src/main and engine/*/src/main against
# app/src/main/cpp and core.
set -euo pipefail

root=${1:-.}

python3 - "$root" <<'PY'
import glob
import os
import re
import sys

root = sys.argv[1]

# Mismatches main already had when the gate arrived, by JNI function name. Shrink only: fix one,
# delete its line; an entry that no longer mismatches fails the gate.
KNOWN_MISSING = set()  # declared in Kotlin, no JNI function
KNOWN_ORPHAN = set()   # JNI function, no Kotlin declaration

ESC = {'/': '_', '_': '_1', ';': '_2', '[': '_3'}
TOK = re.compile(
    r'[{}()]'
    r'|(?<![\w:.$])(?:(companion)[ \t]+)?(class|object|interface)\b(?:[ \t]+(`[^`]+`|[A-Za-z_]\w*))?'
    r'|\b(fun|val|var|typealias)\b'
    r'|\b(external|native)\b')
KFUN = re.compile(r'external\b(?:[ \t]+[a-z]+)*[ \t]+fun\s+(?:<[^>]*>\s*)?(`[^`]+`|[A-Za-z_]\w*)\s*\(')
KODD = re.compile(r'external\b(?:[ \t]+[a-z]+)*[ \t]+(?:fun|val|var|get|set)\b')
JFUN = re.compile(r'native\b[^;{}()=]*?\b([A-Za-z_]\w*)\s*\(')
CDEF = re.compile(r'\bJava_\w+(?=\s*\()')

problems = []
decls = []   # (class, method, prefix match, file, line)
cdefs = {}   # name -> [file, line, exported, extern "C"]


def esc(name):
    out = []
    for ch in name:
        if ch.isascii() and ch.isalnum():
            out.append(ch)
        elif ch in ESC:
            out.append(ESC[ch])
        else:
            b = ch.encode('utf-16-be')
            out.extend('_0%04x' % int.from_bytes(b[k:k + 2], 'big') for k in range(0, len(b), 2))
    return ''.join(out)


def strip(src, kotlin):
    """Two copies of src, offsets kept: comments blanked, and string and char bodies blanked too."""
    code = list(src)
    bare = list(src)
    n = len(src)

    def blank(a, b, comment):
        for k in range(a, min(b, n)):
            if src[k] != '\n':
                bare[k] = ' '
                if comment:
                    code[k] = ' '

    i = 0
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            blank(i, j, True)
            i = j
        elif src.startswith('/*', i):
            depth = 1
            j = i + 2
            while j < n and depth:
                if src.startswith('*/', j):
                    depth -= 1
                    j += 2
                elif kotlin and src.startswith('/*', j):
                    depth += 1
                    j += 2
                else:
                    j += 1
            blank(i, j, True)
            i = j
        elif kotlin and src.startswith('"""', i):
            j = src.find('"""', i + 3)
            j = n if j < 0 else j
            blank(i + 3, j, False)
            i = j + 3
        elif c == '"' or (c == "'" and (kotlin or i == 0 or not src[i - 1].isalnum())):
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            blank(i + 1, j, False)
            i = j + 1
        else:
            i += 1
    return ''.join(code), ''.join(bare)


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def rel(path):
    return os.path.relpath(path, root).replace(os.sep, '/')


def walk(top, exts):
    for d, dirs, names in os.walk(top):
        dirs.sort()
        for name in sorted(names):
            if name.endswith(exts):
                yield os.path.join(d, name)


def scan_jvm(path):
    src = read(path)
    java = path.endswith('.java')
    word = 'native' if java else 'external'
    if word not in src:
        return
    code, bare = strip(src, not java)
    where = rel(path)
    m = re.search(r'^\s*package\s+([\w.`]+)', bare, re.M)
    pkg = m.group(1).replace('`', '').replace('.', '/') + '/' if m else ''
    m = re.search(r'@file\s*:\s*JvmName\s*\(\s*"([^"]+)"', code)
    stem = os.path.basename(path).rsplit('.', 1)[0]
    file_class = m.group(1) if m else re.sub(r'\W', '_', stem[:1].upper() + stem[1:]) + 'Kt'

    stack = []      # per open brace: (class name, is companion), or None for any other block
    pending = None  # (class name, is companion, paren depth) until its '{'
    paren = 0
    for t in TOK.finditer(bare):
        tok = t.group(0)
        if tok == '(':
            paren += 1
        elif tok == ')':
            paren -= 1
        elif tok == '{':
            stack.append(pending[:2] if pending and pending[2] == paren else None)
            pending = None
        elif tok == '}':
            if stack:
                stack.pop()
        elif t.group(2):
            name = (t.group(3) or '').strip('`')
            if t.group(1) and not name:
                name = 'Companion'
            pending = (name, bool(t.group(1)), paren) if name else None
        elif t.group(4):
            if pending and pending[2] == paren:
                pending = None
        elif t.group(5) == word:
            pos = t.start()
            line = bare.count('\n', 0, pos) + 1
            f = (JFUN if java else KFUN).match(bare, pos)
            if not f:
                if not java and KODD.match(bare, pos):
                    problems.append('%s:%d: an external property, accessor or extension function; '
                                    'this gate reads only `external fun name(`' % (where, line))
                continue
            if not all(stack):
                problems.append('%s:%d: a native method in a local or anonymous class; '
                                'this gate cannot name its class' % (where, line))
                continue
            # The declaration's modifiers: its own line, and the annotation lines right above it.
            start = bare.rfind('\n', 0, pos) + 1
            while start > 0:
                prev = bare.rfind('\n', 0, start - 1) + 1
                if not bare[prev:start].lstrip().startswith('@'):
                    break
                start = prev
            end = f.end()
            jvm_name = re.search(r'@JvmName\s*\(\s*(?:name\s*=\s*)?"([^"]+)"', code[start:end])
            internal = not java and re.search(r'\binternal\b', bare[start:end])
            chain = [entry[0] for entry in stack]
            if chain and stack[-1][1] and '@JvmStatic' in code[start:end]:
                chain.pop()
            if not chain:
                chain = [file_class]
            method = jvm_name.group(1) if jvm_name else f.group(1).strip('`')
            decls.append((pkg + '$'.join(chain), method, bool(internal and not jvm_name), where, line))


def scan_native(path, cmake):
    src = read(path)
    if 'Java_' not in src and 'RegisterNatives' not in src:
        return
    code, bare = strip(src, False)
    where = rel(path)
    m = re.search(r'\bRegisterNatives\b', bare)
    if m:
        problems.append('%s:%d: RegisterNatives binds a table this gate cannot read; bind by name'
                        % (where, bare.count('\n', 0, m.start()) + 1))
    found = False
    for m in CDEF.finditer(bare):
        found = True
        pos = m.start()
        entry = cdefs.setdefault(m.group(0), [where, bare.count('\n', 0, pos) + 1, False, False])
        head = bare[max(bare.rfind(ch, 0, pos) for ch in ';{}') + 1:pos]
        if 'JNIEXPORT' in head:
            entry[2] = True
        if re.search(r'extern\s+"C"', code[:pos]):
            entry[3] = True
    if (found and cmake is not None and where.startswith('app/src/main/cpp/')
            and path.endswith(('.c', '.cc', '.cpp')) and where not in cmake):
        problems.append('%s:1: defines JNI functions but CMakeLists.txt does not name the file' % where)


for top in [os.path.join(root, 'app', 'src', 'main')] + sorted(
        glob.glob(os.path.join(root, 'engine', '*', 'src', 'main'))):
    for p in walk(top, ('.kt', '.java')):
        scan_jvm(p)

cmake_path = os.path.join(root, 'CMakeLists.txt')
cmake_text = read(cmake_path) if os.path.isfile(cmake_path) else None
for top in (os.path.join(root, 'app', 'src', 'main', 'cpp'), os.path.join(root, 'core')):
    for p in walk(top, ('.c', '.cc', '.cpp', '.h', '.hpp', '.inc')):
        scan_native(p, cmake_text)

groups = {}
for d in decls:
    groups.setdefault(d[:3], []).append(d)

claimed = set()
excused = set()
for (cls, method, prefix), ds in sorted(groups.items()):
    short = 'Java_' + esc(cls) + '_' + esc(method)
    longs = [c for c in cdefs if c.startswith(short + '__')]
    if prefix:
        hits = [c for c in cdefs if c.startswith(short + '_00024')]
        ok = bool(hits)
        want = short + '_00024<module> (an internal fun; @JvmName gives it a plain name)'
    elif len(ds) > 1:
        hits = longs + ([short] if short in cdefs else [])
        ok = len(longs) == len(ds) and short not in cdefs
        want = '%d overloads as %s__<descriptor> and no short name (found %d)' % (len(ds), short, len(longs))
    else:
        hits = longs + ([short] if short in cdefs else [])
        ok = bool(hits)
        want = short
    claimed.update(hits)
    if ok:
        continue
    if short in KNOWN_MISSING:
        excused.add(short)
        continue
    for d in ds:
        problems.append('%s:%d: %s.%s has no JNI function: %s' % (d[3], d[4], cls.replace('/', '.'), method, want))

for name, (where, line, exported, extern_c) in sorted(cdefs.items()):
    if name not in claimed:
        if name in KNOWN_ORPHAN:
            excused.add(name)
        else:
            problems.append('%s:%d: %s has no Kotlin external fun' % (where, line, name))
        continue
    if not exported:
        problems.append('%s:%d: %s is not JNIEXPORT, so the hidden-visibility build drops its symbol'
                        % (where, line, name))
    if not extern_c:
        problems.append('%s:%d: %s is not inside extern "C"' % (where, line, name))

for name in sorted((KNOWN_MISSING | KNOWN_ORPHAN) - excused):
    problems.append('tools/jni-parity-gate.sh: %s is listed as a known mismatch but matches now; delete its line' % name)

if not decls and not cdefs:
    problems.append('tools/jni-parity-gate.sh: found no external fun and no Java_ function; did the sources move?')

if problems:
    print('::error title=JNI parity broken::A Kotlin external fun and its Java_ function in '
          'app/src/main/cpp must exist as a pair, under the same name.')
    print('\n'.join(problems))
    sys.exit(1)
print('JNI parity gate passed: %d external funs, %d JNI functions.' % (len(decls), len(cdefs)))
PY
