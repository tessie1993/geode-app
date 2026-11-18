#!/usr/bin/env bash
# Fails when a .cpp under core/api or app/src/main/cpp declares by hand, at namespace scope, a function it does
# not define. Such a function lives in another .cpp; its declaration belongs in a header both files include,
# so a signature that drifts on one side is a compile error there and not a link failure (core/api's
# vizFrameAt and takeVizStill, once declared by hand in geode_stage_api.cpp, are in geode_viz_internal.hpp).
#
# A text check, after comments, strings and preprocessor lines are blanked. A declaration counts when it sits
# in named namespaces or extern "C" only (not static, not in an anonymous namespace, not in a class or a
# function), has a type before its name, and has a parameter list that reads as parameters: empty, void,
# or each one with a type and a name, a builtin type, or a pointer or reference. So `Foo g(a, b);` reads as
# a variable. It cannot see: operators, templates, declarations a macro writes, code under #if.
#
# Usage: tools/hand-declaration-gate.sh [repo-root]
#        tools/hand-declaration-gate.sh --self-test
set -euo pipefail

if [ "${1:-}" = "--self-test" ]; then
    self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
    t=$(mktemp -d)
    trap 'rm -rf "${t:?}"' EXIT
    mkdir -p "$t/core/api" "$t/app/src/main/cpp"
    # What may stay: a forward declaration of a function the file defines, static and anonymous-namespace
    # declarations, a variable built from arguments, a static_assert, a member and a local declaration, a
    # declaration in a comment or a string.
    printf '%s\n' '#include "api/x.h"' 'namespace geode::api {' 'int later(int a, float* b);' \
        'pairing::Pairing gPairing(osRandom);' 'std::vector<int> sizes(10);' 'static_assert(sizeof(int) == 4);' \
        'int later(int a, float* b) { return a; }' '}  // namespace geode::api' \
        'static void helper(int x);' 'namespace {' 'void inner(int x);' '}  // namespace' \
        'struct S { void member(int x); };' 'void f() { void local(int x); }' \
        '// int commented(int x);' 'const char* text = "int quoted(int x);";' 'static void helper(int x) {}' \
        > "$t/core/api/clean_api.cpp"
    printf '%s\n' '#include <jni.h>' 'extern "C" {' \
        'JNIEXPORT jint JNICALL Java_p_N_one(JNIEnv* env, jobject, jint a) { return a; }' '}' \
        > "$t/app/src/main/cpp/clean_jni.cpp"
    bash "$self" "$t" >/dev/null || { echo "::error::hand declaration gate self-test: clean tree failed"; exit 1; }
    plant() {
        printf '%s\n' "$2" > "$t/$1"
        if bash "$self" "$t" >/dev/null; then echo "::error::hand declaration gate self-test: passed: $1: $2"; exit 1; fi
        rm "$t/$1"
    }
    plant core/api/leak_api.cpp $'namespace geode::api {\nint vizFrameAt(geode_viz* v, const geode_pcm_ring* ring, float pacedFps);\n}'
    plant core/api/leak_api.cpp $'namespace geode::api {\nbool takeVizStill(geode_viz* v, int& width, int& height,\n                  std::vector<uint8_t>& rgba);\n}'
    plant core/api/leak_api.cpp 'void geode_viz_poke(void);'
    plant core/api/leak_api.cpp $'namespace a { namespace b {\nconst char* name(int);\n} }'
    plant app/src/main/cpp/leak_jni.cpp $'extern "C" {\n[[nodiscard]] int geode_hidden(int count);\n}'
    plant app/src/main/cpp/leak_jni.cpp 'extern void geode_other(const char* text);'
    echo "hand declaration gate self-test passed"
    exit 0
fi

root=${1:-.}

python3 - "$root" <<'PY'
import glob
import os
import re
import sys

root = sys.argv[1]
files = sorted(glob.glob(os.path.join(root, 'core/api/**/*.cpp'), recursive=True)
               + glob.glob(os.path.join(root, 'app/src/main/cpp/**/*.cpp'), recursive=True))

BUILTIN = {'void', 'bool', 'char', 'short', 'int', 'long', 'float', 'double', 'unsigned', 'signed', 'auto',
           'size_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
           'uintptr_t', 'intptr_t', 'jint', 'jlong', 'jfloat', 'jboolean', 'jobject', 'jclass', 'jstring'}
SKIP_HEAD = re.compile(r'^(static|using|typedef|template|friend|return|class|struct|union|enum|namespace|'
                       r'static_assert|goto|case|default|delete|throw|operator)\b')
HEAD = re.compile(r'^(?:extern\s+)?(.*?[\w>*&])\s*\b([A-Za-z_]\w*)\s*\($', re.S)
NAME_DEF = re.compile(r'([A-Za-z_]\w*)\s*\([^;{}]*\)[^;{}()]*$', re.S)


def blank(src):
    """Comments, string and char literals and preprocessor lines become spaces; newlines stay."""
    out = list(src)
    n = len(src)
    i = 0
    line_start = True
    while i < n:
        c = src[i]
        if line_start and c in ' \t':
            i += 1
            continue
        if line_start and c == '#':
            j = i
            while j < n and not (src[j] == '\n' and src[j - 1] != '\\'):
                j += 1
            for k in range(i, j):
                if src[k] != '\n':
                    out[k] = ' '
            i = j
            continue
        line_start = c == '\n'
        if src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
        elif c == 'R' and src.startswith('R"', i):
            m = re.match(r'R"([^(\s]*)\(', src[i:])
            if not m:
                i += 1
                continue
            end = src.find(')' + m.group(1) + '"', i)
            j = n if end < 0 else end + len(m.group(1)) + 2
        elif c in '"\'':
            j = i + 1
            while j < n and src[j] != c and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            j += 1
        else:
            i += 1
            continue
        for k in range(i, min(j, n)):
            if src[k] != '\n':
                out[k] = ' '
        line_start = j > 0 and src[j - 1] == '\n'
        i = j
    return ''.join(out)


def split_params(text):
    parts, depth, cur = [], 0, ''
    for ch in text:
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
    parts.append(cur)
    return [p.strip() for p in parts]


def reads_as_params(text):
    text = text.strip()
    if text in ('', 'void'):
        return True
    for p in split_params(text):
        p = p.split('=')[0].strip()
        if not p:
            return False
        if p.endswith(('*', '&', '...')):
            continue
        words = re.findall(r'[A-Za-z_][\w:]*', p)
        if re.search(r'[^\w\s:<>,*&\[\]]', p):
            return False
        if len(words) >= 2 or (len(words) == 1 and words[0].split('::')[-1] in BUILTIN):
            continue
        return False
    return True


problems = []
for path in files:
    with open(path, encoding='utf-8', errors='replace') as fh:
        src = blank(fh.read().replace('\r\n', '\n'))
    scopes = []          # 'ns' (named namespace or extern "C") or 'other'
    stmt_start = 0
    defined = set()
    decls = []           # (name, line, text)
    i = 0
    n = len(src)
    paren = 0
    while i < n:
        c = src[i]
        if c == '(':
            paren += 1
        elif c == ')':
            paren -= 1
        elif paren == 0 and c in ';{}':
            stmt = src[stmt_start:i]
            at_ns = all(s == 'ns' for s in scopes)
            head = re.sub(r'\[\[.*?\]\]', ' ', stmt).strip()
            if c == '{':
                # extern "C" reads as plain extern: its string is blanked.
                if re.fullmatch(r'(inline\s+)?namespace\s+[\w:]+', head) or head == 'extern':
                    scopes.append('ns')
                else:
                    scopes.append('other')
                    if at_ns:
                        m = NAME_DEF.search(head)
                        if m:
                            defined.add(m.group(1))
            elif c == '}':
                if scopes:
                    scopes.pop()
            elif at_ns and head and not SKIP_HEAD.match(head) and '(' in head:
                first = head.index('(')
                close = head.rfind(')')
                tail = head[close + 1:].strip()
                m = HEAD.match(head[:first + 1])
                if (m and '=' not in head[:first] and re.fullmatch(r'((const|noexcept|override|final)\s*)*', tail)
                        and reads_as_params(head[first + 1:close])):
                    line = src.count('\n', 0, stmt_start + len(stmt) - len(stmt.lstrip())) + 1
                    decls.append((m.group(2), line, ' '.join(head.split())))
            stmt_start = i + 1
        i += 1
    for name, line, text in decls:
        if name not in defined:
            problems.append(f'{os.path.relpath(path, root)}:{line}: {text};')

if problems:
    print('::error title=Hand declaration::A .cpp under core/api or app/src/main/cpp declares a function another '
          '.cpp defines. Declare it in a header both files include.')
    print('\n'.join(problems))
    sys.exit(1)
print(f'Hand declaration gate passed: {len(files)} files, no function declared by hand at namespace scope.')
PY
