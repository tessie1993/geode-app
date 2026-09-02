// Reading the fingers, in the units a fragment style already thinks in.
//
// Include AFTER lib_scene_uniforms - it declares the uniforms and
// TOUCH_MAX_POINTS that everything here reads.
//
// ---- the one guarantee ----------------------------------------------------
//
// With nothing touched and nothing fading, every function here returns exactly
// 0 (or the identity, for touchWarp3). Not "almost zero": the idle test is an
// equality on values the CPU writes as literal zeros, so a style that calls
// these on an untouched frame renders bit-identically to one that does not
// call them at all. That is what makes it safe to wire touch into a style that
// people are already using.
//
// ---- gesture vs. count, and why idle is not `uTouchGesture == 0` ----------
//
// TouchField sets uTouchGesture from the LIVE finger count, so it drops to 0
// on the frame the last finger lifts - while uTouchPoints and uTouchAnchor.z
// keep decaying for RELEASE_TAU_SECONDS (0.55 s) afterwards. That decay is the
// entire point of the release wake: a finger that vanished on the frame it
// left would pop, and a style would have no way to draw the gesture ending.
// Gating on uTouchGesture alone would delete it. So idle here means "no live
// gesture AND nothing left fading", which is the state an untouched frame is
// actually in.

/** True when there is no live gesture and nothing still fading. */
bool touchIdle() {
    return uTouchGesture == 0 && uTouchCount == 0 && uTouchAnchor.z <= 0.0;
}

/**
 * The anchor in the SAME units view() returns: y-up, origin at centre, x
 * scaled by the aspect ratio so a circle drawn around it is round.
 *
 * It is deliberately NOT pushed through the rest of view()'s stack. Drift,
 * zoom, rotation, twist, tiling and warp transform the style's DOMAIN, and a
 * style that folds its domain wants the finger in the folded frame, unfolded
 * frame or both depending on what the fold means. Apply whichever of those you
 * applied to uv, in the same order, and the anchor tracks the finger through
 * your own geometry.
 */
vec2 touchAnchor() {
    if (touchIdle()) return vec2(0.0);
    return vec2(uTouchAnchor.x * uResolution.x / uResolution.y, uTouchAnchor.y);
}

/** Anchor strength, 0..1: 1 while a finger is down, decaying after release. */
float touchStrength() {
    if (touchIdle()) return 0.0;
    return uTouchAnchor.z;
}

/**
 * Influence of the anchor at p, falling from touchStrength() at the anchor to
 * 0 far from it. p is in view() units.
 *
 * A Gaussian, exp(-d*d) with d in units of `radius`, rather than a
 * smoothstep. Two reasons, both visible on screen:
 *
 * - it has no support edge. A smoothstep falls to exactly 0 at a fixed radius,
 *   and a finger dragging that boundary across a textured field draws a
 *   travelling circle the user can see. A Gaussian never reaches zero, so
 *   there is no circle to see.
 * - its derivative is continuous everywhere, including at the peak. Anything
 *   built on the gradient of this - a warp, a normal, a flow - creases at a
 *   smoothstep's endpoints and does not crease here.
 *
 * At d = radius the influence is 0.37 of peak, and at 2*radius it is 0.018, so
 * `radius` reads as "the size of the effect" even though the tail is infinite.
 */
float touchFalloff(vec2 p, float radius) {
    if (touchIdle()) return 0.0;
    float r = max(radius, 1e-3);
    vec2 d = (p - touchAnchor()) / r;
    return uTouchAnchor.z * exp(-dot(d, d));
}

/** Base radius of a wake blob, in view() units, at the moment of release. */
#define TOUCH_WAKE_RADIUS 0.35
/** How fast a released blob spreads, in view() units per second of age. */
#define TOUCH_WAKE_SPREAD 0.55

/**
 * Summed influence of every slot, live and fading, so a lifted finger leaves a
 * trail behind rather than switching off.
 *
 * Unbounded above: five fingers in one place sum to five. That is deliberate -
 * a style decides whether to clamp, tone-map or let it blow out, and summing
 * lets it tell one finger from five, which a max() would not.
 *
 * A fading blob SPREADS as it fades (TOUCH_WAKE_SPREAD * age) instead of only
 * dimming. Dimming alone reads as a dot being turned down; spreading reads as
 * something dissipating, which is what a wake is.
 *
 * The loop bound is the compile-time TOUCH_MAX_POINTS and the break is on the
 * uniform uTouchCount, so an untouched frame exits on the first iteration.
 */
float touchWake(vec2 p) {
    if (touchIdle()) return 0.0;
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    float sum = 0.0;
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 t = uTouchPoints[i];
        if (t.z <= 0.0) continue;
        float r = TOUCH_WAKE_RADIUS + TOUCH_WAKE_SPREAD * t.w;
        vec2 d = (p - vec2(t.x * aspect, t.y)) / r;
        sum += t.z * exp(-dot(d, d));
    }
    return sum;
}

/** Peak inward pull, as a fraction of the offset from the anchor's line. */
#define TOUCH_WARP_GAIN 0.45
/** Radius of the pull, in view() units. */
#define TOUCH_WARP_RADIUS 0.7
/**
 * max over u >= 0 of exp(-u) * (2u - 1), attained at u = 1.5.
 *
 * This is the peak of the radial derivative of the pull: for the Gaussian
 * profile g(r) = G * exp(-r^2/R^2) the map r -> r * (1 - g(r)) has derivative
 * 1 + G * exp(-u) * (2u - 1) with u = r^2/R^2, so the Jacobian norm is bounded
 * by 1 + 0.4463 * G. It is written out because that bound is the ONLY thing
 * keeping the warp marchable, and a future change to the profile changes it.
 */
#define TOUCH_WARP_PEAK 0.4463

/**
 * Pull a 3D point toward the anchor's world ray.
 *
 * FRAME: p must be in a camera frame whose x and y are the screen axes in
 * view() units and whose z runs along the view direction - the frame a style
 * is already in when it builds `rd = normalize(vec3(uv, focal))`. The anchor's
 * world ray is then the line through (touchAnchor(), 0) parallel to +z, and
 * this contracts space toward that line, strongest at the anchor and falling
 * off over TOUCH_WARP_RADIUS. Depth is untouched, so the pull looks the same
 * near and far and the effect reads as bending the SCENE rather than the
 * camera.
 *
 * MARCHING: this is a non-rigid deform, so it breaks the raymarch's
 * never-overestimate invariant. Divide your distance estimate by
 * touchWarpLipschitz() - the same correction hyperspace_frag applies with
 * uLipschitz. TOUCH_WARP_GAIN is held below 1 so the map never folds (the
 * radial factor 1 - g stays positive); at 0.45 the correction is at most 1.20,
 * i.e. a fifth of the frame's steps, which is why the gain is a fifth of the
 * way to a fold and not nine tenths.
 */
vec3 touchWarp3(vec3 p) {
    if (touchIdle()) return p;
    vec2 d = p.xy - touchAnchor();
    float g = uTouchAnchor.z * TOUCH_WARP_GAIN
        * exp(-dot(d, d) / (TOUCH_WARP_RADIUS * TOUCH_WARP_RADIUS));
    return vec3(p.xy - d * g, p.z);
}

/**
 * Upper bound on touchWarp3's Jacobian norm - divide a distance estimate
 * measured in warped space by this before stepping the ray.
 *
 * Scaled by the live strength rather than pinned at the worst case, so an
 * untouched frame divides by exactly 1 and marches at full speed.
 */
float touchWarpLipschitz() {
    if (touchIdle()) return 1.0;
    return 1.0 + TOUCH_WARP_PEAK * TOUCH_WARP_GAIN * uTouchAnchor.z;
}
