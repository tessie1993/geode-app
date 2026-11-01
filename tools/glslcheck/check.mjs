// glslcheck - compiles and renders the app's shader assets headlessly.
//
// It answers two questions per shader, "does the GLSL the native core would
// assemble compile under an ES 3.00 grammar" and "does one frame of it come
// out non-black", and it writes the frame to a PNG so the answer can be
// looked at rather than trusted.
//
// The fragment style ids are the SHADER-kind `style|` lines of
// core/tests/catalogue.txt, the catalogue's text table (geode_catalogue_text).
// catalogue_test checks that file against core/viz/Catalogue.cpp, so the list
// here is the list the app ships.
//
// Includes are resolved the way core/viz/ShaderSource.cpp resolves them: the
// registered names are PARSED OUT OF THAT FILE (kIncludes), the line pattern
// is the same anchored `//#include <name>`, and an unknown name is a hard
// error - so a library added to the app without registering it fails here
// the same way it fails on the phone.
//
// Uniform values mirror ShaderScene::uploadParams / uploadMotion / uploadTouch
// and SoundUniforms::upload with SceneParams defaults and a mid-loudness sound
// frame. Every uniform the
// resolved GLSL declares must have a value in the table or the run refuses:
// a uniform the harness forgot defaults to zero in GL, and a zeroed feature
// is indistinguishable from a broken one.
//
// What it cannot tell you: rendering is ANGLE over SwiftShader, which ignores
// precision qualifiers, tolerates uniform counts real ES 3.0 drivers reject,
// and is not a Mali or an Adreno. This is a lower bound on portability, not a
// device check.
//
//   node check.mjs --list
//   node check.mjs --shader kifs --out out/kifs.png
//   node check.mjs --all --size 200
//   node check.mjs --fluid-display               # every SHADING/BLOOM/SUNRAYS x LOOK variant

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { launch } from './cdp.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const SHADERS = path.join(ROOT, 'app', 'src', 'main', 'assets', 'shaders');
const SHADER_SOURCE_CPP = path.join(ROOT, 'core', 'viz', 'ShaderSource.cpp');
const CATALOGUE_TXT = path.join(ROOT, 'core', 'tests', 'catalogue.txt');
const FLUID_LOOK_HPP = path.join(ROOT, 'core', 'viz', 'fluid', 'FluidLook.hpp');

// ---- includes, as ShaderSource.cpp does them ------------------------------

function parseIncludeRegistry() {
  const src = fs.readFileSync(SHADER_SOURCE_CPP, 'utf8');
  const m = src.match(/kIncludes\s*=\s*\{([\s\S]*?)\};/);
  if (!m) throw new Error(`could not find kIncludes in ${SHADER_SOURCE_CPP}`);
  return [...m[1].matchAll(/"([A-Za-z0-9_]+)"/g)].map((x) => x[1]);
}

const INCLUDE_LINE = /^[ \t]*\/\/#include[ \t]+(\w+)[ \t]*$/;

// Recursive, as ShaderSource::resolveIncludes is: a nested include (lib_sound
// inside lib_scene_uniforms) is pasted too, and an include already on the
// current chain is a circular-include error. One repeated outside its own
// chain is pasted again, so a double include fails as a redefinition here
// the way it does on the device.
function resolveIncludes(source, registry, chain = []) {
  const out = [];
  for (const rawLine of source.split('\n')) {
    const line = rawLine.replace(/\r$/, '');
    const m = line.match(INCLUDE_LINE);
    if (!m) {
      out.push(rawLine);
      continue;
    }
    if (!registry.includes(m[1])) throw new Error(`unknown shader include '${m[1]}'`);
    if (chain.includes(m[1])) throw new Error(`circular shader include '${m[1]}'`);
    const file = path.join(SHADERS, `${m[1]}.glsl`);
    if (!fs.existsSync(file)) throw new Error(`missing shader include '${m[1]}'`);
    out.push(resolveIncludes(fs.readFileSync(file, 'utf8'), registry, [...chain, m[1]]));
  }
  return out.join('\n');
}

// `style|<id>|<KIND>[|MARCH_DETAIL]`, in catalogue order; SHADER is the kind
// ShaderScene draws from shaders/<id>_frag.glsl (the loose styles and every
// style family).
function shaderIds() {
  const lines = fs.readFileSync(CATALOGUE_TXT, 'utf8').split('\n');
  if (!lines[0].startsWith('catalogue|')) throw new Error(`${CATALOGUE_TXT} is not a catalogue table`);
  const ids = lines.map((l) => l.split('|')).filter((f) => f[0] === 'style' && f[2] === 'SHADER').map((f) => f[1]);
  if (!ids.length) throw new Error(`no SHADER styles in ${CATALOGUE_TXT}`);
  return ids;
}

function fluidLookCount() {
  const src = fs.readFileSync(FLUID_LOOK_HPP, 'utf8');
  const m = src.match(/kLookCount\s*=\s*(\d+)/);
  return m ? Number(m[1]) : 1;
}

// ---- uniform declarations --------------------------------------------------

function stripComments(s) {
  return s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
}

function parseUniforms(glsl) {
  const clean = stripComments(glsl);
  const defines = new Map();
  for (const m of clean.matchAll(/#define\s+(\w+)\s+(\d+)/g)) defines.set(m[1], Number(m[2]));
  const out = [];
  for (const m of clean.matchAll(/uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)(?:\s*\[\s*(\w+)\s*\])?\s*;/g)) {
    let count = 1;
    if (m[3] !== undefined) count = /^\d+$/.test(m[3]) ? Number(m[3]) : defines.get(m[3]);
    if (!count) throw new Error(`cannot resolve array length of uniform ${m[2]}`);
    out.push({ type: m[1], name: m[2], count });
  }
  return out;
}

// ---- the values the app uploads --------------------------------------------
//
// ShaderScene::uploadParams with SceneParams defaults (palette 0 = Spectrum,
// base 0, range 1), a mid-loudness frame from the motion layer and the sound
// engine, no fingers. Anything not here is a fatal audit failure, by design.

// uSound: every GeodeSoundFrame scalar in lib_sound.glsl's order, four to a
// vec4 (SND_* names in the comments). Relative levels sit near 1, shaped
// values near 0.5, clocks somewhere past the start, one-frame triggers at 0.
const SOUND_SCALARS = [
  1.1, 0.9, 0.8, 1.0, //        BASS MID TREBLE VOL
  1.0, 0.9, 0.8, 1.0, //        *_ATT
  1.0, 1.0, 0.85, 0.85, //      SUB SUB_ATT MID_HIGH MID_HIGH_ATT
  0.55, 0.45, 0.35, 0.5, //     LOW MIDDLE HIGH LEVEL
  0.6, 0.5, 0.4, 0.5, //        *_PRESENCE INTENSITY
  37, 35, 33, 36, //            BASS_TIME MID_TIME TREBLE_TIME CURVED_TIME
  41, 22, 0.6, 0.25, //         HIT_TIME NOTE_TIME PRESENCE RMS
  0.5, 0.3, 0.5, 0.2, //        LOUDNESS CENTROID ROLLOFF FLATNESS
  0.4, 0.5, 0.3, 0.0, //        DENSITY_SLOW DENSITY_FAST FLUX ENERGY_TREND
  0.2, 0.5, 0.4, 0.0, //        PEAK_FREQ PEAK_MAG STEREO_WIDTH STEREO_PAN
  0.3, 0.2, 0.25, 0.3, //       KICK_ENV SNARE_ENV HAT_ENV ONSET_ENV
  1, 0, 0.37, 0.62, //          KICK_TOGGLE SNARE_TOGGLE KICK_RANDOM NOTE_RANDOM
  0, 0, 0, 0, //                KICK SNARE HAT KICK2
  0, 0, 0, 0, //                KICK4 KICK8 KICK16 SNARE2
  0, 0, 0, 0, //                SNARE4 HARD_CUT ONSET SILENCE
  0, 0, 0, 0.5, //              RETURNED SURGE NOTE_CHANGE LEGATO
  0, 0.4, 0.25, 0.5, //         REATTACK PITCH PITCH_CLASS PITCH_HEIGHT
  0.6, 0, 0, 0.1, //            PITCH_CONFIDENCE INTERVAL GLIDE VIBRATO
  0.5, 0.4, 120, 0.3, //        EVENNESS RUN_DENSITY BPM BEAT_PHASE
  0.7, 0, 57, 0, //             BEAT_CONF BEAT BEAT_COUNT DOWNBEAT
  0.3, 14, 0, 3, //             BAR_PHASE BAR_COUNT PHRASE PHRASE_INDEX
  0.5, 0.3, 0.3, 0.2, //        PHRASE_PHASE FLUX_LOW FLUX_MID FLUX_HIGH
  0.3, 0.3, 4, 1, //            HFC KEY_HUE KEY_ROOT KEY_MAJOR
  0.6, 0.4, 0.3, 0, //          KEY_CLARITY CHORD_HUE TENSION HARMONIC_CHANGE
  1, 4, 0.6, 0.3, //            SCALE SCALE_ROOT SCALE_BRIGHTNESS SCALE_TENSION
  0.7, 0.2, 0, 1, //            SCALE_FIT NOVELTY SECTION_CHANGE SECTION_ID
  1, 8, 0.2, 0, //              SECTION_STATE SECTION_BARS BUILD_UP DROP
];

const SHADER_VALUES = {
  uTime: 37.0, uResolution: [0, 0],
  uSpeed: 1, uZoom: 1, uRotation: 0, uZoomLayers: [1, 1, 0], uColorShift: 0, uHueRange: 1, uSat: 1, uBright: 1,
  uInvert: 0, uIntensity: 1, uMirrorX: 0, uTurbulence: 0, uPalBase: 0, uPalRange: 1,
  uContrast: 1, uGamma: 1, uPal2Base: 0, uPal2Range: 1, uPaletteMix: 0, uDuotone: 0, uBloom: 0, uWarp: 0,
  uRipple: 0, uSymmetry: 6, uKaleido: 0, uMorph: 0, uPixelate: 0, uPosterize: 0, uSway: 0, uPulse: 0,
  uDriftX: 0, uDriftY: 0, uShake: 0, uTile: 1, uTwist: 0, uTemperature: 0, uSolarize: 0,
  uFlash: 0,
  uTouchAnchor: [0, 0, 0, 0], uTouchPoints: [0, 0, 0, 0], uTouchCount: 0, uTouchGesture: 0, uTouchAxis: [0, 0],
  uTouchSpin: 0,
  uSteps: 96,
  uPalLutMix: 0, uPalLutRow: 0.5,
  uSpike: 0.15,
  uMoveDir: [0.8, 0.6], uSpawnSeed: 0.37, uSpawnAge: 4.0, uFormPhase: 0.62, uFlowPhase: 12.5,
  uEnergyRel: 0.5, uBassRel: 0.5, uMidRel: 0.5, uTrebRel: 0.5, uMotionBright: 0.5, uHarmony: 0.6,
  uKeyHue: 0.3, uKeyStrength: 0.5, uBeatOsc: 0.4, uBarOsc: 0.6, uOrbit: [0.3, -0.2], uDrift: 0.2,
  uBreath: 0.5, uMotion: 0.5,
  // SoundUniforms::upload: the frame, the Audio drive slider at its default, held events, counts.
  uSound: SOUND_SCALARS, uSoundDrive: 1, uSoundEvents: [0.2, 0, 0, 0], uSoundCounts: [41, 3],
  // CameraRig at rest: eye on the path at the base height, looking down +z, base focal.
  uCamPos: [0, 0, 37], uCamBasis: [1, 0, 0, 0, 1, 0, 0, 0, 1], uCamFocal: 1.6, uCamTravel: 37,
  uCamDolly: 0, uCamLook: [0, 0, 0],
};

const SHADER_TEXTURES = { uSoundTex: 'sound', uPalLut: 'palette' };

// FluidLook::drawDisplay with mid-loudness audio.
const FLUID_VALUES = {
  uInvRes: [0, 0], uAspect: 1, uDitherScale: [1, 1], uTexelSize: [0, 0],
  uTime: 37.0, uAudio: [0.55, 0.45, 0.35, 0.5], uLookHue: 0.2,
};
const FLUID_TEXTURES = { uDye: 'dye', uBloom: 'dye', uSunrays: 'white', uDither: 'noise' };

function plan(uniforms, values, textures, width, height) {
  const missing = [];
  const out = [];
  for (const u of uniforms) {
    if (u.type === 'sampler2D') {
      if (!(u.name in textures)) missing.push(u.name);
      else out.push({ ...u, texture: textures[u.name] });
      continue;
    }
    if (!(u.name in values)) {
      missing.push(u.name);
      continue;
    }
    let value = values[u.name];
    if (Array.isArray(value) && u.type === 'vec4' && value.length !== 4 && value.length !== u.count * 4) {
      throw new Error(`uniform ${u.name}: ${value.length} floats for vec4[${u.count}]`);
    }
    if (u.name === 'uResolution') value = [width, height];
    if (u.name === 'uInvRes' || u.name === 'uTexelSize') value = [1 / width, 1 / height];
    out.push({ ...u, value });
  }
  if (missing.length) throw new Error(`no value for uniform(s): ${missing.join(', ')} - add them to the table`);
  return out;
}

// ---- driving the page ------------------------------------------------------

function withKeywords(src, defines) {
  if (!defines.length) return src;
  const nl = src.indexOf('\n', src.indexOf('#version'));
  return `${src.slice(0, nl + 1)}${defines.map((d) => `#define ${d}\n`).join('')}${src.slice(nl + 1)}`;
}

function parseArgs(argv) {
  const a = { size: 240, height: null, shaders: [], all: false, list: false, fluidDisplay: false, out: null, time: null };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === '--list') a.list = true;
    else if (v === '--all') a.all = true;
    else if (v === '--fluid-display') a.fluidDisplay = true;
    else if (v === '--shader') a.shaders.push(argv[++i]);
    else if (v === '--height') a.height = Number(argv[++i]);
    else if (v === '--size') a.size = Number(argv[++i]);
    else if (v === '--out') a.out = argv[++i];
    else if (v === '--time') a.time = Number(argv[++i]);
    else throw new Error(`unknown flag ${v}`);
  }
  return a;
}

function report(name, r) {
  const status = r.error ? 'FAIL' : 'ok  ';
  const stats = r.error
    ? r.error
    : `mean ${r.meanLuma.toFixed(3)} max ${r.maxLuma.toFixed(3)} black ${r.fracBlack.toFixed(3)} blown ${r.fracBlown.toFixed(3)}` +
      (r.glError ? ` glError ${r.glError}` : '') + (r.dropped.length ? ` dropped[${r.dropped.join(',')}]` : '');
  console.log(`${status} ${name.padEnd(28)} ${stats}`);
  return !r.error;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const registry = parseIncludeRegistry();
  const ids = shaderIds();
  if (args.list) {
    console.log(ids.join('\n'));
    return;
  }
  const jobs = [];
  const quadVert = fs.readFileSync(path.join(SHADERS, 'quad_vert.glsl'), 'utf8');
  const targets = args.all ? ids : args.shaders;
  for (const id of targets) {
    const file = path.join(SHADERS, `${id}_frag.glsl`);
    if (!fs.existsSync(file)) throw new Error(`no such fragment style: ${id}`);
    const frag = resolveIncludes(fs.readFileSync(file, 'utf8'), registry);
    const values = { ...SHADER_VALUES };
    if (args.time !== null) values.uTime = args.time;
    jobs.push({ name: id, vert: quadVert, frag, uniforms: plan(parseUniforms(frag), values, SHADER_TEXTURES, args.size, args.height ?? args.size) });
  }
  if (args.fluidDisplay) {
    const vert = fs.readFileSync(path.join(SHADERS, 'fluid_base_vert.glsl'), 'utf8');
    const src = fs.readFileSync(path.join(SHADERS, 'fluid_display_frag.glsl'), 'utf8');
    const looks = fluidLookCount();
    for (let look = 0; look < looks; look++) {
      for (let flags = 0; flags < 8; flags++) {
        const defines = [`LOOK ${look}`];
        if (flags & 1) defines.push('SHADING');
        if (flags & 2) defines.push('BLOOM');
        if (flags & 4) defines.push('SUNRAYS');
        const frag = withKeywords(src, defines);
        const vertUniforms = parseUniforms(vert);
        jobs.push({
          name: `fluid_display L${look} f${flags}`,
          vert,
          frag,
          uniforms: plan([...vertUniforms, ...parseUniforms(frag)], FLUID_VALUES, FLUID_TEXTURES, args.size, args.size),
        });
      }
    }
  }
  if (!jobs.length) throw new Error('nothing to do: --shader <id>, --all or --fluid-display');

  const browser = await launch();
  let allOk = true;
  try {
    const page = await browser.openPage(pathToFileURL(path.join(HERE, 'page.html')).href);
    for (const job of jobs) {
      const r = await page.call('__render', { ...job, width: args.size, height: args.height ?? args.size });
      allOk = report(job.name, r) && allOk;
      if (args.out && r.png) {
        const dest = jobs.length === 1 ? args.out : path.join(args.out, `${job.name.replace(/[^\w]+/g, '_')}.png`);
        fs.mkdirSync(path.dirname(dest), { recursive: true });
        fs.writeFileSync(dest, Buffer.from(r.png, 'base64'));
      }
    }
    for (const line of page.drainConsole()) console.log(`  [page] ${line}`);
  } finally {
    await browser.close();
  }
  process.exit(allOk ? 0 : 1);
}

main().catch((e) => {
  console.error(e.message);
  process.exit(2);
});
