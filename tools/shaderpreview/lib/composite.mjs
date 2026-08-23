// The composite pass - render/VisualizerRenderer.kt's `composite_frag` upload.
//
// Everything else in this tool renders a SCENE, which is what you want when
// you are debugging one. But roughly half of what the user actually sees on a
// fluid-family style is applied afterwards: `CompositeGrade.gateFor(FLUID)`
// hands the composite the whole zoom/rotation/colour-grade block, so on those
// styles Zoom and Rotation exist NOWHERE in the scene's own output. A harness
// that stops at the scene cannot tell "the control does nothing" from "the
// control is applied one pass later", and those are opposite findings.
//
// So this mirrors the non-transition composite draw: `uStyle = CUT`, one
// scene texture, both gates from the active scene's family. Transitions are
// out of scope on purpose - two live scenes would need two drivers, and the
// gate algebra a transition exercises is already pinned by CompositeGradeTest.
//
// Values are computed here exactly as VisualizerRenderer computes them, with
// the integrators from CompositeGrade.kt, and audited against the Kotlin the
// same three ways as every scene driver.

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** TransitionStyle.CUT.ordinal - what uStyle is outside a transition. */
const STYLE_CUT = 0;

/** VisualizerRenderer.STYLE_LAYER - the Layers branch. */
const STYLE_LAYER = 6;

/** CompositeGrade: rotation is a speed, so the pass integrates its own angle. */
const TAU = 6.2831855;

/** CompositeGrade.BEAT_DECAY, per second. */
const BEAT_DECAY = 3;

/** VisualSafety.DEFAULT_STROBE_HZ, with Safe visuals off. */
const DEFAULT_STROBE_HZ = 9;

/**
 * The `SceneParams` fields the composite reads, at their `SceneParams`
 * defaults. Every one is a neutral value, so an unspecified composite run is
 * the same picture as a scene-only run put through an identity grade - which
 * is what makes `--composite` safe to leave on.
 */
export const COMPOSITE_DEFAULTS = {
  zoom: 1, rotation: 0, saturation: 1, brightness: 1, contrast: 1, gamma: 1,
  colorShift: 0, intensity: 1, colorCycle: false, cycleSpeed: 0.1,
  pulse: 0, beatResponse: 1,
  warp: 0, ripple: 0, symmetry: 6, kaleidoscope: false, pixelate: 0, tile: 1,
  twist: 0, bloom: 0, posterize: 0, mirror: false, invert: false,
  driftX: 0, driftY: 0, sway: 0, shake: 0, flash: 0, temperature: 0,
  solarize: false,
  chromaAb: 0, vignette: 0, scanlines: 0, grain: 0, glitch: 0, fisheye: 0,
  strobe: 0,
};

/**
 * The per-texture gate, `CompositeGrade.gateFor`. Kept as the four booleans
 * rather than a table of vec4s so a change to the Kotlin reads as the same
 * change here.
 */
export function gateFor(family) {
  return [
    family !== 'SHADER' ? 1 : 0,
    family === 'PARTICLE' || family === 'FLUID' ? 1 : 0,
    family === 'FLUID' ? 1 : 0,
    family === 'MILKDROP' || family === 'FLUID' ? 1 : 0,
  ];
}

/**
 * @param family one of CompositeGrade.SceneFamily's names; decides both gates.
 */
/**
 * @param layer  when set, forces the uStyle 6 (Layers) branch with the given
 *   `{ mix, mode }`. The harness binds the SAME scene texture to uTexA and
 *   uTexB, so this does not preview a real two-scene composite - what it does
 *   check is the blend algebra, which for b == a has a known answer per mode
 *   (DIFFERENCE must go black, MULTIPLY must darken, SCREEN must brighten).
 *   That is worth having: the blend functions are the only new maths in the
 *   Layers path and nothing else off-device can execute them.
 */
export function createCompositeDriver({ params, family, width, height, layer = null }) {
  const p = { ...COMPOSITE_DEFAULTS, ...params };
  const gate = gateFor(family);
  let rotationAngle = 0;
  let cyclePhase = 0;
  let beatPulse = 0;

  const supplies = new Set([
    'uTexA', 'uTexB', 'uNoise', 'uDither', 'uRatio', 'uProgress', 'uStyle',
    // Layers. Supplied at their inactive values (see standIns): uStyle is CUT
    // here, so the shader never reads them - but the app uploads them on EVERY
    // frame, so omitting them would be exactly the "harness drifted behind the
    // app" case the audit exists to catch.
    'uLayerMix', 'uBlendMode',
    'uTime', 'uBeat', 'uChroma', 'uVignette', 'uScanline', 'uGrain', 'uGlitch',
    'uFisheye', 'uStrobe', 'uStrobeHz', 'uGateA', 'uGateB',
    'uPostWarp', 'uPostRipple', 'uPostSymmetry', 'uPostKaleido', 'uPostPixelate',
    'uPostTile', 'uPostTwist', 'uPostMirror', 'uPostBloom', 'uPostPosterize',
    'uPostInvert', 'uPostDriftX', 'uPostDriftY', 'uPostSway', 'uPostShake',
    'uPostFlash', 'uPostTemp', 'uPostSolarize', 'uPostZoom', 'uPostRotation',
    'uPostSat', 'uPostBright', 'uPostContrast', 'uPostGamma', 'uPostHue',
    'uPostPulse',
    // Conditional in the app, supplied here as the state the app itself sends
    // when they are absent - see the stand-ins the CLI prints.
    'uFlow', 'uFlowStrength', 'uRipple', 'uRippleTexel', 'uRippleStrength',
    'uRippleSpecular',
  ]);

  /** What the CLI must report rather than let the reader assume. */
  const standIns = [
    'uNoise = 1x1 black, uDither = 0 (the app\'s state when the blue-noise tile could not be read)',
    'uFlow = 1x1 black, uFlowStrength = 0 (fluidWarp off)',
    'uRipple = 1x1 black, uRippleStrength/Specular = 0 (the F2 overlay off)',
    'uStyle = CUT, uProgress = 0, uTexB = uTexA (no transition is in flight)',
    'uLayerMix = 0, uBlendMode = 0 (Layers off; the uStyle 6 branch is not modelled - '
      + 'two live scenes would need two scene drivers, like transitions)',
  ];

  function step(f, dt, timeSeconds) {
    rotationAngle = (rotationAngle + p.rotation * dt) % TAU;
    if (p.colorCycle) cyclePhase = (cyclePhase + p.cycleSpeed * dt) % 1;
    const beat = !f.beat ? 0 : (f.beatStrength > 0 ? f.beatStrength : 1);
    beatPulse = Math.max(0, Math.max(beat, beatPulse - dt * BEAT_DECAY));
    // CompositeGrade.pulseAmount: the slider times the SQUARED envelope.
    const env = clamp(beatPulse, 0, 1);
    const pulseAmount = clamp(p.pulse, 0, 1) * env * env;

    const u = (name, v) => ({ [name]: { t: '1f', v } });
    return Object.assign(
      {},
      {
        uTexA: { t: 'tex', v: 0 },
        uTexB: { t: 'tex', v: 1 },
        uFlow: { t: 'tex', v: 2 },
        uRipple: { t: 'tex', v: 3 },
        uNoise: { t: 'tex', v: 4 },
        uStyle: { t: '1i', v: layer ? STYLE_LAYER : STYLE_CUT },
        // BlendMode.NORMAL; unread at uStyle = CUT, uploaded regardless.
        uBlendMode: { t: '1i', v: layer ? layer.mode : 0 },
        uRippleTexel: { t: '2f', v: [0, 0] },
        uGateA: { t: '4fv', v: gate },
        uGateB: { t: '4fv', v: gate },
      },
      u('uDither', 0),
      u('uRatio', width / Math.max(height, 1)),
      u('uProgress', 0),
      u('uLayerMix', layer ? layer.mix : 0),
      u('uTime', timeSeconds),
      // uBeat is the frame's raw beat impulse in the app, not the envelope.
      u('uBeat', beat),
      u('uChroma', p.chromaAb),
      u('uVignette', p.vignette),
      u('uScanline', p.scanlines),
      u('uGrain', p.grain),
      u('uGlitch', p.glitch),
      u('uFisheye', p.fisheye),
      u('uStrobe', p.strobe),
      u('uStrobeHz', DEFAULT_STROBE_HZ),
      u('uPostWarp', p.warp),
      u('uPostRipple', p.ripple),
      u('uPostSymmetry', p.symmetry),
      u('uPostKaleido', p.kaleidoscope ? 1 : 0),
      u('uPostPixelate', p.pixelate),
      u('uPostTile', p.tile),
      u('uPostTwist', p.twist),
      u('uPostMirror', p.mirror ? 1 : 0),
      u('uPostBloom', p.bloom),
      u('uPostPosterize', p.posterize),
      u('uPostInvert', p.invert ? 1 : 0),
      u('uPostDriftX', p.driftX),
      u('uPostDriftY', p.driftY),
      u('uPostSway', p.sway),
      u('uPostShake', p.shake),
      u('uPostFlash', p.flash),
      u('uPostTemp', p.temperature),
      u('uPostSolarize', p.solarize ? 1 : 0),
      u('uPostZoom', p.zoom),
      u('uPostRotation', rotationAngle),
      u('uPostSat', p.saturation),
      // CompositeGrade.brightness: brightness and intensity are one factor.
      u('uPostBright', p.brightness * p.intensity),
      u('uPostContrast', p.contrast),
      u('uPostGamma', p.gamma),
      u('uPostHue', p.colorShift + cyclePhase),
      u('uPostPulse', pulseAmount),
      u('uFlowStrength', 0),
      u('uRippleStrength', 0),
      u('uRippleSpecular', 0),
    );
  }

  /** See the scene drivers' jumpClock: free-running clocks only. */
  function jumpClock(seconds) {
    rotationAngle = (rotationAngle + p.rotation * seconds) % TAU;
    if (p.colorCycle) cyclePhase = (cyclePhase + p.cycleSpeed * seconds) % 1;
  }

  return {
    supplies,
    standIns,
    step,
    jumpClock,
    /** Reported per frame: the integrated angle is the thing a rotation bug
     *  is about, and it is invisible in a luminance statistic. */
    debug: () => ({ postRotation: Math.round(rotationAngle * 1e4) / 1e4 }),
  };
}
