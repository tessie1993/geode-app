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
//#include lib_chrys

// Chrysanthemum Neon: a flower drawn only in outline, as bent glass tubes in
// the dark, now a neon sculpture in space. Four nested petal rings still grow
// from the same small heart, each longer than the last and each in its own
// colour, and their outlines still weave into a lattice set by how many
// petals each ring has. What is new is depth: every ring's tube is bent up
// out of the plane into its own shallow cup (the inner rings steep, the outer
// ones nearly flat), so the rings stack in front of one another, a real tube
// of glass with a white-hot core where it faces the eye and a coloured rim
// where it turns away, haloed by the light it throws into the air. Behind the
// flower stands the dark backboard a neon sign is mounted on, etched with the
// old faint guide circles and lit by the tubes in front of it.
//
// Nothing is filled, as before: a tube is a hot core and a wide, faint halo,
// with a little light chasing round every petal's rim.
//
// ---- camera -----------------------------------------------------------------
//
// camAround, orbiting the heart (the Camera tab's look, dolly and FOV all
// apply). The flower's axis is tilted toward the orbit's starting side, so the
// view sweeps from face-on, over the board, round to a free-floating sculpture
// seen from behind; the board fades out as the eye passes behind its plane
// (a back face would be a black disc over the flower) and fades back in on
// the way round. uZoom still applies through view(). Nothing here depends on
// absolute travel except the orbit angle, which camAround keeps whole-turn
// periodic, so the rig's 8192 wrap never shows.
//
// ---- audio ------------------------------------------------------------------
//
// Unchanged in meaning from the flat style. The song's structure sets the
// PETAL COUNTS: each ring's count (5 to 12) comes from a hash of
// SND_CUT_COUNT, so every section has its own figure, held until the next.
// When a section changes, the old figure's tubes thin and dim while the new
// figure's thicken and brighten across the section envelope
// (SND_HELD_HARD_CUT), cupping a little deeper on the way (chrysBump, so it
// starts from rest); they no longer swirl round as they trade places (owner
// 2026-11-12, no style spins to the music). The old figure is the hash of the previous count, which is what the
// screen was already showing, so nothing pops, and the two weights sum to one
// so the total light stays constant. The presence band (sndSpectrum(0.80))
// stretches the chasing lights into long trails. The rings counter-rotate on
// uDrift. Brightness follows 0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE).
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) runs the counter-rotation, the
// chasing lights and the hue clock. Complexity: uMorph adds up to two more,
// longer rings (six at most). Chaos: uTurbulence + uWarp bend the tubes into
// hand-made, wobbly glass, now in and out of their cup as well as across it;
// uTwist curls every petal into a spiral. Energy: uIntensity scales the
// section cupping and the trail stretch. Colour: pal() for every tube (white-hot
// cores are a fixed accent). Knobs, as offsets from 0.5 so off is the tuned
// look: k0 PULSE how hard the presence swell brightens, k1 TEXTURE extra
// wobble, k2 FLOW the chase trail length, k3 TONE the hue, k4 BUILD the cup
// depth, k5 SPACE the orbit distance.
//
// ---- budget -----------------------------------------------------------------
//
// One ray-sphere test against NE_BOUND first: rays that miss the flower never
// march. Inside it, at most NE_MAX_STEPS steps, broken on uSteps (Detail).
// map() costs one chrysPetal per ring (four to six), and a second set only
// while the section crossfade still shows the old figure (a uniform-driven,
// coherent branch). The hit takes four more taps for the normal and the board
// takes one for the light it catches.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// A ring's outline is the zero set of the 2D petal distance d2 (lib_chrys's
// bound, slope at most about 1 in the plane, a little more with uTwist's
// curl), lifted onto the cup surface y = c r^2. The tube is the set where
// length(d2, y - c r^2) is below its radius. The cup's slope 2 c r stays
// under ~1.1 at the largest cup the knobs and the section allow (c r^2 at most
// ~0.8 at r ~1.5), so the field's gradient stays under ~1.7, and the march
// steps 0.6 of it. The chaos wobble is a few hundredths of a unit at most.
// The steps have a floor (eps) so a ray grazing a tube creeps past it rather
// than stalling; the halo accumulates along the way, so a grazed tube still
// glows. The flower sits inside NE_BOUND, which camAround's eye (radius >= ~1.9
// after SPACE and the dolly) stays at or outside of; the march starts at the
// sphere's entry clamped to 0, so an eye inside it still works.

#define NE_MAX_RINGS 6
#define NE_MAX_STEPS 96
#define NE_BOUND 1.9
/** Tube radius at full weight, world units: a few pixels at the orbit distance. */
#define NE_TUBE 0.014
/** The backboard's plane, along the flower's axis, just behind the heart. */
#define NE_BOARD -0.16
#define NE_BOARD_R 2.1

/** The flower's axis (its local +y) and its second in-plane direction; x stays world x. */
const vec3 NE_AXIS = vec3(0.0, 0.5, -0.8660254);
const vec3 NE_SIDE = vec3(0.0, 0.8660254, 0.5);

float neonCount(float section, float ring) {
    return 5.0 + floor(chrysHash(vec2(section * 1.37, ring * 4.1 + 2.0)) * 7.999);
}

float gT;
float gChaos;
float gRings;
float gWNew;
float gCupK;
float gRot[NE_MAX_RINGS];
float gCntNew[NE_MAX_RINGS];
float gCntOld[NE_MAX_RINGS];
vec3 gCol[NE_MAX_RINGS];
/** Set by map(): the nearest tube's ring, its petal frame (d, u, v, id), its count and figure weight. */
float gK;
vec4 gPet;
float gCnt;
float gW;

vec3 toFlower(vec3 p) {
    return vec3(p.x, dot(p, NE_AXIS), dot(p, NE_SIDE));
}

/** Distance to one ring's tube, at figure weight `w`. Writes the nearest-tube globals if it wins. */
float tubeRing(vec3 q, vec2 xz, float r, float k, float count, float w, float best) {
    float len = 0.28 + 0.19 * k;
    vec4 pet = chrysPetal(xz, count, 0.035 + 0.05 * k, len, max(0.5 - 0.06 * k, 0.18), uTwist, 0.25, 0.5);
    float wob = gChaos * sin(pet.y * 9.0 + pet.w * 2.3 + gT * 0.7);
    float d2 = pet.x + 0.012 * wob;
    float cup = (0.42 - 0.05 * k) * gCupK;
    float dy = q.y - cup * r * r - 0.02 * gChaos * sin(pet.w * 2.3 + pet.y * 6.0 + gT * 0.5);
    float d = length(vec2(d2, dy)) - NE_TUBE * mix(0.35, 1.0, w);
    if (d < best) {
        gK = k;
        gPet = pet;
        gCnt = count;
        gW = w;
    }
    return d;
}

float map(vec3 p) {
    vec3 q = toFlower(p);
    float best = 1e9;
    bool both = gWNew < 0.998;
    for (int i = 0; i < NE_MAX_RINGS; i++) {
        float k = float(i);
        if (k >= gRings) break;
        vec2 xz = chrysTurn(gRot[i]) * q.xz;
        float r = length(xz);
        // Cheap reject: a ring's tube lives within its own reach of the heart.
        if (r > 0.42 + 0.24 * k && best < r - 0.42 - 0.24 * k) continue;
        best = min(best, tubeRing(q, xz, r, k, gCntNew[i], gWNew, best));
        if (both) best = min(best, tubeRing(q, xz, r, k, gCntOld[i], 1.0 - gWNew, best));
    }
    return best;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The chasing light at the nearest tube, from the globals map() left: 0..1. */
float chaseAt(float trail) {
    float u = clamp(gPet.y, 0.0, 1.0);
    float along = gPet.z > 0.0 ? 0.5 * u : 1.0 - 0.5 * u;
    float id = mod(gPet.w, gCnt);
    float head = fract(gT * 0.3 + chrysHash1(id + 9.0 * gK));
    float behind = fract(head - along);
    return (1.0 - smoothstep(0.0, trail, behind)) * step(behind, trail);
}

vec3 ringColour(float k) {
    vec3 c = gCol[0];
    for (int i = 1; i < NE_MAX_RINGS; i++) {
        if (float(i) == k) c = gCol[i];
    }
    return c;
}

void main() {
    vec2 uv = view();

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE), 0.0, 1.5);
    float sectEnv = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    gT = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gChaos = clamp(uTurbulence + uWarp + kTexture, 0.0, 2.0);
    gRings = 4.0 + floor(2.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float trail = (0.04 + 0.3 * clamp(sndSpectrum(0.80) * uIntensity, 0.0, 1.5)) * clamp(1.0 + 1.2 * kFlow, 0.3, 1.8);
    trail = min(trail, 0.9);
    float hue = 0.55 + gT * 0.004 + 0.12 * kTone;

    gWNew = 1.0 - smoothstep(0.1, 0.85, sectEnv);
    float bump = chrysBump(sectEnv);

    gCupK = clamp(1.0 + kBuild, 0.5, 1.5) * (1.0 + 0.4 * bump * clamp(uIntensity, 0.0, 1.0));
    for (int i = 0; i < NE_MAX_RINGS; i++) {
        float k = float(i);
        float dir = mod(k, 2.0) < 0.5 ? 1.0 : -1.0;
        gRot[i] = dir * (uDrift * 0.12 + gT * 0.03) + k * 0.4;
        gCntNew[i] = neonCount(SND_CUT_COUNT, k);
        gCntOld[i] = neonCount(SND_CUT_COUNT - 1.0, k);
        gCol[i] = pal(hue + 0.24 * k);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, 3.4 - 1.0 * kSpace, 4.0 * CAM_SPIN_UNIT, ro, rd);

    // The dark void, with a faint wash of the flower's colour toward its heart.
    float toHeart = max(dot(rd, normalize(-ro)), 0.0);
    vec3 bg = vec3(0.004, 0.004, 0.012) + pal(hue + 0.5) * 0.02 * pow(toHeart, 6.0);

    // The backboard: faded out while the eye is behind its plane.
    float aro = dot(ro, NE_AXIS);
    float ard = dot(rd, NE_AXIS);
    float boardVis = smoothstep(0.05, 0.35, (aro - NE_BOARD) / max(length(ro), 1e-3));
    float tb = abs(ard) > 1e-4 ? (NE_BOARD - aro) / ard : -1.0;
    vec3 pb = ro + rd * tb;
    vec3 qb = toFlower(pb);
    float rb = length(qb.xz);
    float boardMask = tb > 0.0 ? boardVis * (1.0 - smoothstep(NE_BOARD_R - 0.3, NE_BOARD_R, rb)) : 0.0;

    vec3 glowFront = vec3(0.0);
    vec3 glowBack = vec3(0.0);
    vec3 col = bg;
    float hitT = -1.0;

    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - NE_BOUND * NE_BOUND);
    if (h > 0.0) {
        float sq = sqrt(h);
        float t = max(-b - sq, 0.0);
        float tEnd = -b + sq;
        for (int i = 0; i < NE_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (t > tEnd) break;
            float d = map(ro + rd * t);
            float eps = 0.0006 * t + 0.0006;
            if (d < eps) {
                hitT = t;
                break;
            }
            float stepLen = max(d * 0.6, 0.004);
            // The halo the tubes throw into the air, integrated along the ray
            // so it does not depend on how many steps were taken.
            vec3 tc = ringColour(gK);
            float dd = max(d, 0.0);
            float halo = 1.0 / (1.0 + (dd / 0.03) * (dd / 0.03));
            float tight = 1.0 / (1.0 + (dd / 0.008) * (dd / 0.008));
            vec3 g = tc * (2.2 * halo + 3.0 * tight) + mix(tc, vec3(1.0), 0.6) * 6.0 * chaseAt(trail) * tight;
            g *= gW * stepLen;
            if (boardMask > 0.0 && t > tb) {
                glowBack += g;
            } else {
                glowFront += g;
            }
            t += stepLen;
        }
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0008 * hitT + 0.0006);
        // Restore the hit tube's globals after the normal taps.
        map(p);
        vec3 tc = ringColour(gK);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float core = pow(ndv, 3.0);
        float rim = pow(1.0 - ndv, 2.0);
        vec3 tube = tc * (0.55 + 0.6 * core) + vec3(1.0) * 0.45 * core * core + tc * rim * 0.5;
        tube += mix(tc, vec3(1.0), 0.6) * 0.9 * chaseAt(trail);
        // A tube of the fading figure is thin and dim; one of the rising figure grows in.
        tube *= gW;
        // The far side of the flower sits a little deeper in the dark.
        float depth = hitT - length(ro);
        tube *= clamp(1.0 - 0.25 * depth, 0.5, 1.2);
        col = tube;
        if (boardMask > 0.0 && hitT > tb) col = mix(col, bg, boardMask);
    }

    // The board: dark lacquer, etched guide circles, and the light it catches
    // from the tubes nearest it (the ring bases and the heart) plus a pool
    // around the heart.
    if (boardMask > 0.0 && (hitT < 0.0 || hitT > tb)) {
        float dTube = map(pb);
        vec3 tc = ringColour(gK);
        vec3 board = vec3(0.006, 0.006, 0.014) + pal(hue + 0.5) * 0.03 * exp(-rb * 1.3);
        board += pal(hue) * 0.02 * (1.0 - smoothstep(0.0, 0.01, abs(fract(rb * 6.0) - 0.5) / 6.0 - 0.002));
        board += tc * gW * 0.012 / (dTube * dTube + 0.012);
        board += mix(pal(hue + 0.1), vec3(1.0), 0.4) * 0.12 * exp(-rb * rb * 18.0);
        col = mix(col, board, boardMask);
        glowBack *= 1.0 - boardMask;
    }

    col += glowFront + glowBack;

    // The heart: a small warm bulb, its glow taken from the ray's closest
    // approach to the origin (exact, and no march step needed).
    float tc0 = max(-dot(ro, rd), 0.0);
    float miss = length(ro + rd * tc0);
    float heartGlow = chrysGlow(miss, 0.03);
    if (boardMask > 0.0 && tc0 > tb) heartGlow *= 1.0 - boardMask;
    if (hitT > 0.0 && hitT < tc0) heartGlow *= 0.4;
    col += mix(pal(hue + 0.1), vec3(1.0), 0.5) * 0.5 * heartGlow;

    col *= 0.85 + clamp(0.15 + 0.25 * kPulse, 0.0, 0.4) * swell;
    fragColor = vec4(grade(col), 1.0);
}
