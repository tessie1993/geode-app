// JS mirror of the FRAGMENT half of
// engine/scenes/src/main/kotlin/dev/geode/render/compute/SimGlsl.kt.
//
// WHY THIS FILE EXISTS AT ALL. A step that goes through `SimPass` no longer
// ships a whole shader in res/raw: the file holds a BODY - `simStep()` and the
// helpers around it - and the Kotlin generates the `#version`, the precision
// header, the state sampler, the decode helpers and the `main()` that calls
// it. So loading the .glsl and compiling it, which is what this harness does
// for every other style, produces a shader with no main() and no `uSimState`
// and fails at glCompileShader. The choice was between rendering the real body
// through a mirrored wrapper and not rendering the family at all.
//
// WHAT IT DELIBERATELY DOES NOT MIRROR: `SimGlsl.computeStep`. The compute path
// is ES 3.1; WebGL2 is ES 3.0 and has no compute shaders, no image load/store
// and no way to fake either. **Nothing this harness prints says anything about
// the compute path** - not that it compiles, not that it matches. It measures
// the fragment ping-pong, which is the path that ships everywhere and the one
// the compute path is defined against.
//
// The two paths' agreement is not a thing this tool can check. What it can
// check is that the body plus the generated fragment wrapper is the same
// picture the hand-written fragment shader used to draw, which is the question
// a port like this actually raises.
//
// Kept honest the same way lib/hyperspace-math.mjs is: line for line against
// the Kotlin, with the reasoning copied rather than summarised, so a drift
// shows up in a diff. The generated text is compared byte for byte by nothing;
// if it drifts, the audit notices only when a uniform name changes. Read the
// Kotlin when changing this.

/**
 * `SimStateEncoding`, in the four terms the generated source needs.
 *
 * `packed` is the RGBA32UI float-bits encoding. It is representable here and
 * exercised by nothing: the harness's field targets are created through
 * `FieldSim`'s renderable-format probe, which has no integer candidate, so a
 * `packed` encoding would generate a `usampler2D` against an RGBA16F texture.
 * A driver that wants one has to teach `FieldSim` about integer targets first.
 */
export function simEncoding({ packed = false, filterable = true, stateScale = 1 } = {}) {
  return {
    packed,
    // An integer texture cannot be filtered under any circumstances - LINEAR on
    // a usampler2D leaves it incomplete and every fetch reads zero - so the
    // packed encoding interpolates by hand whatever the plan claims. Same
    // override the Kotlin applies in SimPass.build.
    filterable: filterable && !packed,
    stateScale,
    samplerType: packed ? 'highp usampler2D' : 'highp sampler2D',
    texelType: packed ? 'uvec4' : 'vec4',
  };
}

const UNIFORM_STATE = 'uSimState';
const UNIFORM_SIZE = 'uSimSize';

/**
 * The fullscreen triangle the generated fragment step runs with.
 *
 * Deliberately not quad_vert, even though the gl_Position expression is the
 * same six lines: the generated fragment stage derives its coordinate from
 * gl_FragCoord so that simUv() means the same thing on both paths, and the
 * agreement between the two stages is that there are NO varyings between them.
 * quad_vert has one.
 */
export const SIM_FULLSCREEN_VERTEX = [
  '#version 300 es',
  'void main() {',
  '    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));',
  '    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);',
  '}',
].join('\n');

/** GLSL needs a decimal point where Kotlin's Float.toString() always writes one. */
function glslFloat(x) {
  return Number.isInteger(x) ? `${x}.0` : String(x);
}

function loadExpression(enc, fetch) {
  if (enc.packed) return `uintBitsToFloat(${fetch})`;
  if (enc.stateScale !== 1) return `${fetch} * SIM_STATE_SCALE`;
  return fetch;
}

function storeExpression(enc, value) {
  if (enc.packed) return `floatBitsToUint(${value})`;
  // No clamp: writes to a normalised fixed-point target are clamped to [0, 1]
  // by the pipeline, for a colour attachment and for an image store alike.
  if (enc.stateScale !== 1) return `(${value}) / SIM_STATE_SCALE`;
  return value;
}

function sampler(enc) {
  if (enc.filterable) {
    // sampledInFragmentStage = true throughout this file: there is no other
    // stage here. texture() takes its LOD from screen-space derivatives, which
    // exist only in the fragment stage; the compute path spells this
    // textureLod(..., 0.0) for that reason.
    return [
      'vec4 simSample(vec2 uv) {',
      `    return ${loadExpression(enc, `texture(${UNIFORM_STATE}, uv)`)};`,
      '}',
    ];
  }
  return [
    'vec4 simSample(vec2 uv) {',
    `    vec2 p = uv * vec2(${UNIFORM_SIZE}) - 0.5;`,
    '    vec2 f = fract(p);',
    '    ivec2 b = ivec2(floor(p));',
    '    vec4 s00 = simLoad(b);',
    '    vec4 s10 = simLoad(b + ivec2(1, 0));',
    '    vec4 s01 = simLoad(b + ivec2(0, 1));',
    '    vec4 s11 = simLoad(b + ivec2(1, 1));',
    '    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);',
    '}',
  ];
}

function preamble(enc) {
  return [
    'precision highp float;',
    'precision highp int;',
    '',
    `uniform ${enc.samplerType} ${UNIFORM_STATE};`,
    `uniform ivec2 ${UNIFORM_SIZE};`,
    `const float SIM_STATE_SCALE = ${glslFloat(enc.stateScale)};`,
    '',
    'vec2 simUv(ivec2 texel) {',
    `    return (vec2(texel) + 0.5) / vec2(${UNIFORM_SIZE});`,
    '}',
    '',
    'vec4 simLoad(ivec2 texel) {',
    // Clamped, and not optionally: texelFetch outside the texture is undefined
    // in GLSL ES, and a NaN that enters a ping-pong stays there for the life of
    // the scene. Clamping also makes the manual interpolation above behave
    // exactly like GL_CLAMP_TO_EDGE, so the two sampler variants agree at the
    // border instead of near it.
    `    ivec2 at = clamp(texel, ivec2(0), ${UNIFORM_SIZE} - ivec2(1));`,
    `    return ${loadExpression(enc, `texelFetch(${UNIFORM_STATE}, at, 0)`)};`,
    '}',
    '',
    ...sampler(enc),
    '',
  ];
}

/** `SimGlsl.fragmentStep`: one invocation per fragment, out through the attachment. */
export function simFragmentStep(enc, body) {
  const call = `simStep(texel, ${UNIFORM_SIZE}, simLoad(texel))`;
  return [
    '#version 300 es',
    ...preamble(enc),
    `out ${enc.texelType} simOut;`,
    '',
    body.trim(),
    '',
    'void main() {',
    '    ivec2 texel = ivec2(gl_FragCoord.xy);',
    `    simOut = ${storeExpression(enc, call)};`,
    '}',
    '',
  ].join('\n');
}

/** `SimGlsl.displayShader`: the scene's own body, with the decode already in scope. */
export function simDisplayShader(enc, body) {
  return ['#version 300 es', ...preamble(enc), body.trim(), ''].join('\n');
}
