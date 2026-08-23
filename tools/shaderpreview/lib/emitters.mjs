// Port of the FluidEmitters paths MeltField actually uses.
//
// MeltField configures: beatPattern = PATTERN_RING, beatSplats = 2,
// stirrers = 2, sparkle = false, splatRadius = 0.16, and never assigns a
// `choreography`. That leaves exactly two live emitter paths - the ring beat
// splats and the stirrers - plus the virtual-orbit anchor fallback. Suction
// needs a choreography and returns early; sparkle and bassPump are off. So
// the port below is COMPLETE for this scene, not a subset, and that matters:
// the emitters are the second source of dye and a preview that dropped them
// would under-report the very saturation it is being used to measure.
//
// Ported from render/fluid/FluidEmitters.kt.

import { FluidHue } from './palette.mjs';

const BASE_SPEED = 6;
const MAX_SPLATS_PER_FRAME = 16;
const MAX_RADIUS_SWELL = 2;
const BEAT_RESPONSE_GATE = 0.05;

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

export class MeltEmitters {
  constructor() {
    // MeltField's configuration.
    this.beatSplats = 2;
    this.stirrers = 2;
    this.splatRadius = 0.16;
    this.radiusPulse = 0.4;
    this.forceScale = 1;
    this.stirrerSpeed = 1;
    this.beatResponse = 1;
    this.paletteCycleSpeed = 0.5;

    this.beatEnv = 0;
    this.beatEnvRaw = 0;
    this.bassEnv = 0;
    this.stirrerAngle = [0, 1.7, 3.4, 5.1];
    this.stirrerPrevX = [NaN, NaN, NaN, NaN];
    this.stirrerPrevY = [NaN, NaN, NaN, NaN];
    this.activeStirrers = 0;
    this.trebleMean = 0.05;
    this.palettePhase = 0;
    this.suctionPhase = 0;
    this.prevBeat = false;
  }

  /** No choreography: the virtual orbit around the centre. */
  anchor(i, aspect) {
    const ang = this.suctionPhase * 0.4 + i * 2.1;
    return [Math.cos(ang) * 0.45 * Math.min(aspect, 1.4), Math.sin(ang) * 0.45];
  }

  tick(f, dt, aspect, baseHue, hueSpan, motion) {
    const out = [];
    this.beatEnvRaw = Math.max(motion, this.beatEnvRaw * Math.exp(-dt / 0.3));
    this.beatEnv = this.beatEnvRaw * clamp(this.beatResponse, 0, 2);
    const bassTarget = clamp(f.bass * 1.2, 0, 1);
    this.bassEnv += (bassTarget - this.bassEnv)
      * Math.min(1, bassTarget > this.bassEnv ? dt / 0.03 : dt / 0.4);
    this.trebleMean += (f.treble - this.trebleMean) * Math.min(1, dt / 0.32);
    this.palettePhase = (this.palettePhase + dt * this.paletteCycleSpeed * 0.05) % 1;
    this.suctionPhase += dt;

    const radius = this.splatRadius * Math.min(MAX_RADIUS_SWELL, 1 + this.radiusPulse * this.beatEnv);
    const speed = BASE_SPEED * this.forceScale * (0.4 + 1.6 * f.bass) * (0.3 + 0.7 * this.beatEnv);

    const beatEdge = f.beat && !this.prevBeat;
    this.prevBeat = f.beat;

    if (beatEdge && this.beatSplats > 0 && this.beatResponse > BEAT_RESPONSE_GATE) {
      const n = clamp(this.beatSplats, 1, 8);
      const dyeGain = 1.5 * (0.15 + 0.85 * this.beatEnv);
      for (let i = 0; i < n; i++) {
        const frac = i / n;
        const [cr, cg, cb] = FluidHue.rgb((baseHue + this.palettePhase + frac * hueSpan) % 1, 0.9);
        const [ax, ay] = this.anchor(i, aspect);
        const a = frac * 2 * Math.PI + this.palettePhase * 6;
        const ringR = 0.16;
        const x = ax + Math.cos(a) * ringR;
        const y = ay + Math.sin(a) * ringR;
        const tx = -Math.sin(a);
        const ty = Math.cos(a);
        out.push({
          prevX: x - tx * 0.04, prevY: y - ty * 0.04,
          curX: x + tx * 0.04, curY: y + ty * 0.04,
          radius, velX: tx * speed, velY: ty * speed,
          r: cr * dyeGain, g: cg * dyeGain, b: cb * dyeGain,
        });
      }
    }

    const n = clamp(this.stirrers, 0, 4);
    if (n !== this.activeStirrers) {
      for (let i = 0; i < 4; i++) { this.stirrerPrevX[i] = NaN; this.stirrerPrevY[i] = NaN; }
      this.activeStirrers = n;
    }
    if (n > 0) {
      const bands = [f.bass, f.mid, f.treble, f.rms];
      for (let i = 0; i < n; i++) {
        const band = bands[i % bands.length];
        const [cxA, cyA] = this.anchor(i, aspect);
        const orbitR = 0.14 + 0.10 * (i % 3);
        this.stirrerAngle[i] += dt * this.stirrerSpeed * (0.3 + band * 1.7) * (i % 2 === 0 ? 1 : -1);
        const x = cxA + Math.cos(this.stirrerAngle[i]) * orbitR;
        const y = cyA + Math.sin(this.stirrerAngle[i]) * orbitR;
        const px = this.stirrerPrevX[i];
        const py = this.stirrerPrevY[i];
        if (!Number.isNaN(px)) {
          const invDt = 1 / Math.max(dt, 1e-3);
          const [cr, cg, cb] = FluidHue.rgb((baseHue + this.palettePhase + (i * hueSpan) / 4) % 1, 0.85);
          const amp = 0.1 + 0.55 * band;
          out.push({
            prevX: px, prevY: py, curX: x, curY: y, radius,
            velX: (x - px) * invDt * 0.22 * this.forceScale,
            velY: (y - py) * invDt * 0.22 * this.forceScale,
            r: cr * amp, g: cg * amp, b: cb * amp,
          });
        }
        this.stirrerPrevX[i] = x;
        this.stirrerPrevY[i] = y;
      }
    }
    while (out.length > MAX_SPLATS_PER_FRAME) out.pop();
    return out;
  }
}
