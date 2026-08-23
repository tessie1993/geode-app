// Palette maths shared by every scene driver.
//
// Mirrors:
//   render/fluid/FluidHue.kt         - hsv
//
// Lifted out of the former hyperspace-math.mjs when that style was removed:
// FluidHue was never Hyperspace-specific, and the silk, life, acid, myco and
// emitter drivers all read it.

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

export const FluidHue = {
  MIN_HUE_RANGE: 0.1,
  MAX_HUE_RANGE: 1.5,
  wrap01(h) { const x = h - Math.floor(h); return x >= 1 ? 0 : x; },
  base(paletteBase) { return FluidHue.wrap01(paletteBase); },
  range(hueRange) { return clamp(hueRange, FluidHue.MIN_HUE_RANGE, FluidHue.MAX_HUE_RANGE); },
  span(hueRange, paletteRange) { return FluidHue.range(hueRange) * clamp(paletteRange, 0, 1); },
  rgb(hue, saturation) {
    const h = FluidHue.wrap01(hue);
    const s = clamp(saturation, 0, 1);
    const v = 1;
    const sextant = h * 6;
    const i = Math.trunc(sextant) % 6;
    const fr = sextant - Math.trunc(sextant);
    const p = v * (1 - s), q = v * (1 - fr * s), t = v * (1 - (1 - fr) * s);
    switch (i) {
      case 0: return [v, t, p];
      case 1: return [q, v, p];
      case 2: return [p, v, t];
      case 3: return [p, q, v];
      case 4: return [t, p, v];
      default: return [v, p, q];
    }
  },
};
