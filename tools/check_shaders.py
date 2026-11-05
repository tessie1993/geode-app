"""Compile-checks every fragment and vertex shader with the NDK's glslc.

Resolves the `//#include lib_x` lines the way core/viz/ShaderSource.cpp does,
against the same allow-list (core/viz/ShaderIncludes.inc: an unknown or
missing include fails here as it fails on the phone), then compiles for an
OpenGL target. The Silk simulation bodies (silk_step, silk_show) are not whole
shaders; they are compiled inside the preamble core/viz/compute/SimGlsl.cpp
wraps them in. glslc will not take `#version 300 es`
for SPIR-V, so the check bumps it to 310 es: it catches syntax, type and
undeclared-name errors, not ES 3.00-only restrictions.

Usage: python tools/check_shaders.py <glslc> [shader ...]
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHADERS = ROOT / "app/src/main/assets/shaders"
INCLUDES_INC = ROOT / "core/viz/ShaderIncludes.inc"
# Shaders the engine compiles with defines prepended, one entry per variant it
# builds. FluidLook prepends LOOK (one per fluid style's material) and the
# SHADING / BLOOM / SUNRAYS keywords; each look's code sits behind its own #if,
# so a look is only checked if its LOOK value is.
DEFINES = {
    "fluid_display_frag.glsl": [f"#define LOOK {look}\n" for look in range(9)]
    + [f"#define LOOK {look}\n#define SHADING\n#define BLOOM\n#define SUNRAYS\n" for look in range(9)],
}
INCLUDE = re.compile(r"^[ \t]*//#include[ \t]+(\w+)[ \t]*$", re.M)

# SimGlsl::fragmentStep / displayShader for Silk's encoding (RGBA16F, filterable,
# a state scale other than 1). Only what the bodies can see has to match: the
# uniform names, simUv / simLoad / simSample, SIM_STATE_SCALE and simOut.
SIM_PREAMBLE = """#version 310 es
precision highp float;
precision highp int;

uniform sampler2D uSimState;
uniform ivec2 uSimSize;
const float SIM_STATE_SCALE = 8.0;

vec2 simUv(ivec2 texel) {
    return (vec2(texel) + 0.5) / vec2(uSimSize);
}

vec4 simLoad(ivec2 texel) {
    ivec2 at = clamp(texel, ivec2(0), uSimSize - ivec2(1));
    return texelFetch(uSimState, at, 0) * SIM_STATE_SCALE;
}

vec4 simSample(vec2 uv) {
    return texture(uSimState, uv) * SIM_STATE_SCALE;
}

"""
SIM_STEP_MAIN = """
void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    simOut = (simStep(texel, uSimSize, simLoad(texel))) / SIM_STATE_SCALE;
}
"""
# name -> (prefix, suffix) around the resolved body.
SIM_BODIES = {
    "silk_step.glsl": (SIM_PREAMBLE + "out vec4 simOut;\n\n", SIM_STEP_MAIN),
    "silk_show.glsl": (SIM_PREAMBLE, ""),
}


def allow_list():
    # Every double-quoted name outside a `//` comment, as the .inc's header says.
    text = re.sub(r"//[^\n]*", "", INCLUDES_INC.read_text(encoding="utf-8"))
    names = set(re.findall(r'"(\w+)"', text))
    if not names:
        raise SystemExit(f"no include names in {INCLUDES_INC}")
    return names


ALLOWED = allow_list()


def resolve(text, path):
    # Mirrors core/viz/ShaderSource.cpp: an include repeated outside its own
    # chain is pasted again, not skipped, so a double include fails here the
    # way it fails on the device (redefinition) instead of passing silently.
    def expand(m):
        name = m.group(1)
        if name not in ALLOWED:
            raise SystemExit(f"unknown shader include '{name}' (add it to {INCLUDES_INC.relative_to(ROOT)})")
        if name in path:
            raise SystemExit(f"circular shader include '{name}'")
        lib = SHADERS / f"{name}.glsl"
        if not lib.is_file():
            raise SystemExit(f"missing shader include '{name}'")
        return resolve(lib.read_text(encoding="utf-8"), path + (name,))

    return INCLUDE.sub(expand, text)


def main():
    glslc = sys.argv[1]
    names = sys.argv[2:] or sorted(p.name for p in SHADERS.glob("*_frag.glsl")) + sorted(
        p.name for p in SHADERS.glob("*_vert.glsl")) + sorted(SIM_BODIES)
    failed = []
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            stage = "vert" if name.endswith("_vert.glsl") else "frag"
            text = resolve((SHADERS / name).read_text(encoding="utf-8"), ())
            for i, defines in enumerate(DEFINES.get(name, [""])):
                total += 1
                if name in SIM_BODIES:
                    prefix, suffix = SIM_BODIES[name]
                    src = prefix + text.strip() + "\n" + suffix
                else:
                    src = text.replace("#version 300 es", "#version 310 es\n" + defines, 1)
                out = pathlib.Path(tmp) / f"{i}_{name}"
                out.write_text(src, encoding="utf-8")
                r = subprocess.run(
                    [glslc, f"-fshader-stage={stage}", "--target-env=opengl", "-fauto-map-locations",
                     "-fauto-bind-uniforms", "-o", str(out) + ".spv", str(out)],
                    capture_output=True, text=True)
                if r.returncode != 0:
                    label = f"{name} [{defines.strip()}]" if defines else name
                    failed.append(label)
                    print(f"FAIL {label}\n{r.stderr.strip()}\n")
    print(f"{total - len(failed)}/{total} shaders compiled")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
