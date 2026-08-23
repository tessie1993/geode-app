// Reads the uniform names a Kotlin scene actually uploads, straight out of
// its source.
//
// This is the whole reason to trust anything this tool renders. A preview
// harness that invents its own uniform values is a drawing of a shader, not a
// preview of a style: a uniform the harness forgets defaults to zero in GL,
// and a zeroed uniform is indistinguishable from a feature that is switched
// off. `uHasMelt = 0` is precisely that failure, and it is the difference
// between a whole medium being there and not.
//
// So the tool does a three-way audit per scene:
//
//   A. what the SHADER declares          (glsl.parseUniforms)
//   B. what the KOTLIN uploads           (here)
//   C. what the HARNESS supplies         (lib/scenes.mjs)
//
// A \ C is fatal - something would silently be zero.
// B \ C is fatal - the harness has drifted behind the app.
// C \ B is fatal - the harness is inventing inputs the app never sends.
// A \ B is reported as a shader-side finding (the app never sets it either).

import fs from 'node:fs';
import path from 'node:path';

const UPLOAD_PATTERNS = [
  /\bloc\(\s*"(u\w+)"\s*\)/g,
  /glGetUniformLocation\(\s*\w+\s*,\s*"(u\w+)"\s*\)/g,
  /\bsetUniform1f\(\s*"(u\w+)"/g,
  /\bcLoc\(\s*"(u\w+)"\s*\)/g,
  // SimUniforms: a step routed through SimPass never sees its own program, so
  // it names uniforms to a setter instead of resolving a location itself.
  // Anchored on the dot so `print(` and friends cannot match.
  /\.(?:float|int|bool|vec2|vec3|vec4|ivec2|sampler|vec4Array)\(\s*"(u\w+)"/g,
];

/**
 * The two uniforms SimPass sets on the scene's behalf, named as constants
 * rather than written at a call site.
 *
 * `uSimState` and `uSimSize` are the layer's contract with every generated
 * shader: the state binding and the grid size, set inside step() and inside
 * bindStateFor(), never by the scene. Scraped from the constants so there is
 * still exactly one place in the repo that spells them.
 */
const CONSTANT_PATTERNS = [/\bconst val UNIFORM_\w+\s*=\s*"(u\w+)"/g];

/**
 * Uploads a scene hands to a shared uploader rather than writing out itself.
 *
 * `render/scene/SceneTouch.kt` owns the touch block for every family that is
 * NOT a fragment style - cymatics, the four field sims, the beam - because six
 * hand-rolled copies of one packing is six chances for it to drift. A scene
 * that calls it uploads those uniforms as surely as if the glUniform calls sat
 * in its own draw(), and an audit that could not see them would report the app
 * as silently zeroing what it does in fact send - the exact failure this file
 * exists to prevent, inverted.
 *
 * The names are deliberately NOT listed here: the delegate is scanned with the
 * same patterns as the scene, so there is still exactly one place in the repo
 * that says which uniforms it writes.
 */
const DELEGATES = [
  { call: /\bSceneTouch\.upload\s*\(/, file: 'SceneTouch.kt' },
  // A scene that builds a SimPass gets uSimState and uSimSize uploaded for it,
  // on both the compute and the fragment path. Without this the audit would
  // report the app as silently zeroing the state binding it in fact sets.
  { call: /\bSimPass\.build\s*\(/, file: '../compute/SimGlsl.kt', patterns: CONSTANT_PATTERNS },
];

function scanInto(names, src, patterns = UPLOAD_PATTERNS) {
  for (const re of patterns) {
    re.lastIndex = 0;
    let m;
    while ((m = re.exec(src)) !== null) names.add(m[1]);
  }
}

/**
 * Uniform names uploaded by a Kotlin scene.
 *
 * Covers the five call shapes in this codebase:
 *   loc("uName")                              - fluid scenes
 *   glGetUniformLocation(program, "uName")    - ShaderScene's samplers
 *   setUniform1f("uName", ...)                - ShaderScene's scalars
 *   cLoc("uName")                             - the composite pass
 *   .float("uName") / .vec4Array("uName")     - a step routed through SimPass
 * plus whatever the scene delegates (see [DELEGATES]). One level only, and the
 * delegate is resolved as a sibling of the scene, which is where every scene in
 * this codebase lives relative to its helpers.
 */
export function extractUploadedUniforms(kotlinPath) {
  const src = fs.readFileSync(kotlinPath, 'utf8');
  const names = new Set();
  scanInto(names, src);
  for (const d of DELEGATES) {
    if (!d.call.test(src)) continue;
    const sibling = path.join(path.dirname(kotlinPath), d.file);
    if (!fs.existsSync(sibling)) continue;
    scanInto(names, fs.readFileSync(sibling, 'utf8'), d.patterns);
  }
  return names;
}

/**
 * The three-way audit. Returns `{ errors, notes }`; a non-empty `errors`
 * means the render would not be trustworthy and the caller should refuse to
 * present it as one.
 */
export function auditUniforms({ sceneId, declared, uploaded, supplied, ignoreUploaded = [] }) {
  const errors = [];
  const notes = [];
  const declaredNames = new Set(declared.map((d) => d.name));
  const ignore = new Set(ignoreUploaded);

  for (const d of declared) {
    if (!supplied.has(d.name)) {
      errors.push(
        `[${sceneId}] shader declares '${d.name}' (${d.type}${d.isArray ? `[${d.length}]` : ''}) ` +
        `but the harness sets no value - it would render as zero`,
      );
    }
  }
  for (const name of uploaded) {
    if (ignore.has(name)) continue;
    if (!supplied.has(name)) {
      errors.push(
        `[${sceneId}] Kotlin uploads '${name}' but the harness does not - the harness has drifted behind the app`,
      );
    }
  }
  for (const name of supplied) {
    if (!uploaded.has(name) && !ignore.has(name)) {
      errors.push(
        `[${sceneId}] the harness supplies '${name}' but no Kotlin upload of it was found - invented input`,
      );
    }
  }
  for (const name of uploaded) {
    if (!declaredNames.has(name) && !ignore.has(name)) {
      notes.push(`[${sceneId}] Kotlin uploads '${name}' but this shader does not declare it (no-op, location -1)`);
    }
  }
  for (const d of declared) {
    if (!uploaded.has(d.name)) {
      notes.push(`[${sceneId}] shader declares '${d.name}' but no Kotlin upload was found - APP-SIDE zero`);
    }
  }
  return { errors, notes };
}
