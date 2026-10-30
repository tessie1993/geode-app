// The Arbor family's branching distance fields: fractal trees as an iterated
// fold, in 3D for the marched styles and in 2D for the flat ones.
//
// Include after lib_scene_uniforms and lib_sdf3 (it calls rot2() and hash11()).
// The tree functions read no uniform at all. The macro helpers at the bottom
// read uSteps, uMorph, uTurbulence and uWarp, each only inside the helper that
// needs it, so a style that never calls one leaves that uniform to the linker.
//
// ---- the Customize macros --------------------------------------------------------
//
// Every Arbor style answers the same six macros through standard scene params,
// neutral at their defaults: Speed scales animation time (uTime * uSpeed),
// Complexity (uMorph) adds fold levels within the march budget (arbLevels),
// Chaos (uTurbulence + uWarp, arbChaos) adds per-branch jitter and sway noise
// and uTwist a spiral twist, Colour is pal(), Energy (uIntensity) scales the
// in-shader audio amounts, Space is the camera rig or view()'s zoom. They live
// here so "Complexity" adds levels the same way in all twelve.
//
// ---- why a fold, not a recursion ---------------------------------------------
//
// A tree of N levels with k children per fork has k^N limbs. Evaluating every
// limb per pixel (per march step!) is out of the question past level 4. GLSL
// has no recursion anyway. The trick is that a fork is a SYMMETRY: every child
// subtree is the same shape as every other, placed by a rigid motion and a
// uniform scale. So instead of visiting all children, fold the point into ONE
// child's frame (mirror for two arms, a polar wedge for three or more), move it
// to that child's base, turn it onto the child's axis and scale it up. After N
// such steps the point has walked down the one path of the tree it can be
// nearest to, and only N + 1 limbs were measured. The cost is linear in depth,
// not exponential.
//
// Every step is a reflection or rotation (1-Lipschitz) plus a uniform scale,
// and each limb's local distance is multiplied back by the accumulated scale,
// so the estimate stays a true lower bound almost everywhere. "Almost": a twig
// that a twist swings across a fold plane is invisible from the far side of
// that plane, so marched styles step at 0.7-0.85 of the estimate to hide the
// rare overshoot rather than paying for the neighbour's subtree.
//
// ---- what the walk leaves behind ----------------------------------------------
//
// The fold also tells you WHERE on the tree a point is, for free. arborTree()
// and friends write four globals that a style reads right after the call that
// found its hit (a later normalAt() overwrites them, so copy them first):
//
//   gArbLevel  depth of the nearest limb (0 = trunk)
//   gArbAlong  0 at that limb's base, 1 at its tip
//   gArbPath   which fork was taken at every level, as a base-k number: a
//              stable id for the branch, so hash it for per-branch variation
//              (blossoms, firing dendrites, a hue) that holds while the tree
//              moves
//   gArbTip    world distance to the tip of the deepest grown limb on that
//              path: a sphere of radius R there is a bud, a leaf pad, a polyp
//
// ---- growth --------------------------------------------------------------------
//
// `grow` counts levels INCLUDING the trunk and may be fractional: level i is
// drawn at clamp(grow - i, 0, 1) of its length, and nothing past it exists. So
// growth eases instead of popping a level in, which is what lets audio drive it.
// The loop bound is the compile-time ARB_MAX_LEVELS + 1 with a break, as the
// RAYMARCH note in lib_sdf3 requires; ARB_MAX_LEVELS folds is the ceiling.

#define ARB_MAX_LEVELS 9
#define ARB_TAU 6.2831853

float gArbLevel;
float gArbAlong;
float gArbPath;
float gArbTip;

/**
 * The shape of a 3D tree. Lengths are world units at the trunk; every level is
 * `ratio` times the one before.
 */
struct ArborForm {
    /** Children per fork. Below 2.5 a mirror (no atan), else that many polar wedges. */
    float arms;
    /** Angle of a child off its parent's axis, radians. */
    float tilt;
    /** Added to tilt at every level: positive droops (willow), negative curls up (coral). */
    float bend;
    /** Turn about the child's own axis per level; it is what makes a mirror tree 3D. */
    float twist;
    /** Child length / parent length. */
    float ratio;
    /** Trunk length. */
    float len;
    /** Trunk radius / trunk length. */
    float girth;
    /** Tip radius / base radius of one limb. Equal to ratio for a seamless taper. */
    float taper;
};

/** A 2D tree. Two children per fork, each side with its own angle and length. */
struct ArborForm2 {
    /** Fork angle of the +x child (x) and the -x child (y). */
    vec2 spread;
    /** Length ratio of the +x child (x) and the -x child (y). */
    vec2 ratio;
    /** Whole-fork turn per level, radians, positive toward +x: wind, gravity, a lean. */
    float lean;
    /** Extra lean per fork, hashed from the branch id: radians of irregularity. */
    float jitter;
    float len;
    float girth;
    float taper;
};

/** A tapered limb from the origin up +y, length h. `along` is 0 at the base, 1 at the tip. */
float arbLimb3(vec3 p, float h, float r0, float r1, out float along) {
    along = clamp(p.y / max(h, 1e-4), 0.0, 1.0);
    return length(vec3(p.x, p.y - along * h, p.z)) - mix(r0, r1, along);
}

float arbLimb2(vec2 p, float h, float r0, float r1, out float along) {
    along = clamp(p.y / max(h, 1e-4), 0.0, 1.0);
    return length(vec2(p.x, p.y - along * h)) - mix(r0, r1, along);
}

/**
 * A 3D tree rooted at the origin, trunk up +y. Returns world distance and
 * writes the gArb* globals. See the header for why this is a fold.
 */
float arborTree(vec3 p, ArborForm f, float grow) {
    float s = 1.0;
    float r = f.girth * f.len;
    float best = 1e9;
    float tilt = f.tilt;
    float path = 0.0;
    float tipD = 1e9;
    gArbLevel = 0.0;
    gArbAlong = 0.0;
    for (int i = 0; i <= ARB_MAX_LEVELS; i++) {
        float fill = clamp(grow - float(i), 0.0, 1.0);
        if (fill <= 0.0) break;
        if (i > 0) {
            p.y -= f.len;
            if (f.arms < 2.5) {
                path = path * 2.0 + step(0.0, p.x);
                p.x = abs(p.x);
            } else {
                float w = ARB_TAU / f.arms;
                float a = atan(p.z, p.x) + 0.5 * w;
                float k = floor(a / w);
                path = path * f.arms + mod(k, f.arms);
                a -= (k + 0.5) * w;
                p.xz = length(p.xz) * vec2(cos(a), sin(a));
            }
            // Onto the child's axis (it leans toward +x), then the twist that
            // turns the NEXT fork's plane.
            p.xy = rot2(-tilt) * p.xy;
            p.xz = rot2(f.twist) * p.xz;
            p /= f.ratio;
            s *= f.ratio;
            tilt += f.bend;
        }
        float h = f.len * fill;
        float al;
        float d = arbLimb3(p, h, r, r * f.taper, al) * s;
        if (d < best) {
            best = d;
            gArbLevel = float(i);
            gArbAlong = al;
        }
        tipD = length(p - vec3(0.0, h, 0.0)) * s;
    }
    gArbPath = path;
    gArbTip = tipD;
    return best;
}

/**
 * A 2D tree rooted at the origin, trunk up +y, in the units of p. `seed`
 * reseeds the per-branch jitter: a new seed is a new tree of the same habit.
 *
 * The two sides are folded together but measured with their own angle and
 * ratio, so a fork can be lopsided (a lightning channel and its side spark).
 * A point near the fold line only sees its own side's subtree; with thin limbs
 * that is invisible.
 */
float arborTree2(vec2 p, ArborForm2 f, float grow, float seed) {
    float s = 1.0;
    float r = f.girth * f.len;
    float best = 1e9;
    float path = 0.0;
    float tipD = 1e9;
    gArbLevel = 0.0;
    gArbAlong = 0.0;
    for (int i = 0; i <= ARB_MAX_LEVELS; i++) {
        float fill = clamp(grow - float(i), 0.0, 1.0);
        if (fill <= 0.0) break;
        if (i > 0) {
            p.y -= f.len;
            float lean = f.lean + f.jitter * (hash11(seed + path * 0.7123 + float(i) * 3.1) - 0.5);
            p = rot2(-lean) * p;
            float side = step(0.0, p.x);
            p.x = abs(p.x);
            p = rot2(-mix(f.spread.y, f.spread.x, side)) * p;
            float k = mix(f.ratio.y, f.ratio.x, side);
            p /= k;
            s *= k;
            path = path * 2.0 + side;
        }
        float h = f.len * fill;
        float al;
        float d = arbLimb2(p, h, r, r * f.taper, al) * s;
        if (d < best) {
            best = d;
            gArbLevel = float(i);
            gArbAlong = al;
        }
        tipD = length(p - vec2(0.0, h)) * s;
    }
    gArbPath = path;
    gArbTip = tipD;
    return best;
}

/**
 * A crystal dendrite: every node forks THREE ways, straight on (mainRatio) and
 * a mirrored pair at +-side (sideRatio). The straight chain is the spine and
 * each node along it sprouts side arms that are spines themselves, which is
 * how ice and frost grow. The three-way choice is a partition by angle at the
 * node: within side/2 of straight is the spine's child, past it the side
 * arm's. Trunk up +y.
 */
float arborDendrite2(vec2 p, float side, float mainRatio, float sideRatio, float len, float girth, float grow) {
    float s = 1.0;
    float r = girth * len;
    float best = 1e9;
    float path = 0.0;
    float tipD = 1e9;
    gArbLevel = 0.0;
    gArbAlong = 0.0;
    for (int i = 0; i <= ARB_MAX_LEVELS; i++) {
        float fill = clamp(grow - float(i), 0.0, 1.0);
        if (fill <= 0.0) break;
        if (i > 0) {
            p.y -= len;
            p.x = abs(p.x);
            float k = mainRatio;
            float branch = 0.0;
            if (atan(p.x, p.y) > 0.5 * side) {
                p = rot2(-side) * p;
                k = sideRatio;
                branch = 1.0;
            }
            p /= k;
            s *= k;
            path = path * 2.0 + branch;
        }
        float h = len * fill;
        float al;
        float d = arbLimb2(p, h, r, r * 0.8, al) * s;
        if (d < best) {
            best = d;
            gArbLevel = float(i);
            gArbAlong = al;
        }
        tipD = length(p - vec2(0.0, h)) * s;
    }
    gArbPath = path;
    gArbTip = tipD;
    return best;
}

/** The Complexity macro, 0..1 (uMorph; neutral 0). */
float arbMorph() {
    return clamp(uMorph, 0.0, 1.0);
}

/** The Chaos macro, 0..2: uTurbulence plus uWarp, both neutral at 0. Scale per-branch jitter and sway noise by it. */
float arbChaos() {
    return clamp(uTurbulence, 0.0, 1.0) + clamp(uWarp, 0.0, 1.0);
}

/**
 * Fold depth for a marched style: `full` levels at default, up to `extra` more
 * with Complexity, then cut for the Detail control. The march budget is the
 * only quality signal a style gets, so the fold depth follows it: two levels
 * fewer on the cheapest budget, one fewer in the middle. Levels are the twigs,
 * the part of the tree least missed. Never past the loop ceiling.
 */
float arbLevels(float full, float extra) {
    float n = full + extra * arbMorph();
    n -= uSteps < 80.0 ? 2.0 : (uSteps < 104.0 ? 1.0 : 0.0);
    return clamp(n, 1.0, float(ARB_MAX_LEVELS) + 1.0);
}

/** The same for a flat style: no march budget to answer to, so only Complexity moves it. */
float arbLevels2(float full, float extra) {
    return clamp(full + extra * arbMorph(), 1.0, float(ARB_MAX_LEVELS) + 1.0);
}

/** A stable 0..1 per branch: hash of the path id, the level and a style seed. */
float arbBranchHash(float path, float level, float seed) {
    return hash11(path * 0.6180339 + level * 17.13 + seed);
}

/** Ambient occlusion from the march's own step count: a ray that crept along a crevice used many. */
float arbStepAO(float used, float budget) {
    return clamp(1.0 - used / max(budget, 1.0) * 1.3, 0.2, 1.0);
}

/** (wrap diffuse, rim, specular) for a limb surface lit from direction l. */
vec3 arbLight(vec3 n, vec3 rd, vec3 l, float wrap, float shine) {
    float diff = clamp((dot(n, l) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
    float spec = pow(max(dot(reflect(rd, n), l), 0.0), shine);
    return vec3(diff, rim, spec);
}

/** Coverage of a 2D distance at pixel footprint px: 1 inside, 0 outside, antialiased. */
float arbInk(float d, float px) {
    return 1.0 - smoothstep(-px, px, d);
}
