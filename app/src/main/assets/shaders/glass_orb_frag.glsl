#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Orb: one large solid glass sphere hovering over a glossy black floor in
// the middle of a ring of twelve crystal pillars, the family's free style. The
// camera orbits it. Each pillar is a hex prism with a pointed tip and a tall
// light strip down its inner face, so the ring is the studio's strip lights
// made solid: you see them stand in fog with real parallax as the eye circles,
// see them through the ball upside down with rainbow edges (three-wavelength
// dispersion on the way out), see the share the far wall reflects come round a
// second time as a fainter, inverted-again ghost, and see the whole room in the
// floor. Between and above the pillars the family's FRACTAL world keeps
// moving. Under the ball the floor carries the shadow a glass ball really
// casts: dark round the outside, a focused caustic spot at its heart, fringed
// red-out, blue-in, plus a soft mirror image of the orb itself.
//
// ---- the camera ---------------------------------------------------------------
//
// The shared rig, object-centred (camAround): the eye sits OR_CAM_R behind the
// rig's forward direction and turns about +y at 3 * CAM_SPIN_UNIT per unit of
// travel, an integer multiple of a turn per 1024, so the rig's 8192 travel wrap
// lands on a whole number of turns and never shows. Nothing in the scene
// depends on z at all (it is a ring about the origin), so there is no other
// wrap to fit. The dolly pulls the eye in; the eye's height is held above the
// floor, so a steep look up never puts the camera under it. The eye's orbit
// (at most 4.9 out) stays well inside the ring's inner faces (6.2).
//
// ---- audio ------------------------------------------------------------------
//
// None of these drives the frame's brightness:
//   bass (GlassEar.bass)     swells the IOR 1.36 -> 1.6: the room inside bulges
//                            and the caustic spot under the ball tightens;
//   centroid (sharp)         widens the dispersion and the key highlight's bite;
//   flatness (frost)         frosts the ball: the lookups roughen, grain appears;
//   pitch height             raises the key light, so the crown highlight, the
//                            shadow and the floor spot follow the melody;
//   EQ bands (sndEq)         one band per pillar round the ring: each strip lights
//                            up its pillar as far as its band reaches, a spectrum
//                            meter you see in the room, through the ball and in the floor;
//   hat                      throws glints across the ball and the pillars;
//   kick                     rings the ball: standing ripples run over its skin.
// The key (uKeyHue, uKeyStrength) drifts the palette.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed scales the bob and the world's flow. Complexity tints the glass. Chaos
// wobbles the ball (glassWobble) and warps the pillars (glassSurfaceWarp).
// Energy scales every reaction (via glassListen). Knobs: PULSE the kick ring,
// TEXTURE extra frost, FLOW the bob, TONE the hue, BUILD the dispersion, SPACE
// the fog.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Only the pillars are marched; the ball (one quadratic) and the floor (one
// plane) are exact intersections, and the march is bounded by the nearer of
// them. The pillar field is the ring folded into one 30 degree wedge by angle:
// that fold is a rotation inside the wedge, so it keeps distances, and each
// pillar (apothem 0.42 at radius 6.6) is far inside its wedge (half-chord 1.7),
// so the wedge's own pillar is always the nearest. The hex prism is a max of
// unit-normal face planes and the tip a plane normalised by sqrt(1 + 1.3^2),
// both lower bounds; Chaos's surface warp is divided by glassSurfaceLip(). A
// sphere round the eye is carved out as well. Rays that have left the ring
// heading outward stop at once: nothing lies beyond it.
//
// Reflected and refracted rays are not marched: from anywhere inside the ring
// the pillars' inner faces are one cylinder, so a ray-cylinder quadratic finds
// which pillar, how high and how far across its face the ray lands. That is
// what lets the ball show the room three times over at phone cost.
//
// ---- cost -------------------------------------------------------------------
//
// One march of the pillars (uSteps, Detail) per pixel. On the ball about five
// world lookups (3 dispersed, 1 second bounce, 1 reflection); on the floor one
// (its mirror); elsewhere one. Saver drops the bounce.

#define OR_MAX_STEPS 128
#define OR_FAR 34.0
#define OR_CAM_R 3.6
#define OR_RADIUS 1.0
#define OR_FLOOR -1.05
/** Pillars round the ring, their axis radius, hex apothem, shoulder height above the floor and tip slope. */
#define OR_PILLARS 12.0
#define OR_RING 6.6
#define OR_AP 0.42
#define OR_TOP 4.2
#define OR_TIP 1.3
/** The strip's lit length runs up to here above the floor. */
#define OR_STRIP 3.5

vec3 gEye;
float gHue;

float knob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

/** Folds p into its pillar's wedge: q is (radial offset from the pillar axis, height above the floor, across). Returns the pillar's index 0..11. */
float pillarLocal(vec3 p, out vec3 q) {
    float sec = GLASS_TAU / OR_PILLARS;
    float r = length(p.xz);
    float a = r > 1e-4 ? atan(p.z, p.x) : 0.0; // atan(0, 0) is undefined in GLSL ES
    float k = floor(a / sec + 0.5);
    float la = a - k * sec;
    q = vec3(r * cos(la) - OR_RING, p.y - OR_FLOOR, r * sin(la));
    return mod(k + OR_PILLARS, OR_PILLARS);
}

float map(vec3 p) {
    vec3 q;
    pillarLocal(p, q);
    vec2 aq = abs(q.xz);
    // Hex section with a flat face toward the orb: faces at 0, 60 and 120 degrees.
    float pr = max(aq.y * 0.8660254 + aq.x * 0.5, aq.x);
    float side = pr - OR_AP;
    float cap = (q.y - OR_TOP + OR_TIP * pr) / 1.6401219;
    float d = max(side, cap);
    d = (d + glassSurfaceWarp(p)) / glassSurfaceLip();
    return max(d, 0.5 - length(p - gEye));
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

/** How far up its pillar a strip is lit, 0..1: that pillar's EQ band, with a floor so a quiet ring still reads. */
float stripTop(float fi) {
    float band = clamp(sndEq((fi + 0.5) / OR_PILLARS) * 0.42, 0.0, 1.0) * min(glassEnergy(), 1.0);
    return 0.16 + 0.84 * band;
}

/**
 * The light strip on pillar `fi`'s inner face at height `y` above the floor and
 * `s` across the face. `rough` widens it and lowers its peak (seen through
 * frosted glass). A white-hot core in a palette halo, cut off where its band
 * stops, with a small bead riding the top like a meter's peak.
 */
vec3 stripLight(float fi, float y, float s, float rough) {
    float u = y / OR_STRIP;
    float top = stripTop(fi);
    float lit = (1.0 - smoothstep(top - 0.04, top + 0.04, u)) * smoothstep(0.0, 0.03, u);
    float w = 0.05 + rough * 0.6;
    float as = abs(s);
    float core = (1.0 - smoothstep(w, w + 0.03 + rough, as)) * (0.05 / w);
    float halo = exp(-as * 12.0 / (1.0 + rough * 8.0)) * 0.22;
    vec3 sc = glassVivid(pal(gHue + fi / OR_PILLARS), 0.7);
    float bead = exp(-(u - top) * (u - top) * 900.0) * core;
    return (mix(sc, vec3(1.0), 0.45) * core * 1.5 + sc * halo) * lit + sc * bead * 0.8;
}

/** A pillar's dark crystal body, lit by the key light. */
vec3 pillarBody(float fi, float dif, float ao) {
    return pal(gHue + fi / OR_PILLARS + 0.5) * (0.025 + 0.09 * dif) * ao;
}

/**
 * What a ray from `o` inside the ring sees when it is not heading for the
 * floor: a pillar's inner face (one ray-cylinder quadratic; see the header),
 * else the FRACTAL world. `rough` frosts both.
 */
vec3 sky(vec3 o, vec3 d, GlassLook lk, float rough) {
    float a = dot(d.xz, d.xz);
    float rin = OR_RING - OR_AP;
    float cc = dot(o.xz, o.xz) - rin * rin;
    if (a > 1e-5 && cc < 0.0) {
        float b = dot(o.xz, d.xz);
        float t = (-b + sqrt(max(b * b - a * cc, 0.0))) / a;
        vec3 p = o + d * t;
        float y = p.y - OR_FLOOR;
        float sec = GLASS_TAU / OR_PILLARS;
        float ang = atan(p.z, p.x);
        float k = floor(ang / sec + 0.5);
        float s = rin * (ang - k * sec);
        float fi = mod(k + OR_PILLARS, OR_PILLARS);
        float cover = (1.0 - smoothstep(OR_AP * 0.75, OR_AP * 0.95, abs(s)))
                    * (1.0 - smoothstep(OR_TOP - 0.7, OR_TOP - 0.35, y)) * step(0.0, y);
        if (cover > 0.0) {
            vec3 L = glassKeyDir(lk);
            float dif = max(dot(normalize(-vec3(p.x, 0.0, p.z)), L), 0.0);
            vec3 pc = pillarBody(fi, dif, 1.0) + stripLight(fi, y, s, rough);
            if (cover > 0.999) return pc;
            return mix(glassEnvRough(d, lk, rough), pc, cover);
        }
    }
    return glassEnvRough(d, lk, rough);
}

/**
 * The floor's own light at `p`: the dark stone, the key light's pool with the
 * ball's shadow in it (a dark disc round a focused caustic spot, the spot
 * tighter as the IOR rises: `focus` is its width, red wider than blue), and
 * thin palette rings inlaid round the orb. No mirror here: that is floorAt's.
 */
vec3 floorLit(vec3 p, vec3 c, vec3 L, float focus, float dist) {
    vec3 base = pal(gHue + 0.6) * 0.018;
    vec3 key = mix(vec3(1.0), pal(gHue + 0.1), 0.35) * 0.1 * max(L.y, 0.0);
    // The ball's shadow: how close the ray from p toward the key passes the centre, in radii.
    vec3 oc = c - p;
    float along = dot(oc, L);
    vec3 shade = vec3(1.0);
    if (along > 0.0) {
        float s = sqrt(max(dot(oc, oc) - along * along, 0.0)) / OR_RADIUS;
        vec3 w = focus * vec3(1.15, 1.0, 0.87);
        vec3 spot = exp(-(s * s) / (w * w)) * (0.14 / (w * w));
        shade = mix(vec3(0.16) + spot, vec3(1.0), smoothstep(0.82, 1.05, s));
    }
    vec3 col = (base + key) * shade;
    // Inlaid rings every 1.3 between the ball and the pillars, wider with distance so they never shimmer.
    float r = length(p.xz);
    float f = abs(fract((r - 1.8) / 1.3 + 0.5) - 0.5) * 1.3;
    float aa = 0.008 + 0.003 * dist;
    float ring = (1.0 - smoothstep(0.01, 0.01 + aa, f)) * smoothstep(1.5, 1.7, r) * (1.0 - smoothstep(5.6, 5.9, r));
    col += glassVivid(pal(gHue + 0.08 * r), 0.6) * ring * 0.16;
    return col;
}

/** What any ray from `o` inside the ring sees, without the ball: the floor's own light or the sky. */
vec3 seen(vec3 o, vec3 d, vec3 c, GlassLook lk, float focus, float rough) {
    if (d.y < -1e-4) {
        float tf = (OR_FLOOR - o.y) / d.y;
        vec3 p = o + d * tf;
        if (dot(p.xz, p.xz) < (OR_RING - OR_AP) * (OR_RING - OR_AP)) return floorLit(p, c, glassKeyDir(lk), focus, tf);
    }
    return sky(o, d, lk, rough);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    gHue = 0.04 * sin(t * 0.023) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.25 * knob(3);
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, gHue, ear);
    float ior = 1.36 + 0.2 * clamp(ear.bass, 0.0, 1.2);
    float spread = max(0.012 + 0.04 * ear.sharp + 0.03 * knob(4), 0.004);
    float frost = clamp(ear.frost * 0.6 + 0.3 * knob(1), 0.0, 1.0);
    // The caustic spot's width on the floor: a stronger lens focuses it tighter.
    float focus = mix(0.34, 0.2, (ior - 1.36) / 0.24);
    int bounces = glassBounces();
    vec3 L = glassKeyDir(lk);
    float fogK = 0.035 * (1.0 - 1.2 * knob(5));
    vec3 fogCol = pal(gHue + 0.55) * 0.045;

    vec3 ro;
    vec3 rd;
    camAround(uv, OR_CAM_R, 3.0 * CAM_SPIN_UNIT, ro, rd);
    ro.y = max(ro.y, OR_FLOOR + 0.3);
    gEye = ro;
    vec3 c = vec3(0.0, OR_FLOOR + OR_RADIUS + 0.12 + 0.05 * (1.0 + knob(2)) * sin(t * 0.37), 0.0);
    vec3 tint = mix(vec3(1.0), pal(gHue + 0.5), 0.2 + 0.5 * glassComplexity());

    float tFloor = rd.y < -1e-4 ? (OR_FLOOR - ro.y) / rd.y : 1e9;
    vec2 hs = glassSphere(ro, rd, c, OR_RADIUS);
    float tOrb = hs.x > 0.0 ? hs.x : 1e9;
    float tMax = min(min(tFloor, tOrb), OR_FAR);

    // The pillars, marched. uSteps is the user's Detail; this loop's own bound is the constant.
    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < OR_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * tm;
        float d = map(p);
        float eps = glassEps(tm);
        if (d < eps) {
            hitT = tm;
            break;
        }
        // Past the ring and heading out: nothing more to hit.
        if (dot(p.xz, p.xz) > (OR_RING + 1.0) * (OR_RING + 1.0) && dot(p.xz, rd.xz) > 0.0) break;
        tm += max(d * 0.9, eps);
        used = float(i);
        if (tm > tMax) break;
    }

    vec3 col;
    if (hitT > 0.0) {
        // ---- a pillar ----
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(glassEps(hitT), 0.0015));
        vec3 q;
        float fi = pillarLocal(p, q);
        float ao = glassStepAO(used);
        float dif = max(dot(n, L), 0.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0 + 80.0 * ear.sharp);
        float inner = smoothstep(0.75, 0.95, dot(n.xz, -normalize(p.xz)));
        float h = gHue + fi / OR_PILLARS;
        // Fake refraction for the crystal body: the bent ray picks a palette spot, the channels staggered.
        vec3 rf = refract(rd, n, 0.67);
        float kp = h + 0.4 * (0.5 + 0.5 * dot(rf, vec3(0.42, 0.62, 0.66)));
        vec3 prism = vec3(pal(kp).r, pal(kp + 0.07).g, pal(kp + 0.14).b);
        col = pillarBody(fi, dif, ao) + prism * fres * 0.22 * ao + mix(vec3(1.0), prism, 0.4) * spec * 0.35;
        // The tip glows faintly from inside, as the canyon's crystals do.
        col += pal(h + 0.1) * smoothstep(OR_TOP - 1.2, OR_TOP, q.y) * 0.06;
        col += inner * stripLight(fi, q.y, q.z, 0.0);
        col += vec3(1.0) * glassGlint(p, 6.0, ear.hat) * (0.15 + 0.5 * fres) * 0.5;
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    } else if (tOrb < 1e8) {
        // ---- the ball ----
        vec3 p = ro + rd * tOrb;
        vec3 n = glassWobble(normalize(p - c), p, glassChaos());
        // The kick rings the ball like struck jelly: a few standing waves on its surface, eased.
        vec3 lc = (p - c) / OR_RADIUS;
        float ring = ear.kick * 0.14 * (1.0 + 1.6 * knob(0));
        n = normalize(n + ring * sin(lc * vec3(7.0, 9.0, 8.0) + t * vec3(5.0, 4.0, 6.0)));
        n = glassGrain(n, p, frost);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        float rough = lk.rough + frost * 0.25;

        // In, across, out per wavelength (a sphere's chord is one more quadratic).
        vec3 d1 = glassRefract(rd, n, 1.0 / ior);
        vec3 oc = p - c;
        float b = dot(oc, d1);
        float chord = max(-b + sqrt(max(b * b - (dot(oc, oc) - OR_RADIUS * OR_RADIUS), 0.0)), 0.0);
        vec3 qx = p + d1 * chord;
        vec3 n2 = normalize(qx - c);
        vec3 iors = glassIors(ior, spread);
        vec3 absorb = glassBeer(tint, chord, 0.5);
        float fExit = glassFresnel(max(dot(d1, n2), 0.0), ior);
        vec3 refr = vec3(seen(qx, glassRefract(d1, -n2, iors.x), c, lk, focus, rough).r,
                         seen(qx, glassRefract(d1, -n2, iors.y), c, lk, focus, rough).g,
                         seen(qx, glassRefract(d1, -n2, iors.z), c, lk, focus, rough).b) * absorb * (1.0 - fExit);
        if (bounces > 1) {
            // The far wall's reflected share crosses again (same chord) and leaves: the inverted ghost.
            vec3 d2 = reflect(d1, n2);
            vec3 q2 = qx + d2 * chord;
            vec3 n3 = normalize(q2 - c);
            vec3 e2 = glassRefract(d2, -n3, ior);
            refr += seen(q2, e2, c, lk, focus, rough) * absorb * absorb * fExit
                  * (1.0 - glassFresnel(max(dot(d2, n3), 0.0), ior)) * 1.6;
        }
        vec3 refl = seen(p, reflect(rd, n), c, lk, focus, lk.rough + frost * 0.15);
        col = refr * (1.0 - fr) + refl * fr;
        col += pal(gHue + 0.15) * pow(1.0 - cosI, 3.0) * 0.08;
        col += glassRim(cosI, gHue, 0.35);
        // The key's crown highlight: it walks with the pitch.
        col += vec3(1.0) * pow(max(dot(reflect(rd, n), L), 0.0), 60.0 + 200.0 * ear.sharp) * (0.5 + 0.5 * ear.sharp) * (1.0 - frost * 0.6);
        col += vec3(1.0) * glassGlint(p - c, 9.0, ear.hat) * (0.3 + 0.7 * fr + 0.3 * cosI);
        col = mix(col, fogCol, 1.0 - exp(-tOrb * fogK));
    } else if (tFloor < 1e8) {
        // ---- the floor: its own light plus a glossy mirror of the room ----
        vec3 p = ro + rd * tFloor;
        col = floorLit(p, c, L, focus, tFloor);
        vec3 rr = vec3(rd.x, -rd.y, rd.z);
        float cosV = clamp(-rd.y, 0.0, 1.0);
        float m = 1.0 - cosV;
        float gloss = 0.04 + 0.5 * m * m * m * m * m;
        float mrough = 0.03 + frost * 0.1;
        vec2 hm = glassSphere(p, rr, c, OR_RADIUS);
        vec3 mir;
        if (hm.x > 0.0) {
            // The orb's mirror image: one look through it, and its rim.
            vec3 pm = p + rr * hm.x;
            vec3 nm = normalize(pm - c);
            float ci = max(dot(nm, -rr), 0.0);
            mir = seen(pm, glassRefract(rr, nm, 1.0 / ior), c, lk, focus, mrough + 0.05) * tint * 0.8
                + glassRim(ci, gHue, 0.35);
        } else if (dot(p.xz, p.xz) < (OR_RING - OR_AP) * (OR_RING - OR_AP)) {
            mir = sky(p, rr, lk, mrough);
        } else {
            mir = glassEnvRough(rr, lk, mrough);
        }
        col += mir * gloss * 1.4;
        col = mix(col, fogCol, 1.0 - exp(-tFloor * fogK));
    } else {
        // Between and above the pillars: the world, hazed a little toward the horizon.
        col = glassEnv(rd, lk);
        col = mix(col, fogCol, (1.0 - smoothstep(0.0, 0.35, abs(rd.y))) * 0.5);
    }

    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
