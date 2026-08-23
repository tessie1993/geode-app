#!/usr/bin/env node
// Headless preview for the app's GPU styles. See README.md - in particular
// the section on what this tool CANNOT tell you.
//
//   node preview.mjs --scene shader --shader julia_frag --audio tone
//   node preview.mjs --list

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { launch } from './lib/cdp.mjs';
import { parseIncludeRegistry, loadShader, parseUniforms, shaderFile } from './lib/glsl.mjs';
import { extractUploadedUniforms, auditUniforms } from './lib/kotlin.mjs';
import { MODELS } from './lib/audio.mjs';
import {
  createShaderSceneDriver, FIELD_FAMILIES,
} from './lib/scenes.mjs';
import { createCompositeDriver } from './lib/composite.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const APP = path.resolve(HERE, '../../app/src/main');
// Both raw roots the build merges into one namespace: the app's, and
const RAW = path.join(APP, 'res/raw');
const ENGINE_RAW = path.resolve(HERE, '../../engine/scenes/src/main/res/raw');
const RAWS = [RAW, ENGINE_RAW];
// The renderer and the scenes live in :engine:scenes, not in :app. They used to
// be under app/src/main/java/dev/geode and every path here pointed there; when
// they moved, this tool stopped starting at all rather than drifting quietly,
// which is the failure mode it was built to have.
const JAVA = path.resolve(HERE, '../../engine/scenes/src/main/kotlin/dev/geode');
const GLUTIL = path.join(JAVA, 'render/scene/GlUtil.kt');

/**
 * The GLSL styles, keyed as `SHADER_SCENES` in SceneCapabilities.kt.
 * Read out of the Kotlin so a style added there shows up here.
 */
function shaderSceneMap() {
  const src = fs.readFileSync(path.join(JAVA, 'render/scene/SceneCapabilities.kt'), 'utf8');
  const block = src.match(/SHADER_SCENES[^=]*=\s*(?:linkedMapOf|mapOf|hashMapOf)\(([\s\S]*?)\n\s*\)/);
  if (!block) throw new Error('could not find SHADER_SCENES in SceneCapabilities.kt');
  const out = new Map();
  const re = /SceneIds\.(\w+)\s+to\s+R\.raw\.(\w+)/g;
  let m;
  while ((m = re.exec(block[1])) !== null) out.set(m[1].toLowerCase(), m[2]);
  return out;
}

function parseArgs(argv) {
  const a = {
    scene: 'shader',
    shader: null,
    style: null,
    frames: 6,
    fps: 60,
    width: 480,
    height: 480,
    audio: 'beat',
    out: null,
    seed: 12345,
    warmup: 0,
    everyFrame: 1,
    clockJump: 0,
    hasMelt: true,
    floatSim: true,
    count: 2048,
    stretchScale: 1,
    stretchMax: 2,
    params: {},
    fieldStats: false,
    composite: false,
    layer: null,
    list: false,
    json: false,
  };
  for (let i = 2; i < argv.length; i++) {
    const k = argv[i];
    const next = () => argv[++i];
    switch (k) {
      case '--scene': a.scene = next(); break;
      case '--shader': a.shader = next(); break;
      case '--style': a.style = next(); break;
      case '--frames': a.frames = Number(next()); break;
      case '--fps': a.fps = Number(next()); break;
      case '--width': a.width = Number(next()); break;
      case '--height': a.height = Number(next()); break;
      case '--size': { const s = Number(next()); a.width = s; a.height = s; break; }
      case '--audio': a.audio = next(); break;
      case '--out': a.out = next(); break;
      case '--seed': a.seed = Number(next()); break;
      case '--warmup': a.warmup = Number(next()); break;
      case '--every': a.everyFrame = Number(next()); break;
      case '--clock-jump': a.clockJump = Number(next()); break;
      case '--no-melt': a.hasMelt = false; break;
      case '--no-float-sim': a.floatSim = false; break;
      case '--count': a.count = Number(next()); break;
      case '--stretch-scale': a.stretchScale = Number(next()); break;
      case '--stretch-max': a.stretchMax = Number(next()); break;
      case '--field-stats': a.fieldStats = true; break;
      case '--composite': a.composite = true; break;
      case '--layer': {
        // "mix,mode" - forces the Layers branch of composite_frag. See
        // createCompositeDriver's `layer` parameter for what this can and
        // cannot tell you.
        const [mix, mode] = next().split(',');
        a.layer = { mix: Number(mix), mode: Number(mode || 0) };
        break;
      }
      case '--list': a.list = true; break;
      case '--json': a.json = true; break;
      case '--param': {
        const [name, value] = next().split('=');
        a.params[name] = value === 'true' ? true : value === 'false' ? false : Number(value);
        break;
      }
      default: throw new Error(`unknown flag ${k}`);
    }
  }
  return a;
}

function makeDriver(args) {
  if (args.scene === 'shader') {
    const map = shaderSceneMap();
    if (!args.shader) throw new Error(`--scene shader needs --shader; try --list`);
    const resource = map.get(args.shader) || (args.shader.endsWith('_frag') ? args.shader : null);
    if (!resource) throw new Error(`unknown shader style '${args.shader}'; try --list`);
    return {
      driver: createShaderSceneDriver({
        params: args.params, width: args.width, height: args.height,
      }),
      fragResource: resource,
      kotlinPath: path.join(JAVA, 'render/scene/ShaderScene.kt'),
      ignoreUploaded: [],
      standIns: [
        'uFlow = 1x1 black, uFlowStrength = 0 (the app\'s state for a scene not wired to the FlowField)',
        'uPalLut = 1x1 black, uPalLutMix = 0 (the app\'s state when the cyclic-palette atlas is absent)',
        'uTouch* = untouched, gesture 0 (there is no pointer here; anything a style draws only under a finger is invisible)',
      ],
      family: 'SHADER',
    };
  }
  const fam = FIELD_FAMILIES[args.scene];
  if (fam) {
    const styleId = args.style || fam.styles[0].id;
    const style = fam.styles.find((s) => s.id === styleId);
    if (!style) {
      throw new Error(
        `unknown ${args.scene} style '${styleId}' (${fam.styles.map((s) => s.id).join(', ')})`,
      );
    }
    const driver = fam.create({
      style, params: args.params, width: args.width, height: args.height,
      // A family whose shaders are SimPass BODIES has to generate its wrapper,
      // and which wrapper depends on the encoding the format plan resolved -
      // the same fork --no-float-sim drives on the target side.
      floatSim: args.floatSim,
    });
    return {
      driver,
      // A field-sim scene is SEVERAL programs; fieldPrograms names them all
      // and the audit below merges every stage's declarations.
      fieldPrograms: driver.fieldPrograms,
      fieldAssemble: driver.fieldAssemble || null,
      fragResource: driver.fieldPrograms.show[1],
      kotlinPath: path.join(JAVA, fam.kotlin),
      ignoreUploaded: [],
      standIns: [...driver.standIns],
      // VisualizerRenderer.compositeFamily(): not ShaderScene, not
      // MilkdropScene, so the else branch - FLUID gates.
      family: 'FLUID',
    };
  }
  throw new Error(
    `unknown scene '${args.scene}' (shader, ${Object.keys(FIELD_FAMILIES).join(', ')})`,
  );
}

async function main() {
  const args = parseArgs(process.argv);
  const registry = parseIncludeRegistry(GLUTIL);

  if (args.list) {
    const map = shaderSceneMap();
    console.log(`includes registered in GlUtil: ${[...registry].join(', ')}`);
    console.log('\nscenes:');
    console.log('  shader --shader <id>       (ShaderScene.kt), one of:');
    for (const [id, res] of map) console.log(`    ${id.padEnd(12)} -> ${res}.glsl`);
    for (const [scene, fam] of Object.entries(FIELD_FAMILIES)) {
      console.log(`  ${scene} --style <id>${' '.repeat(Math.max(1, 13 - scene.length))}(${fam.blurb}), one of:`);
      for (const s of fam.styles) console.log(`    ${s.id.padEnd(20)} ${s.label}`);
    }
    console.log(`\naudio models: ${Object.keys(MODELS).join(', ')}`);
    return;
  }

  const {
    driver, fragResource, kotlinPath, ignoreUploaded, standIns, family,
    vertResource = 'quad_vert',
    echoVertResource = null,
    echoFragResource = null,
    fieldPrograms = null,
    fieldAssemble = null,
  } = makeDriver(args);

  // A field-sim scene (silk/life/acid/myco) is a set of NAMED programs run as
  // a per-frame pass list; a classic scene is one program (plus, for
  // a second program (the echo). Either way every stage's declarations feed the same
  // three-way audit.
  // A family may ASSEMBLE rather than merely load: SilkScene's two .glsl files
  // are SimPass bodies, and the `#version`, the state sampler, the decode
  // helpers and the main() around each are generated by SimGlsl.kt in the app
  // and by lib/simglsl.mjs here. The assembled text is what gets compiled AND
  // what the uniform audit reads, so a uniform the wrapper declares is audited
  // exactly like one the file declares.
  const assemble = fieldAssemble || ((_name, srcs) => srcs);
  const fieldShaders = fieldPrograms
    ? Object.fromEntries(Object.entries(fieldPrograms).map(([name, [v, fr]]) => [
      name,
      assemble(name, { vertSrc: loadShader(RAWS, v, registry), fragSrc: loadShader(RAWS, fr, registry) }),
    ]))
    : null;
  const vertSrc = fieldShaders ? null : loadShader(RAWS, vertResource, registry);
  const fragSrc = fieldShaders ? null : loadShader(RAWS, fragResource, registry);
  const echoShaders = echoVertResource
    ? {
      vert: loadShader(RAWS, echoVertResource, registry),
      frag: loadShader(RAWS, echoFragResource, registry),
    }
    : null;
  // Uniforms from EVERY stage the scene's Kotlin uploads to. quad_vert
  // declares none, so this is a no-op for every fullscreen style;
  // a vertex stage may declare part of the contract, and an echo pass is a
  // second program whose six would otherwise be audited against nothing.
  // A field sim contributes every one of its programs' stages (the deposit
  // pass declares in its OWN vertex shader).
  const declared = (() => {
    const byName = new Map();
    const stages = fieldShaders
      ? Object.values(fieldShaders).flatMap(
        (s) => [...parseUniforms(s.vertSrc), ...parseUniforms(s.fragSrc)],
      )
      : [
        ...parseUniforms(vertSrc), ...parseUniforms(fragSrc),
        ...(echoShaders ? [...parseUniforms(echoShaders.vert), ...parseUniforms(echoShaders.frag)] : []),
      ];
    for (const d of stages) {
      if (!byName.has(d.name)) byName.set(d.name, d);
    }
    return [...byName.values()];
  })();
  const uploaded = extractUploadedUniforms(kotlinPath);
  const audit = auditUniforms({
    sceneId: args.scene,
    declared,
    uploaded,
    supplied: driver.supplies,
    ignoreUploaded,
  });

  // The composite pass gets the SAME three-way audit as a scene: its shader is
  // composite_frag, its Kotlin is CompositePass's loc() block (it was
  // VisualizerRenderer's cLoc() block until the pass was extracted), and a
  // uniform any of the three is missing would be a silent zero in the pass the
  // user actually looks at.
  let compositeDriver = null;
  let compositeShaders = null;
  let compositeCounts = null;
  if (args.composite) {
    compositeDriver = createCompositeDriver({
      params: args.params, family, width: args.width, height: args.height,
      layer: args.layer,
    });
    compositeShaders = {
      vert: loadShader(RAWS, 'fade_vert', registry),
      frag: loadShader(RAWS, 'composite_frag', registry),
    };
    const compositeDeclared = parseUniforms(compositeShaders.frag);
    const compositeUploaded = extractUploadedUniforms(path.join(JAVA, 'render/CompositePass.kt'));
    const compositeAudit = auditUniforms({
      sceneId: 'composite',
      declared: compositeDeclared,
      uploaded: compositeUploaded,
      supplied: compositeDriver.supplies,
      // Uploaded by the renderer, but to ANOTHER program: the crossfade has a
      // shader of its own and is not the composite.
      ignoreUploaded: ['uFadeAlpha'],
    });
    compositeCounts = {
      shaderDeclares: compositeDeclared.length,
      kotlinUploads: compositeUploaded.size - 1,
      harnessSupplies: compositeDriver.supplies.size,
    };
    audit.errors.push(...compositeAudit.errors);
    audit.notes.push(...compositeAudit.notes);
    standIns.push(...compositeDriver.standIns);
  }

  const report = {
    scene: args.scene,
    style: fieldPrograms ? (args.style || FIELD_FAMILIES[args.scene].styles[0].id) : (args.shader || null),
    composite: args.composite ? `composite_frag.glsl, gate ${family}` : null,
    shader: fieldPrograms
      ? Object.entries(fieldPrograms).map(([name, [, fr]]) => `${name}=${fr}.glsl`).join(' ')
      : `${fragResource}.glsl`,
    kotlin: path.relative(path.resolve(HERE, '../..'), kotlinPath),
    includesResolved: (fieldPrograms
      ? Object.values(fieldPrograms).flatMap(([v, fr]) => [v, fr])
      : [fragResource]
    ).flatMap((res) => fs.readFileSync(shaderFile(RAWS, res), 'utf8')
      .match(/^[ \t]*\/\/#include[ \t]+(\w+)[ \t]*$/gm) || []).map((s) => s.trim()),
    uniformAudit: {
      shaderDeclares: declared.length,
      kotlinUploads: uploaded.size,
      harnessSupplies: driver.supplies.size,
      errors: audit.errors,
      notes: audit.notes,
      standIns,
      composite: compositeCounts,
    },
    frames: [],
  };

  if (audit.errors.length) {
    console.error('UNIFORM AUDIT FAILED - the render below would not be trustworthy:\n');
    for (const e of audit.errors) console.error('  ' + e);
    console.error('\nFix lib/scenes.mjs (or the app) before believing any image from this run.');
    process.exitCode = 2;
    return;
  }

  const meltShaders = false
    ? {
      vert: loadShader(RAWS, 'fluid_base_vert', registry),
      frags: {
        splat: loadShader(RAWS, 'fluid_splat_frag', registry),
        advect: loadShader(RAWS, 'fluid_advect_frag', registry),
        curl: loadShader(RAWS, 'fluid_curl_frag', registry),
        vorticity: loadShader(RAWS, 'fluid_vorticity_frag', registry),
        divergence: loadShader(RAWS, 'fluid_divergence_frag', registry),
        pressure: loadShader(RAWS, 'fluid_pressure_frag', registry),
        gradient: loadShader(RAWS, 'fluid_gradient_frag', registry),
        clear: loadShader(RAWS, 'fluid_clear_frag', registry),
      },
    }
    : null;

  const model = MODELS[args.audio];
  if (!model) throw new Error(`unknown audio model '${args.audio}' (${Object.keys(MODELS).join(', ')})`);
  const audioAt = model();

  const outDir = args.out ? path.resolve(process.cwd(), args.out) : null;
  if (outDir) fs.mkdirSync(outDir, { recursive: true });

  const browser = await launch();
  try {
    const page = await browser.openPage(pathToFileURL(path.join(HERE, 'page/harness.html')).href);
    const init = await page.call('__init', {
      width: args.width,
      height: args.height,
      vertSrc,
      fragSrc,
      meltConfig: driver.meltConfig,
      meltShaders,
      compositeShaders,
      particles: driver.particleConfig || null,
      echoShaders,
      fieldSim: fieldShaders ? { programs: fieldShaders, targets: driver.fieldTargets, forceByte: !args.floatSim } : null,
    });
    if (!init.ok) {
      console.error('harness init failed: ' + init.error);
      for (const l of page.drainConsole()) console.error('  ' + l);
      process.exitCode = 3;
      return;
    }
    // The page reports the format each ping-pong target actually got (the
    // FluidBuffers-style probe may have fallen back); drivers that change
    // their uploads on a fallback - MycoScene's byte-trail deposit rescale -
    // read it here.
    if (driver.onInit) driver.onInit(init);
    report.gl = {
      browser: browser.version,
      renderer: init.unmaskedRenderer,
      glsl: init.glsl,
      colorBufferFloat: init.colorBufferFloat,
      melt: init.melt,
      fieldSim: init.fieldSim || null,
    };
    // A declared uniform the linker dropped is not a harness bug; surface it
    // as a shader observation instead.
    const inactive = declared.map((d) => d.name).filter((n) => !init.activeUniforms.includes(n));
    if (compositeShaders) {
      const kept = init.compositeActiveUniforms || [];
      inactive.push(
        ...parseUniforms(compositeShaders.frag).map((d) => d.name).filter((n) => !kept.includes(n)),
      );
    }
    if (inactive.length) report.uniformAudit.deadStrippedByLinker = inactive;

    const dt = 1 / args.fps;
    let simTime = 0;
    let captureIndex = 0;
    const totalSteps = args.warmup + args.frames * args.everyFrame;

    for (let i = 0; i < totalSteps; i++) {
      const f = audioAt(simTime);
      // Clock jumping. Every step is still a NORMAL 1/fps frame - the app
      // clamps its own dt to 1/15 s, so a 60-second dt is a state the app can
      // never be in and a picture drawn from one would be a fiction. What the
      // jump moves is the free-running clocks only (see driver.jumpClock), so
      // an hour of wallpaper uptime costs one step instead of 216 000 and the
      // thing being tested - float precision in uTime - is the thing that
      // actually got an hour older.
      const plan = driver.step(f, dt);
      // After the scene's step, as in the app: the composite grades the frame
      // the scene has just drawn, at the same simulated instant.
      const compositePlan = compositeDriver ? compositeDriver.step(f, dt, simTime + dt) : null;
      simTime += dt;
      // Warm-up runs at real time so the scene is populated before the clock
      // is thrown forward; jumping from a cold start only ever measures an
      // empty scene at a large t.
      if (args.clockJump > 0 && i >= args.warmup && driver.jumpClock) {
        driver.jumpClock(args.clockJump);
        if (compositeDriver) compositeDriver.jumpClock(args.clockJump);
        simTime += args.clockJump;
      }
      const isWarmup = i < args.warmup;
      const isCapture = !isWarmup && (i - args.warmup) % args.everyFrame === 0;
      const res = await page.call('__frame', {
        uniforms: plan.uniforms,
        composite: compositePlan,
        melt: plan.melt,
        audioTex: plan.audioTex,
        particles: plan.particles || null,
        echo: plan.echo || null,
        passes: plan.passes || null,
        probe: plan.probe || null,
        capture: isCapture && !!outDir,
        wantFieldStats: isCapture && args.fieldStats,
        skipRender: !isCapture,
      });
      if (!res.ok) throw new Error('frame failed: ' + res.error);
      // The probe channel (LifeScene's census texel) flows back on EVERY
      // frame, captured or not - the census cadence must not depend on
      // --every.
      if (driver.feedback && res.probe) driver.feedback(res.probe);
      if (!isCapture) continue;

      const entry = {
        index: captureIndex,
        simTime: Math.round(simTime * 1000) / 1000,
        ...res.metrics,
        ...plan.debug,
        ...(compositeDriver ? compositeDriver.debug() : {}),
      };
      if (outDir && res.png) {
        const file = path.join(outDir, `frame_${String(captureIndex).padStart(3, '0')}.png`);
        fs.writeFileSync(file, Buffer.from(res.png.split(',')[1], 'base64'));
        entry.png = file;
      }
      report.frames.push(entry);
      captureIndex++;
    }

    report.console = page.drainConsole();
  } finally {
    await browser.close();
  }

  if (args.json) {
    console.log(JSON.stringify(report, null, 2));
  } else {
    printReport(report);
  }
  if (outDir) {
    fs.writeFileSync(path.join(outDir, 'report.json'), JSON.stringify(report, null, 2));
  }
}

function printReport(r) {
  console.log(`scene      ${r.scene}`);
  if (r.style) console.log(`style      ${r.style}`);
  console.log(`shader     ${r.shader}${r.includesResolved.length ? `  (${r.includesResolved.join(', ')})` : ''}`);
  if (r.composite) console.log(`composite  ${r.composite}`);
  console.log(`kotlin     ${r.kotlin}`);
  console.log(`gl         ${r.gl.renderer}`);
  console.log(`           ${r.gl.glsl}, EXT_color_buffer_float=${r.gl.colorBufferFloat}`);
  if (r.gl.melt) console.log(`melt       ${JSON.stringify(r.gl.melt)}`);
  if (r.gl.fieldSim) {
    for (const [name, t] of Object.entries(r.gl.fieldSim.targets)) {
      const fb = t.format === t.requested ? '' : ` (requested ${t.requested})`;
      console.log(`target     ${name}: ${t.width}x${t.height} ${t.format}${fb} ${t.filter}`);
    }
  }
  const a = r.uniformAudit;
  console.log(`uniforms   shader declares ${a.shaderDeclares}, kotlin uploads ${a.kotlinUploads}, harness supplies ${a.harnessSupplies} - AUDIT OK`);
  if (a.composite) {
    const c = a.composite;
    console.log(`           composite declares ${c.shaderDeclares}, renderer uploads ${c.kotlinUploads}, harness supplies ${c.harnessSupplies} - AUDIT OK`);
  }
  for (const s of a.standIns) console.log(`  stand-in: ${s}`);
  for (const n of a.notes) console.log(`  note: ${n}`);
  if (a.deadStrippedByLinker) console.log(`  dead-stripped by linker: ${a.deadStrippedByLinker.join(', ')}`);
  console.log('');
  const cols = ['t', 'meanLuma', 'maxLuma', 'blown>0.95', 'black<0.02', 'dMeanLuma'];
  console.log(cols.map((c, i) => c.padStart(i === 0 ? 8 : 11)).join(''));
  for (const f of r.frames) {
    console.log(
      String(f.simTime).padStart(8)
      + String(f.meanLuma).padStart(11)
      + String(f.maxLuma).padStart(11)
      + String(f.fracBlownOut).padStart(11)
      + String(f.fracBlack).padStart(11)
      + String(f.deltaMeanLuma === null ? '-' : f.deltaMeanLuma).padStart(11)
      + (f.dye ? `   dye max ${f.dye.max} mean ${f.dye.mean}` : '')
      + (f.actName ? `   ${f.actName} n=${f.bloomCount}` : '')
      + (f.postRotation === undefined ? '' : `   postRot ${f.postRotation}`),
    );
  }
  if (r.frames.some((f) => f.png)) {
    console.log(`\npngs: ${path.dirname(r.frames.find((f) => f.png).png)}`);
  }
  if (r.console && r.console.length) {
    console.log('\npage console:');
    for (const l of r.console) console.log('  ' + l);
  }
}

main().catch((e) => {
  console.error(e.stack || String(e));
  process.exitCode = 1;
});
