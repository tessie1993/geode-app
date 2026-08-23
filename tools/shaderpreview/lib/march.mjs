// The Detail control as a march-step budget.
//
// Mirrors render/scene/MarchBudget.kt, constant for constant. It lived in the former
// hyperspace-math.mjs until that style was removed; the marched FRAGMENT styles
// (vanishing, morphogen, nebula, noneuclid, kifs) are its consumers now, so it gets its
// own file rather than being folded into palette.mjs, which is palette maths.

export const MAX_STEPS = 128;
export const MIN_DETAIL = 0.25;
export const MAX_DETAIL = 1.5;

// Floor rather than 1: below this the surface breaks up into visible banding on the deeper
// styles, which reads as a bug rather than as a quality setting.
const FLOOR_STEPS = 64;

/** `MarchBudget.forDetail(detail).steps`. */
export function marchSteps(detail) {
  const t = Math.min(1, Math.max(0, (detail - MIN_DETAIL) / (MAX_DETAIL - MIN_DETAIL)));
  return Math.round(FLOOR_STEPS + (MAX_STEPS - FLOOR_STEPS) * t);
}
