#!/usr/bin/env python3
"""Compile every fragment style under app/src/main/assets/shaders with glslangValidator.

Resolves `//#include <name>` lines the way core/viz/ShaderSource.cpp does: one
level deep, whole-line, and only for the names in ShaderSource.cpp's kIncludes
(read out of that file so the two cannot drift). A style that includes an
unknown library, or a library that fails to compile, fails here rather than on
a phone.

    tools/validate_shaders.py            # every *_frag.glsl
    tools/validate_shaders.py kifs_frag  # one style

Needs glslangValidator on PATH (Debian/Ubuntu: glslang-tools).
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHADERS = ROOT / "app/src/main/assets/shaders"
SHADER_SOURCE = ROOT / "core/viz/ShaderSource.cpp"
INCLUDE_LINE = re.compile(r"^[ \t]*//#include[ \t]+(\w+)[ \t]*$")


def known_includes():
    src = SHADER_SOURCE.read_text()
    block = re.search(r"kIncludes\s*=\s*\{([^}]*)\}", src, re.S)
    if not block:
        sys.exit(f"could not find kIncludes in {SHADER_SOURCE}")
    return set(re.findall(r'"([^"]+)"', block.group(1)))


def resolve(path, allowed):
    out = []
    for line in path.read_text().splitlines():
        m = INCLUDE_LINE.match(line)
        if not m:
            out.append(line)
            continue
        name = m.group(1)
        if name not in allowed:
            raise ValueError(f"{path.name}: unknown shader include '{name}'")
        out.append((SHADERS / f"{name}.glsl").read_text())
    return "\n".join(out) + "\n"


def validate(path, allowed):
    try:
        source = resolve(path, allowed)
    except ValueError as e:
        return str(e)
    with tempfile.NamedTemporaryFile("w", suffix=".frag", delete=False) as f:
        f.write(source)
        tmp = f.name
    proc = subprocess.run(["glslangValidator", tmp], capture_output=True, text=True)
    pathlib.Path(tmp).unlink()
    if proc.returncode == 0:
        return None
    return (proc.stdout + proc.stderr).strip()


def main(argv):
    allowed = known_includes()
    wanted = set(argv[1:])
    styles = sorted(p for p in SHADERS.glob("*_frag.glsl") if not wanted or p.stem in wanted)
    if not styles:
        sys.exit("no matching styles")
    failures = 0
    for path in styles:
        error = validate(path, allowed)
        if error:
            failures += 1
            print(f"FAIL {path.name}\n{error}\n")
        else:
            print(f"ok   {path.name}")
    print(f"\n{len(styles) - failures} ok, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
