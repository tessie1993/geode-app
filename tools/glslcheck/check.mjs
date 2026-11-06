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
// registered names are read from core/viz/ShaderIncludes.inc, the allow-list
// ShaderSource.cpp compiles in (kIncludes), the line pattern
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
//   node check.mjs --shader black_hole --sound SND_KICK_ENV=1,SND_DROP=1 --set uMorph=0.8
//   node check.mjs --stills out/stills.tsv [--shard 1/4] [--out out/stills]
//                                                 # the visual quality gate's input: every style through
//                                                 # the sound engine's golden song, judged by
//                                                 # core/tests/visual_gate.cpp (README, "Stills gate")
//
// --sound sets single slots of the sound frame by their lib_sound name (the
// rest keep the table's mid-loudness groove below), so a
// style can be looked at on a kick, in a drop or in silence. --set overrides
// any other uniform by name (a comma list for a vector).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { launch } from './cdp.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const SHADERS = path.join(ROOT, 'app', 'src', 'main', 'assets', 'shaders');
const SHADER_INCLUDES_INC = path.join(ROOT, 'core', 'viz', 'ShaderIncludes.inc');
const CATALOGUE_TXT = path.join(ROOT, 'core', 'tests', 'catalogue.txt');
const FLUID_LOOK_HPP = path.join(ROOT, 'core', 'viz', 'fluid', 'FluidLook.hpp');
const GOLDEN_SONG = path.join(ROOT, 'core', 'tests', 'golden', 'sound_frames.tsv');
const FLASH_LIMITER_HPP = path.join(ROOT, 'core', 'viz', 'FlashLimiter.hpp');

// ---- includes, as ShaderSource.cpp does them ------------------------------

function parseIncludeRegistry() {
  // Every double-quoted name outside a `//` comment, as the .inc's header says.
  const src = fs.readFileSync(SHADER_INCLUDES_INC, 'utf8').replace(/\/\/[^\n]*/g, '');
  const names = [...src.matchAll(/"([A-Za-z0-9_]+)"/g)].map((x) => x[1]);
  if (!names.length) throw new Error(`no include names in ${SHADER_INCLUDES_INC}`);
  return names;
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
  1, 0, 0, 0, //                FADE_GAIN (and the vec4's padding)
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
  // The style knobs (lib_scene_uniforms): every knob at the 0.5 ShaderScene holds them at (array fill).
  uKnob: [0.5],
  uSteps: 96,
  uPalLutMix: 0, uPalLutRow: 0.5,
  uSpike: 0.15,
  uMoveDir: [0.8, 0.6], uSpawnSeed: 0.37, uSpawnAge: 4.0, uFormPhase: 0.62, uFlowPhase: 12.5,
  uEnergyRel: 0.5, uBassRel: 0.5, uMidRel: 0.5, uTrebRel: 0.5, uMotionBright: 0.5, uHarmony: 0.6,
  uKeyHue: 0.3, uKeyStrength: 0.5, uBeatOsc: 0.4, uBarOsc: 0.6, uOrbit: [0.3, -0.2], uDrift: 0.2,
  uBreath: 0.5, uMotion: 0.5,
  // The Speed-integrated clocks (core/viz/SpeedClock.hpp) at uSpeed 1, where they equal the unscaled
  // ones: uFlowTime is uFlowPhase, uSoundTimeSped the BASS_TIME MID_TIME TREBLE_TIME scalars below.
  uFlowTime: 12.5, uSoundTimeSped: [37, 35, 33],
  // SoundUniforms::upload: the frame, the Audio drive slider at its default, held events, counts.
  uSound: SOUND_SCALARS, uSoundDrive: 1, uSoundEvents: [0.2, 0, 0, 0], uSoundCounts: [41, 3],
  // CameraRig at rest: eye on the path at the base height, looking down +z, base focal.
  uCamPos: [0, 0, 37], uCamBasis: [1, 0, 0, 0, 1, 0, 0, 0, 1], uCamFocal: 1.6, uCamTravel: 37,
  uCamDolly: 0, uCamLook: [0, 0, 0],
  // Morph World's genome (lib_world): seed 0, Bubble Nave, as `world_genome_test --dump 0` prints it.
  // Every seed: `--set uWorld=$(world_genome_test --dump <i>)`.
  uWorld: [1, 6, 5, 1, 0.5, 0.6, 0, 3, 1.6, 0.3, 0.3, 0.8, 0.8, 0.8, 0.6, 1.1, 0, 0.6, 0, 0, 6, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1.6, 0.05, 0, 1.4, 8, 1.6, 2, 0.8, 0.2, 0, 0.12, 0.2, 0, 0, 0.8, 0.55, 0.4, 0.9, 0.04, 1.6, 0, 0, 1, 0, 0, 1, 0.0, -3.0, 0.5, 0.0, 0.5, 0.0, 0.0, 1.5, 0.15, 0.0, 0.0, 0.0, 0.0, 0, 1.47569, 5, 0, 0, 0, 1.9, 1, 0, 1, 0, 0, 0],
};

const SHADER_TEXTURES = { uSoundTex: 'sound', uPalLut: 'palette', uWorldShapes: 'world_shapes', uWorldTex: 'world_tex' };

// Morph World's baked assets (core/viz/world/WorldAssets), handed to the page as the app uploads them:
// the shape volumes as one 32 x 32 x 512 R8 TEXTURE_3D, the detail maps as an 8-layer R8 TEXTURE_2D_ARRAY.
function worldAssets() {
  const read = (f) => fs.readFileSync(path.join(SHADERS, f)).toString('base64');
  return {
    world_shapes: { target: '3d', w: 32, h: 32, d: 512, b64: read('world_shapes.bin') },
    world_tex: { target: 'array', w: 128, h: 128, d: 8, b64: read('world_textures.bin') },
  };
}

// FluidLook::drawDisplay with mid-loudness audio.
const FLUID_VALUES = {
  uInvRes: [0, 0], uAspect: 1, uDitherScale: [1, 1], uTexelSize: [0, 0],
  uTime: 37.0, uAudio: [0.55, 0.45, 0.35, 0.5], uLookHue: 0.2,
};
const FLUID_TEXTURES = { uDye: 'dye', uBloom: 'dye', uSunrays: 'white', uDither: 'noise', uTemperature: 'dye' };

function plan(uniforms, values, textures, width, height) {
  const missing = [];
  const out = [];
  for (const u of uniforms) {
    if (u.type.startsWith('sampler')) {
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


// ---- Liquid mode (--liquid <style>) -----------------------------------------
//
// Runs the style and liquid_frag.glsl together for --frames frames at 30 fps and writes four
// snapshots of what the screen shows. The per-frame numbers mirror core/viz/liquid/LiquidMath
// (injectWeight, bornRadius, Dive, BornClock) and LiquidPass::run; when those change, change these.
function mulberry(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
function injectWeight(l, dt) {
  const per60 = Math.exp(Math.log(0.5) + (Math.log(0.012) - Math.log(0.5)) * l);
  return 1 - Math.pow(1 - per60, dt * 60);
}
const bornRadius = (l) => 0.12 + 0.08 * l;
const ease = (v, t, tau, dt) => v + (t - v) * (1 - Math.exp(-dt / tau));

function liquidJobs(args, registry) {
  const W = args.size, H = args.height ?? args.size;
  const aspect = W / H;
  const quadVert = fs.readFileSync(path.join(SHADERS, 'quad_vert.glsl'), 'utf8');
  const styleFile = (id) => {
    const f = path.join(SHADERS, `${id}_frag.glsl`);
    if (!fs.existsSync(f)) throw new Error(`no such fragment style: ${id}`);
    return resolveIncludes(fs.readFileSync(f, 'utf8'), registry);
  };
  const styleFrag = styleFile(args.liquid);
  const bornId = args.born === 2 ? (args.bornStyle ?? 'kifs') : args.liquid;
  const bornFrag = styleFile(bornId);
  const liquidFrag = fs.readFileSync(path.join(SHADERS, 'liquid_frag.glsl'), 'utf8');
  const styleU = parseUniforms(styleFrag);
  const bornU = parseUniforms(bornFrag);
  const liquidU = parseUniforms(liquidFrag);
  const rng = mulberry(args.seed);
  const uni = (lo, hi) => lo + (hi - lo) * rng();
  let course = 0, tx = 0, ty = 0, tTwist = 0, tSurge = 1, vx = 0, vy = 0, twist = 0, surge = 1;
  let roll = { uColorShift: 0.0, uZoom: 1.0, uTwist: 0.0, uSymmetry: 6 };
  let dip = -1, gain = 1, nextSwap = 4.0;
  const dt = 1 / 30;
  const frames = [];
  const snaps = new Set([Math.floor(args.frames * 0.25), Math.floor(args.frames * 0.5), Math.floor(args.frames * 0.75), args.frames - 1]);
  const t0 = args.time ?? 37;
  for (let i = 0; i < args.frames; i++) {
    const t = t0 + i * dt;
    course -= dt;
    if (course <= 0) {
      course = uni(2.5, 7);
      const r = 0.22 * Math.sqrt(uni(0, 1)), a = uni(0, 6.2831853);
      tx = r * Math.cos(a); ty = r * Math.sin(a);
      tTwist = uni(0, 1) < 0.35 ? uni(-0.5, 0.5) : 0;
      tSurge = uni(0.6, 1.5);
    }
    vx = ease(vx, tx, 1.8, dt); vy = ease(vy, ty, 1.8, dt);
    twist = ease(twist, tTwist, 1.5, dt); surge = ease(surge, tSurge, 1.2, dt);
    const rate = 0.25 * surge;
    // BornClock with a phrase every 8 s.
    if (dip < 0 && t - t0 >= nextSwap) { dip = 0; nextSwap += 8; }
    if (dip >= 0) {
      const halfFade = 0.225, before = dip;
      dip += dt;
      if (before < halfFade && dip >= halfFade) {
        roll = { uColorShift: uni(0, 1), uZoom: uni(0.7, 1.8), uTwist: uni(-0.6, 0.6), uSymmetry: Math.round(uni(3, 10)) };
      }
      gain = Math.min(1, Math.abs(dip - halfFade) / halfFade);
      if (dip >= 0.45) { dip = -1; gain = 1; }
    }
    const styleValues = { ...SHADER_VALUES, uTime: t };
    const bornValues = { ...SHADER_VALUES, uTime: t + 11, ...(args.born === 1 ? roll : {}) };
    const threeD = args.threeD;
    const liquidValues = {
      uVelScale: [64 / 32, 64 / 32], uTexel: [1 / W, 1 / H], uStyleTexel: [1 / W, 1 / H], uDt: dt, uAspect: aspect,
      uVp: threeD ? [0, 0] : [vx, vy], uScale: threeD ? 1 : Math.pow(2, rate * dt), uTwist: threeD ? 0 : twist * dt,
      uInject: injectWeight(args.liquidity, dt), uEdge: 0.6 * (1 - args.liquidity * 0.5), uSharpen: 0.35,
      uBornR: bornRadius(args.liquidity), uBornGain: threeD ? 0 : gain, uBornMode: args.born === 3 ? 3 : 0,
      uAge: args.age, uSurface: args.surface, uFeedback: 1, uShimmer: 0.35, uLight: 0.5, uTime: t,
    };
    frames.push({
      time: t,
      snap: snaps.has(i),
      style: plan(styleU, styleValues, SHADER_TEXTURES, W, H),
      born: args.born === 1 || args.born === 2 ? plan(bornU, bornValues, SHADER_TEXTURES, W, H) : [],
      liquid: plan(liquidU, liquidValues, { uPrev: 'prev', uStyle: 'style', uBorn: 'born', uVel: 'vel' }, W, H),
    });
  }
  return { styleVert: quadVert, styleFrag, bornFrag, liquidVert: quadVert, liquidFrag, frames, bornSeparate: args.born === 1 || args.born === 2, width: W, height: H };
}

// ---- driving the page ------------------------------------------------------

function withKeywords(src, defines) {
  if (!defines.length) return src;
  const nl = src.indexOf('\n', src.indexOf('#version'));
  return `${src.slice(0, nl + 1)}${defines.map((d) => `#define ${d}\n`).join('')}${src.slice(nl + 1)}`;
}

// The lib_sound names, parsed out of the library: SND_X -> (uniform, flat slot).
function soundSlots() {
  const src = fs.readFileSync(path.join(SHADERS, 'lib_sound.glsl'), 'utf8');
  const lanes = { x: 0, y: 1, z: 2, w: 3 };
  const out = {};
  for (const m of src.matchAll(/^#define\s+(SND_\w+)\s+(uSound\w*)(?:\[(\d+)\])?\.([xyzw])\s*$/gm)) {
    out[m[1]] = { uniform: m[2], slot: (m[3] ? Number(m[3]) * 4 : 0) + lanes[m[4]] };
  }
  return out;
}

function applyOverrides(values, args) {
  if (args.sound.length) {
    const slots = soundSlots();
    const arrays = { uSound: [...values.uSound], uSoundEvents: [...values.uSoundEvents], uSoundCounts: [...values.uSoundCounts] };
    for (const kv of args.sound) {
      const [name, val] = kv.split('=');
      const s = slots[name];
      if (!s) throw new Error(`no such sound slot: ${name}`);
      arrays[s.uniform][s.slot] = Number(val);
    }
    Object.assign(values, arrays);
  }
  for (const kv of args.set) {
    const [name, val] = kv.split('=');
    if (!(name in values)) throw new Error(`no such uniform: ${name}`);
    const nums = val.split(',').map(Number);
    values[name] = nums.length === 1 ? nums[0] : nums;
  }
}

// ---- The stills gate (--stills <tsv>) ----------------------------------------
//
// Every style through the sound engine's own output, not a hand-made groove: core/tests/golden/
// sound_frames.tsv is every GeodeSoundFrame scalar at 60 fps over sound_golden_test's 20 s synthetic song
// (verse, a build with a snare roll, a drop in a new key with four on the floor and sixteenth hats, a
// second of silence, the verse again). Its columns are matched to lib_sound's SND_* names by name, so a
// field added to one and not the other stops the run instead of shifting every slot after it.
//
// Two reads per style, written to one TSV for core/tests/visual_gate.cpp to judge:
//   stills  STILL_TIMES at --still-size: black and blown fractions (frameStats in page.html).
//   run     every golden frame from RUN_START to RUN_END (the end of the build into the drop, the most
//           strobe-prone stretch of the song), at --run-size, each boxed to the kProbeGrid grid
//           OutputProbe reads on the phone; the gate counts them with FlashLimiter's own counters.
// Song seconds, not uTime: uTime runs at TIME_OFFSET + song time, so the clocks are past their start the
// way the default table's 37 is.
const STILL_TIMES = [4.0, 9.5, 12.0, 18.5]; // verse, build, drop, verse again after the silence
const RUN_START = 9.5;
const RUN_END = 12.0;
const TIME_OFFSET = 30.0;
// The held events' fall times (lib_sound: hard cut 2.5 s, surge 2 s, returned 2.5 s, note change 0.5 s).
const HELD = [['hardCut', 2.5], ['surge', 2.0], ['returned', 2.5], ['noteChange', 0.5]];

function probeGridSide() {
  const m = fs.readFileSync(FLASH_LIMITER_HPP, 'utf8').match(/kProbeGrid\s*=\s*(\d+)/);
  if (!m) throw new Error(`no kProbeGrid in ${FLASH_LIMITER_HPP}`);
  return Number(m[1]);
}

// The golden song as one sound frame per row, in lib_sound's uSound order, plus the CPU-held events and
// counts SoundUniforms uploads beside it (stepped here from the triggers, as ReactionState steps them).
function goldenSong() {
  const lines = fs.readFileSync(GOLDEN_SONG, 'utf8').split('\n').filter((l) => l.trim());
  const header = lines[0].split('\t');
  if (header[0] !== 't') throw new Error(`${GOLDEN_SONG} has no t column`);
  const slots = soundSlots();
  const order = header.slice(1).map((name) => {
    const snd = `SND_${name.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toUpperCase()}`;
    const s = slots[snd];
    if (!s || s.uniform !== 'uSound') throw new Error(`golden column ${name} has no lib_sound slot ${snd}`);
    return s.slot;
  });
  if (order.length !== SOUND_SCALARS.length) {
    throw new Error(`golden song has ${order.length} scalars, lib_sound ${SOUND_SCALARS.length}: regenerate one of them`);
  }
  const col = (name) => header.indexOf(name) - 1;
  const held = HELD.map(([name, fall]) => ({ i: col(name), fall, v: 0 }));
  const onsetI = col('onset'), cutI = col('hardCut');
  let onsets = 0, cuts = 0, last = 0;
  return lines.slice(1).map((line) => {
    const f = line.split('\t').map(Number);
    const t = f[0];
    const vals = f.slice(1);
    const dt = t - last;
    last = t;
    const sound = new Array(SOUND_SCALARS.length);
    order.forEach((slot, i) => { sound[slot] = vals[i]; });
    for (const h of held) h.v = Math.max(h.v - dt / h.fall, vals[h.i] > 0 ? 1 : 0, 0);
    if (vals[onsetI] > 0) onsets = (onsets + 1) % 4096;
    if (vals[cutI] > 0) cuts = (cuts + 1) % 4096;
    return { t, sound, events: held.map((h) => h.v), counts: [onsets, cuts], snd: (name) => vals[col(name)] };
  });
}

// One frame's uniform values: the table, with everything the song moves taken from the song.
function songValues(row) {
  const t = TIME_OFFSET + row.t;
  const beat = row.snd('beatPhase'), bar = row.snd('barPhase');
  return {
    ...SHADER_VALUES,
    uTime: t, uSound: row.sound, uSoundEvents: row.events, uSoundCounts: row.counts,
    uSoundTimeSped: [row.snd('bassTime'), row.snd('midTime'), row.snd('trebleTime')],
    uFlowPhase: 0.5 * t, uFlowTime: 0.5 * t,
    uBeatOsc: 0.5 + 0.5 * Math.cos(6.2831853 * beat), uBarOsc: 0.5 + 0.5 * Math.cos(6.2831853 * bar),
    uKeyHue: row.snd('keyHue'), uHarmony: row.snd('keyClarity'),
    uCamPos: [0, 0, t], uCamTravel: t,
  };
}

async function runStills(args, registry) {
  const song = goldenSong();
  const side = probeGridSide();
  const at = (t) => song[Math.min(song.length - 1, Math.max(0, Math.round(t * 60) - 1))];
  const textures = { ...SHADER_TEXTURES, uSoundTex: 'soundLive' };
  const quadVert = fs.readFileSync(path.join(SHADERS, 'quad_vert.glsl'), 'utf8');
  let ids = args.shaders.length ? args.shaders : shaderIds();
  if (args.shard) {
    const [k, n] = args.shard.split('/').map(Number);
    if (!(n >= 1 && k >= 1 && k <= n)) throw new Error(`bad --shard ${args.shard}: want k/n`);
    ids = ids.filter((_, i) => i % n === k - 1);
  }
  const runRows = song.filter((r) => r.t >= RUN_START - 1e-6 && r.t <= RUN_END + 1e-6);
  const lines = [`stills\t1\t${args.stillSize}\t${args.runSize}\t60\t${side}`];
  const browser = await launch();
  try {
    const page = await browser.openPage(pathToFileURL(path.join(HERE, 'page.html')).href);
    for (const id of ids) {
      const file = path.join(SHADERS, `${id}_frag.glsl`);
      if (!fs.existsSync(file)) throw new Error(`no such fragment style: ${id}`);
      const frag = resolveIncludes(fs.readFileSync(file, 'utf8'), registry);
      const uniforms = parseUniforms(frag);
      const frame = (row, size) => ({
        time: row.t,
        soundGain: Math.min(1.5, Math.max(0, row.snd('vol'))),
        roll: row.t * 7.0,
        uniforms: plan(uniforms, songValues(row), textures, size, size),
      });
      const started = Date.now();
      let r;
      try {
        // A uniform missing from the table is this style's failure (render), not the whole shard's.
        r = await page.call('__stills', {
          vert: quadVert, frag, side, png: !!args.out, stillSize: args.stillSize, runSize: args.runSize,
          stills: STILL_TIMES.map((t) => frame(at(t), args.stillSize)),
          frames: runRows.map((row) => frame(row, args.runSize)),
        });
      } catch (e) {
        r = { error: e.message };
      }
      const ms = Date.now() - started;
      if (r.error) {
        lines.push(`style\t${id}\terror\t${String(r.error).replace(/[\t\n]/g, ' ')}`);
        console.log(`FAIL ${id.padEnd(28)} ${r.error}`);
        continue;
      }
      lines.push(`style\t${id}\tok\t${ms}`);
      for (const s of r.stills) {
        lines.push(`still\t${s.time.toFixed(4)}\t${s.meanLuma.toFixed(4)}\t${s.maxLuma.toFixed(4)}\t${s.fracBlack.toFixed(4)}\t${s.fracBlown.toFixed(4)}`);
        if (args.out && s.png) {
          fs.mkdirSync(args.out, { recursive: true });
          fs.writeFileSync(path.join(args.out, `${id}_t${s.time.toFixed(1)}.png`), Buffer.from(s.png, 'base64'));
        }
      }
      for (const f of r.frames) lines.push(`frame\t${f.time.toFixed(4)}\t${f.grid}`);
      const worst = r.stills.reduce((w, s) => ({ black: Math.max(w.black, s.fracBlack), blown: Math.max(w.blown, s.fracBlown) }), { black: 0, blown: 0 });
      console.log(`ok   ${id.padEnd(28)} black<=${worst.black.toFixed(3)} blown<=${worst.blown.toFixed(3)} ${r.frames.length} frames ${ms} ms` +
        (r.glError ? ` glError ${r.glError}` : ''));
    }
    for (const line of page.drainConsole()) console.log(`  [page] ${line}`);
  } finally {
    await browser.close();
  }
  fs.mkdirSync(path.dirname(path.resolve(args.stills)), { recursive: true });
  fs.writeFileSync(args.stills, `${lines.join('\n')}\n`);
  console.log(`wrote ${ids.length} styles to ${args.stills}`);
}

function parseArgs(argv) {
  const a = {
    size: 240, height: null, shaders: [], all: false, list: false, fluidDisplay: false, out: null, time: null,
    liquid: null, frames: 240, born: 1, bornStyle: null, age: 0, surface: 0, liquidity: 0.35, seed: 1, threeD: false,
    sound: [], set: [], stills: null, shard: null, stillSize: 96, runSize: 32,
  };
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
    else if (v === '--liquid') a.liquid = argv[++i];
    else if (v === '--frames') a.frames = Number(argv[++i]);
    else if (v === '--born') a.born = Number(argv[++i]);
    else if (v === '--born-style') a.bornStyle = argv[++i];
    else if (v === '--age') a.age = Number(argv[++i]);
    else if (v === '--surface') a.surface = Number(argv[++i]);
    else if (v === '--liquidity') a.liquidity = Number(argv[++i]);
    else if (v === '--seed') a.seed = Number(argv[++i]);
    else if (v === '--3d') a.threeD = true;
    else if (v === '--sound') a.sound.push(...argv[++i].split(','));
    else if (v === '--set') a.set.push(argv[++i]);
    else if (v === '--stills') a.stills = argv[++i];
    else if (v === '--shard') a.shard = argv[++i];
    else if (v === '--still-size') a.stillSize = Number(argv[++i]);
    else if (v === '--run-size') a.runSize = Number(argv[++i]);
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
  if (args.stills) {
    await runStills(args, registry);
    return;
  }
  if (args.liquid) {
    const job = liquidJobs(args, registry);
    const browser = await launch();
    try {
      const page = await browser.openPage(pathToFileURL(path.join(HERE, 'page.html')).href);
      const r = await page.call('__liquid', job);
      for (const line of page.drainConsole()) console.log(`  [page] ${line}`);
      if (r.error) {
        console.log(`FAIL liquid ${args.liquid}: ${r.error}`);
        process.exit(1);
      }
      console.log(`ok   liquid ${args.liquid} (${job.frames.length} frames, half-float liquid ${r.half}, glError ${r.glError})`);
      const dir = args.out ?? path.join(HERE, 'out', `liquid_${args.liquid}`);
      fs.mkdirSync(dir, { recursive: true });
      for (const f of r.frames) fs.writeFileSync(path.join(dir, `f${String(f.index).padStart(4, '0')}.png`), Buffer.from(f.png, 'base64'));
      console.log(`     wrote ${r.frames.length} snapshots to ${dir}`);
    } finally {
      await browser.close();
    }
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
    applyOverrides(values, args);
    const uniforms = plan(parseUniforms(frag), values, SHADER_TEXTURES, args.size, args.height ?? args.size);
    const assets = uniforms.some((u) => u.texture && u.texture.startsWith('world_')) ? worldAssets() : undefined;
    jobs.push({ name: id, vert: quadVert, frag, uniforms, assets });
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
