"""Compile-checks every fragment style with the NDK's glslc.

Resolves the `//#include lib_x` lines the way core/viz/ShaderSource.cpp does,
then compiles for an OpenGL target. glslc will not take `#version 300 es`
for SPIR-V, so the check bumps it to 310 es: it catches syntax, type and
undeclared-name errors, not ES 3.00-only restrictions.

Usage: python tools/check_shaders.py <glslc> [shader ...]
"""
import pathlib
import re
import subprocess
import sys
import tempfile

SHADERS = pathlib.Path(__file__).resolve().parent.parent / "app/src/main/assets/shaders"
# Styles the engine compiles with defines prepended (FluidLook prepends LOOK).
DEFINES = {"fluid_display_frag.glsl": "#define LOOK 0\n"}
INCLUDE = re.compile(r"^\s*//#include\s+(\w+)\s*$", re.M)


def resolve(text, path):
    # Mirrors core/viz/ShaderSource.cpp: an include repeated outside its own
    # chain is pasted again, not skipped, so a double include fails here the
    # way it fails on the device (redefinition) instead of passing silently.
    def expand(m):
        name = m.group(1)
        if name in path:
            raise SystemExit(f"circular shader include '{name}'")
        return resolve((SHADERS / f"{name}.glsl").read_text(encoding="utf-8"), path + (name,))

    return INCLUDE.sub(expand, text)


def main():
    glslc = sys.argv[1]
    names = sys.argv[2:] or sorted(p.name for p in SHADERS.glob("*_frag.glsl"))
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            src = resolve((SHADERS / name).read_text(encoding="utf-8"), ())
            src = src.replace("#version 300 es", "#version 310 es\n" + DEFINES.get(name, ""), 1)
            out = pathlib.Path(tmp) / name
            out.write_text(src, encoding="utf-8")
            r = subprocess.run(
                [glslc, "-fshader-stage=frag", "--target-env=opengl", "-fauto-map-locations",
                 "-fauto-bind-uniforms", "-o", str(out) + ".spv", str(out)],
                capture_output=True, text=True)
            if r.returncode != 0:
                failed.append(name)
                print(f"FAIL {name}\n{r.stderr.strip()}\n")
    print(f"{len(names) - len(failed)}/{len(names)} shaders compiled")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
