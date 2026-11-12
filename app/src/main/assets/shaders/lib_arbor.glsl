// The Arbor family's branching distance fields: fractal trees as an iterated
// fold, in 3D for the marched styles and in 2D for the flat ones.
//
// Include after lib_scene_uniforms and lib_sdf3 (it calls rot2() and hash11()).
// The style then defines ArborHit arborMap(vec3 p), its world at p, and after it
// includes lib_arbor_field, which routes it through the family's one field.
// The tree functions read no uniform at all. The macro helpers at the bottom
// read uSteps, uMorph, uTurbulence and uWarp, the style knobs (arbKnob) and the
// mid band (arbMidBow), and arbView() the view controls, each only inside the
// helper that needs it, so a style that never calls one leaves that uniform to
// the linker.
//
// ---- the Customize macros --------------------------------------------------------
//
// Every Arbor style answers the same six macros through standard scene params,
// neutral at their defaults: Speed scales animation time (uTime, which already integrates Speed),
// Complexity (uMorph) adds fold levels within the march budget (arbLevels),
// Chaos (uTurbulence + uWarp, arbChaos) adds per-branch jitter and sway noise
// and uTwist a spiral twist, Colour is pal(), Energy (uIntensity) scales the
// in-shader audio amounts, Space is the camera rig or the zoom. They live
// here so "Complexity" adds levels the same way in all twelve. arbView()
// replaces view() so those four uniforms are not ALSO applied as screen warps.
// arbView() reads vUv, so include this file only in a fragment style that
// declares `in vec2 vUv` (every style does).
//
// The six style knobs are read in each style as offsets from 0.5 (arbKnob),
// so with Note & key reactions off the style is its tuned self. Willow,
// Coral, Frost, Bonsai, Canopy, Spire and Synapse give them lib_flame's,
// lib_opal's and lib_hollow's roles: k0 PULSE the emission, k1 TEXTURE the
// limbs' girth, k2 FLOW added to the Chaos bend, k3 TONE the palette position,
// k4 BUILD the glow, k5 SPACE the reach (the haze, or the orbit). The older
// five (Grove, Bolt, Roots, Rosette, Sakura) still spend them their own way.
//
// ---- the normalization shell ----------------------------------------------------
//
// Every shape variable of the family is read once, normalized, in arborIn():
// one field of ArborIn each, 0 at rest, -1..1 from a knob (its offset from 0.5,
// doubled) or the Twist slider, 0..1 from the other params (0 their default).
// Each is then mapped to its own range in one place: the tree's form in
// arborShape() (arborShape2() for a flat tree), the rest in the style's setup
// block through arbSpan(), once, into a named value the shading reads. The
// drivers are scene params and style knobs only, every one a target of the
// reaction matrix, so the music moves any variable through the matrix; nothing
// in the shell reads the sound frame. At the defaults every variable is exactly
// the constant it was, so the rest look is unchanged.
//
//   variable  driver                    mapped to
//   levels    Morph                     fold levels, arbLevels: + extra
//   chaos     Turbulence + Warp, FLOW   0..2, per-branch jitter and sway noise
//   twist     Twist                     the forks' spiral, arborShape: +-0.6 rad
//   girth     k1 TEXTURE                limb girth, arborShape: x0.75..x1.25
//   density   k1 TEXTURE                the foliage's size (leaves, pads, polyps,
//                                       beads, blossoms), each style's own range
//   spread    k2 FLOW                   a child's angle off its parent, x0.8..x1.25
//   bend      k2 FLOW                   gravity on every limb, +-0.12 rad a level
//   flow      k2 FLOW                   a style's own wind, fall or flow rate
//   scale     k5 SPACE                  the trunk's length, x0.92..x1.08
//   reach     k5 SPACE                  haze, orbit and far, each style's own
//   glow      k4 BUILD                  the style's glow
//   emit      k0 PULSE                  the emission
//   tone      k3 TONE                   the palette position
//
// A branch COUNT (ArborForm.arms) is not driven: a count pops on every beat
// (lib_neon's rule); Morph's levels ease in instead.
//
// THE FIELD. arborField(p) (lib_arbor_field) is the family's one field: an
// ArborHit, the distance with everything the walk and the style found there
// (the limb's level, along, path and tip, and the style's material), and it
// writes no global. ARBOR_GROW there is the one line where Grow (lib_grow:
// Spawn, Fractal, Front and Back layer) and Formula will wrap the style's map
// once they land.
//
// The mid band bows the limbs (arbMidBow): sndShaped(SND_MIDDLE), times SND_DRIVE,
// added to a 3D form's bend, a 2D form's lean or a dendrite's side angle, so
// the boughs give while the mids climb and lift back as they fall. A tree
// never turns (trees hold still, and no style spins to the music, owner
// 2026-11-12): the bow bends the limbs, never the whole tree. Mid gain scales it.
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
// and friends return it in their ArborHit, beside the distance:
//
//   level  depth of the nearest limb (0 = trunk)
//   along  0 at that limb's base, 1 at its tip
//   path   which fork was taken at every level, as a base-k number: a
//          stable id for the branch, so hash it for per-branch variation
//          (blossoms, firing dendrites, a hue) that holds while the tree
//          moves
//   tip    world distance to the tip of the deepest grown limb on that
//          path: a sphere of radius R there is a bud, a leaf pad, a polyp
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

/**
 * What the field found at a point: the distance, where on the tree (the walk's
 * level, along, path and tip, as the header says; 0 where no tree was walked)
 * and the style's own material in `mat` (each style says what its channels hold).
 */
struct ArborHit {
    float d;
    float level;
    float along;
    float path;
    float tip;
    vec4 mat;
};

/** A hit at distance d off the tree: no limb, no material. */
ArborHit arbHit(float d) {
    return ArborHit(d, 0.0, 0.0, 0.0, 0.0, vec4(0.0));
}

/** The nearer of two hits. */
ArborHit arbNear(ArborHit a, ArborHit b) {
    return b.d < a.d ? b : a;
}

/**
 * The shape of a 3D tree. Lengths are world units at the trunk; every level is
 * `ratio` times the one before.
 */
struct ArborForm {
    /** Children per fork. Below 2.5 a mirror (no atan), else that many polar wedges. */
    float arms;
    /** Angle of a child off its parent's axis, radians. */
    float tilt;
    /**
     * Gravity, radians per level: each child turns toward world DOWN by bend
     * times the sine of its angle off vertical, so a horizontal limb sags most
     * and a hanging one stops. Negative turns toward UP (coral, phototropism).
     * Parent-relative tilt cannot do this: with any twist the tilts spiral.
     */
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
 * where on the tree it was found. See the header for why this is a fold.
 */
ArborHit arborTree(vec3 p, ArborForm f, float grow) {
    float s = 1.0;
    float r = f.girth * f.len;
    float best = 1e9;
    float path = 0.0;
    float tipD = 1e9;
    vec3 tipP = p;
    float tipS = 1.0;
    // World down, carried through every frame change the point goes through,
    // so a limb deep in the tree still knows which way gravity pulls.
    vec3 g = vec3(0.0, -1.0, 0.0);
    mat2 tiltM = rot2(-f.tilt);
    mat2 twistM = rot2(f.twist);
    ArborHit hit = arbHit(1e9);
    for (int i = 0; i <= ARB_MAX_LEVELS; i++) {
        float fill = clamp(grow - float(i), 0.0, 1.0);
        if (fill <= 0.0) break;
        if (i > 0) {
            p.y -= f.len;
            if (f.arms < 2.5) {
                float side = step(0.0, p.x);
                path = path * 2.0 + side;
                float m = 2.0 * side - 1.0;
                p.x *= m;
                g.x *= m;
            } else {
                float w = ARB_TAU / f.arms;
                float a = atan(p.z, p.x) + 0.5 * w;
                float k = floor(a / w);
                path = path * f.arms + mod(k, f.arms);
                a -= (k + 0.5) * w;
                p.xz = length(p.xz) * vec2(cos(a), sin(a));
                float c = cos(k * w);
                float sn = sin(k * w);
                g.xz = vec2(c * g.x + sn * g.z, c * g.z - sn * g.x);
            }
            // Onto the child's axis (it leans toward +x), then the twist that
            // turns the NEXT fork's plane.
            p.xy = tiltM * p.xy;
            g.xy = tiltM * g.xy;
            p.xz = twistM * p.xz;
            g.xz = twistM * g.xz;
            // Gravity: turn the child's axis toward g's sideways part.
            float gl = length(g.xz);
            if (gl > 1e-4) {
                vec2 h = g.xz / gl;
                float beta = f.bend * gl;
                float cb = cos(beta);
                float sb = sin(beta);
                float pa = dot(p.xz, h);
                vec2 pr = p.xz - pa * h;
                p = vec3(pr.x + (cb * pa - sb * p.y) * h.x, sb * pa + cb * p.y, pr.y + (cb * pa - sb * p.y) * h.y);
                float ga = dot(g.xz, h);
                vec2 gr = g.xz - ga * h;
                g = vec3(gr.x + (cb * ga - sb * g.y) * h.x, sb * ga + cb * g.y, gr.y + (cb * ga - sb * g.y) * h.y);
            }
            p /= f.ratio;
            s *= f.ratio;
        }
        float h = f.len * fill;
        float al;
        float d = arbLimb3(p, h, r, r * f.taper, al) * s;
        if (d < best) {
            best = d;
            hit.level = float(i);
            hit.along = al;
        }
        tipP = p - vec3(0.0, h, 0.0);
        tipS = s;
    }
    tipD = length(tipP) * tipS;
    hit.d = best;
    hit.path = path;
    hit.tip = tipD;
    return hit;
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
ArborHit arborTree2(vec2 p, ArborForm2 f, float grow, float seed) {
    float s = 1.0;
    float r = f.girth * f.len;
    float best = 1e9;
    float path = 0.0;
    float tipD = 1e9;
    ArborHit hit = arbHit(1e9);
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
            hit.level = float(i);
            hit.along = al;
        }
        tipD = length(p - vec2(0.0, h)) * s;
    }
    hit.d = best;
    hit.path = path;
    hit.tip = tipD;
    return hit;
}

/**
 * A crystal dendrite: every node forks THREE ways, straight on (mainRatio) and
 * a mirrored pair at +-side (sideRatio). The straight chain is the spine and
 * each node along it sprouts side arms that are spines themselves, which is
 * how ice and frost grow. The three-way choice is a partition by angle at the
 * node: within side/2 of straight is the spine's child, past it the side
 * arm's. Trunk up +y.
 */
ArborHit arborDendrite2(vec2 p, float side, float mainRatio, float sideRatio, float len, float girth, float grow) {
    float s = 1.0;
    float r = girth * len;
    float best = 1e9;
    float path = 0.0;
    float tipD = 1e9;
    ArborHit hit = arbHit(1e9);
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
            hit.level = float(i);
            hit.along = al;
        }
        tipD = length(p - vec2(0.0, h)) * s;
    }
    hit.d = best;
    hit.path = path;
    hit.tip = tipD;
    return hit;
}

/**
 * view() without its four screen warps: Morph's polar remap, Turbulence,
 * Twist and Warp. Every Arbor style reads its uv from this instead.
 *
 * Those four uniforms are the Complexity and Chaos macros here, and they
 * already mean something inside the tree (more levels, per-branch jitter, a
 * spiral in the forks). view() would ALSO bend the finished picture with
 * them, and a marched style's rays with it, so a Complexity of 1 turned the
 * scene into a polar smear instead of a denser tree. Everything else view()
 * does (aspect, pixelate, mirror, kaleidoscope, drift, rotation and sway, the
 * beat pulse and zoom, ripple) is kept line for line, so those sliders mean
 * what they mean on every other style.
 */
vec2 arbView() {
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= uResolution.x / uResolution.y;
    if (uPixelate > 0.001) {
        float px = mix(1.0, 12.0, uPixelate) * 24.0;
        uv = floor(uv * px) / px;
    }
    if (uMirrorX > 0.5) uv.x = abs(uv.x);
    if (uKaleido > 0.5 && uSymmetry >= 2.0) {
        float ang = atan(uv.y, uv.x);
        float rad = length(uv);
        float seg = 6.2831853 / uSymmetry;
        ang = abs(mod(ang, seg) - seg * 0.5);
        uv = vec2(cos(ang), sin(ang)) * rad;
    }
    float a = uRotation + uSway * 0.35 * sin(uTime * 0.7);
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Drift after the spin, so the rotation pivots on the screen centre.
    vec2 driftPhase = fract(vec2(uDriftX, uDriftY) * uTime * 0.025 + 0.25);
    uv += 1.0 - 2.0 * abs(2.0 * driftPhase - 1.0);
    float z = uZoom;
    uv /= max(z, 0.05);
    if (uTile > 1.01) {
        uv = 1.0 - abs(mod(uv * uTile * 0.5 + 1.0, 4.0) - 2.0);  // mirrored: a hard wrap cracks
    }
    if (uRipple > 0.001) {
        float r = length(uv);
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndShaped(SND_LOW) * 4.0);
    }
    return uv;
}

/** Style knob i as its offset from 0.5: exactly 0 while Note & key reactions are off. */
float arbKnob(int i) {
    return clamp(uKnob[i], 0.0, 1.0) - 0.5;
}

/** The mid band's bow, radians (see the header): 0.12 at most, 0 in silence. */
float arbMidBow() {
    return 0.08 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0);
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

// ---- the normalization shell (see the header) -----------------------------------

/** Every shape variable, normalized: 0 at rest, -1..1 from a knob or Twist, 0..1 from Morph. */
struct ArborIn {
    float levels;
    float chaos;
    float twist;
    float girth;
    float density;
    float spread;
    float bend;
    float flow;
    float scale;
    float reach;
    float glow;
    float emit;
    float tone;
};

/** Each variable from its driver (the header's table): params and knobs only, no sound. */
ArborIn arborIn() {
    float kTex = 2.0 * arbKnob(1);
    float kFlow = 2.0 * arbKnob(2);
    float kSpace = 2.0 * arbKnob(5);
    ArborIn n;
    n.levels = arbMorph();
    // Half the Chaos macro (0..2) plus FLOW's share, so arbSpan(n.chaos, 0.0, 0.0, 2.0) is the old
    // arbChaos() + 0.5 * FLOW's offset, floored at none.
    n.chaos = clamp(0.5 * arbChaos() + 0.125 * kFlow, -1.0, 1.0);
    n.twist = clamp(uTwist, -1.0, 1.0);
    n.girth = kTex;
    n.density = kTex;
    n.spread = kFlow;
    n.bend = kFlow;
    n.flow = kFlow;
    n.scale = kSpace;
    n.reach = kSpace;
    n.glow = 2.0 * arbKnob(4);
    n.emit = 2.0 * arbKnob(0);
    n.tone = 2.0 * arbKnob(3);
    return n;
}

/**
 * A normalized n to its variable's range: `rest` at 0, `lo` at -1, `hi` at 1, straight between.
 * Exactly `rest` at 0, so the rest look is the old one bit for bit. A 0..1 variable uses rest..hi.
 */
float arbSpan(float n, float rest, float lo, float hi) {
    return rest + n * (n < 0.0 ? rest - lo : hi - rest);
}

/**
 * A 3D tree's form, `f` at rest, mapped by the normalized variables: spread opens the
 * forks, bend adds gravity, twist winds them, scale lengthens the trunk, girth thickens
 * every limb. The arms (a count) stay the style's. f itself at rest.
 */
ArborForm arborShape(ArborForm f, ArborIn n) {
    f.tilt *= arbSpan(n.spread, 1.0, 0.8, 1.25);
    f.bend += arbSpan(n.bend, 0.0, -0.12, 0.12);
    f.twist += arbSpan(n.twist, 0.0, -0.6, 0.6);
    f.len *= arbSpan(n.scale, 1.0, 0.92, 1.08);
    f.girth *= arbSpan(n.girth, 1.0, 0.75, 1.25);
    return f;
}

/** The same for a flat tree: spread opens both sides, twist leans every fork, scale and girth as in 3D. */
ArborForm2 arborShape2(ArborForm2 f, ArborIn n) {
    f.spread *= arbSpan(n.spread, 1.0, 0.8, 1.25);
    f.lean += arbSpan(n.twist, 0.0, -0.15, 0.15);
    f.len *= arbSpan(n.scale, 1.0, 0.92, 1.08);
    f.girth *= arbSpan(n.girth, 1.0, 0.75, 1.25);
    return f;
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
