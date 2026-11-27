"""Compile-checks every fragment and vertex shader with the NDK's glslc.

Resolves the `//#include lib_x` lines the way core/viz/glsl/ShaderSource.cpp does,
against the same allow-list (core/viz/glsl/ShaderIncludes.inc: an unknown or
missing include fails here as it fails on the phone), then compiles for an
OpenGL target. The Silk simulation bodies (silk_step, silk_show) are not whole
shaders; they are compiled inside the preamble core/viz/styles/silk/SimGlsl.cpp
wraps them in. Before compiling it lints every shader and lib for the time
rule (see speed_twice) and every resolved fragment shader for an int precision
before its first 32-bit uint hash (see int_precision_late). glslc will not take `#version 300 es`
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
INCLUDES_INC = ROOT / "core/viz/glsl/ShaderIncludes.inc"
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


COMMENTS = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
PREPROCESSOR = re.compile(r"^[ \t]*#[^\n]*", re.M)
# Accumulated clocks. Speed is integrated natively into uTime, uFlowTime and
# uSoundTimeSped (so those already carry it), and uFlowPhase and the SND_*_TIME
# band clocks are the unscaled forms: none of them may meet uSpeed in a statement.
U_TIME = re.compile(r"\b(uTime|uFlowPhase|uFlowTime|uSoundTimeSped|SND_(BASS|MID|TREBLE|CURVED)_TIME)\b")
# A plain alias (`float spd = uSpeed;`) counts as uSpeed in the rest of the file.
SPEED_ALIAS = re.compile(r"\b(\w+)\s*=\s*uSpeed\s*;")


def speed_twice(text):
    """Line numbers of statements that put an accumulated clock and uSpeed together.

    The time rule: uTime is ShaderScene's speed-INTEGRATED clock
    (shaderTime_ += speed * dt, core/viz/styles/shader/ShaderScene.cpp), so a shader
    never multiplies it by uSpeed again. `uTime * uSpeed` squares Speed and,
    because it is elapsed time times the live slider, any Speed change (the
    slider, an LFO, a macro, a reaction route) jumps the picture by
    elapsed * delta-speed. The sound engine's band clocks are accumulated the
    same way: their Speed-scaled form is uSoundTimeSped (core/viz/modulation/SpeedClock.hpp);
    uFlowPhase and uFlowTime are held constant now. Read per
    statement, comments stripped, so `uTime * 0.2 * uSpeed` is caught too and
    prose about the rule is not.
    """
    code = COMMENTS.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    # A preprocessor line ends at its newline, not at a `;`: close each one as its
    # own statement, so a #define never merges into the code after it (and one
    # that itself multiplies uTime by uSpeed is still caught).
    code = PREPROCESSOR.sub(lambda m: m.group(0) + ";", code)
    names = ["uSpeed"] + sorted(set(SPEED_ALIAS.findall(code)))
    u_speed = re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\b")
    hits = []
    pos = 0
    for stmt in re.split(r"([;{}])", code):
        speed = u_speed.search(stmt)
        if speed and U_TIME.search(stmt):
            hits.append(code.count("\n", 0, pos + speed.start()) + 1)
        pos += len(stmt)
    return hits


def check_speed_rule():
    # The guard guards itself first, so a regex slip cannot pass everything.
    assert speed_twice("float t = uTime * uSpeed;") == [1]
    assert speed_twice("// uTime * uSpeed\nx = 1.0 +\n  uTime * 0.3 * uSpeed;") == [3]
    assert speed_twice("#define T (uTime * 0.1)\nfloat w = uSpeed * 2.0;") == []
    assert speed_twice("#define T (uTime * uSpeed)\nfloat w = 2.0;") == [1]
    assert speed_twice("float f = uFlowPhase * 0.6 * uSpeed;") == [1]
    assert speed_twice("float spd = uSpeed;\nfloat c = SND_MID_TIME * spd;") == [2]
    assert speed_twice("float f = uFlowTime * uSpeed;") == [1]
    assert speed_twice("float t = uTime * 0.3;\nfloat f = uFlowTime; float w = uSpeed * 2.0; // uTime * uSpeed") == []
    bad = [f"{p.name}:{line}" for p in sorted(SHADERS.glob("*.glsl"))
           for line in speed_twice(p.read_text(encoding="utf-8"))]
    for b in bad:
        print(f"FAIL {b}: an accumulated clock times uSpeed jumps when Speed moves; read uTime, uFlowTime or uSoundTimeSped")
    return bad


# 32-bit uint work: a hex or long decimal uint literal, or a float bit cast.
UINT32 = re.compile(r"\b(0x[0-9a-fA-F]+u|\d{6,}u|floatBitsToUint|uintBitsToFloat)\b")
HIGHP_INT = re.compile(r"precision\s+highp\s+int\s*;")


def int_precision_late(text):
    """True when 32-bit uint work comes before any `precision highp int;`.

    The fragment default for int and uint is mediump, which Mali and PowerVR
    honour at 16 bits: a hash run there collapses into stripes and repeats.
    """
    code = COMMENTS.sub("", text)
    use = UINT32.search(code)
    return bool(use) and not HIGHP_INT.search(code, 0, use.start())


def check_int_precision():
    assert int_precision_late("uint h = x * 0x9e3779b9u;")
    assert int_precision_late("// precision highp int;\nuint h = x * 747796405u;")
    assert int_precision_late("uint b = floatBitsToUint(f);\nprecision highp int;")
    assert not int_precision_late("precision highp int;\nuint h = x * 0x9e3779b9u;")
    assert not int_precision_late("uint i = 3u; float x = 123456.0;")
    bad = [p.name for p in sorted(SHADERS.glob("*_frag.glsl"))
           if int_precision_late(resolve(p.read_text(encoding="utf-8"), ()))]
    for b in bad:
        print(f"FAIL {b}: 32-bit uint work before `precision highp int;` collapses at mediump on Mali/PowerVR")
    return bad


def allow_list():
    # Every double-quoted name outside a `//` comment, as the .inc's header says.
    text = re.sub(r"//[^\n]*", "", INCLUDES_INC.read_text(encoding="utf-8"))
    names = set(re.findall(r'"(\w+)"', text))
    if not names:
        raise SystemExit(f"no include names in {INCLUDES_INC}")
    return names


ALLOWED = allow_list()


def resolve(text, path):
    # Mirrors core/viz/glsl/ShaderSource.cpp: an include repeated outside its own
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
    speed_failed = check_speed_rule()
    precision_failed = check_int_precision()
    glslc = sys.argv[1]
    names = sys.argv[2:] or sorted(
        p.name for p in SHADERS.glob("*_frag.glsl") if p.stem not in ALLOWED
    ) + sorted(
        p.name for p in SHADERS.glob("*_vert.glsl") if p.stem not in ALLOWED
    ) + sorted(SIM_BODIES)
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
    if speed_failed:
        print(f"{len(speed_failed)} statement(s) multiply an accumulated clock by uSpeed")
    if precision_failed:
        print(f"{len(precision_failed)} fragment shader(s) hash in uint before `precision highp int;`")
    sys.exit(1 if failed or speed_failed or precision_failed else 0)


if __name__ == "__main__":
    main()
