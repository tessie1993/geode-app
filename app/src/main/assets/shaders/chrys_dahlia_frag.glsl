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

// Chrysanthemum Dahlia: the spiral cactus-dahlia, now a real ball of quills,
// raymarched and orbited close enough that it fills the frame. Eight rings
// of rolled, pointed tube petals stand on a small round heart. Each ring's
// bases sit further down the heart than the one inside it, so the inner
// quills stand upright and curled shut while the outer ones lie out and
// reflex back below the equator, the way a cactus dahlia makes a globe.
// Every quill is swept around the head along a spiral, so seen face-on the
// arms still wind into the centre as the flat style's did. The petals are
// true tubes now (a round cross-section around a straight centreline that
// thickens fast and tapers to a point), so the light rolls across each one
// on its own. Two-tone: a deep base climbing to a pale, almost white tip.
//
// The camera is the shared rig in object-centred mode (camAround): the eye
// circles the head at four turns per 1024 world units of travel, so the
// rig's 8192 wrap lands on a whole number of turns and never shows. The
// head's axis leans toward the camera's starting side, so the orbit sweeps
// from the face-on spiral round to the reflexed outer quills and back. The
// scene has no z term at all, so there is nothing else to wrap.
//
// Budget: a 112-step ceiling broken on uSteps (the user's Detail), behind a
// bounding-sphere test so rays that miss the head never march. Inside map()
// a ring whose meridian-plane bound cannot beat the nearest surface so far
// is skipped, so most taps pay for two or three rings, not eleven. Below
// uSteps 64 the ambient-occlusion taps are dropped. Normals are the 4-tap
// tetrahedron.
//
// ---- audio ------------------------------------------------------------------
//
// The mid band CURLS the flower: SND_MID_PRESENCE (slew-limited) blended with
// the low-mid/mid regions sets how far each quill sweeps around the head
// along its spiral, so a vocal or a lead line visibly winds the arms tighter
// and a sparse passage lets them straighten into a starburst. Inner rings
// curl more than outer ones, so the winding has a centre. The head turns on
// uDrift. Treble (SND_TREBLE_PRESENCE) moves the specular streak across the
// roll (a geometry of the light, not its level). Brightness follows
// SND_INTENSITY only.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime drives the turn and hue clocks (already Speed-integrated).
// Complexity: uMorph adds up to three more, smaller rings inside the head
// (11 at most), packed tight at the heart. Chaos: uTurbulence + uWarp vary
// quill lengths and fray their surfaces; uTwist adds to the spiral curl.
// Energy: uIntensity scales how far the mid band winds the arms. Colour:
// pal() throughout. Space: uZoom via view().
// Knobs (each neutral at 0.5): k0 PULSE scales the mid band's curl, k1
// TEXTURE the fray, k3 TONE the hue, k4 BUILD how far the outer quills
// reflex, k5 SPACE the orbit radius.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The head is evaluated in cylindrical coordinates (rho, y, phi) about its
// own axis. Each ring folds phi into one sector per quill, and a quill's
// radius is clamped to under half its sector, so no quill reaches past its
// own fold. The quill itself is a tapered capsule around a segment in the
// (rho, y) meridian plane, whose radius changes slowly along it (slope at
// most ~0.07), so its distance is a near-exact bound. The spiral is a domain
// warp phi -= curl * (rho - rho0): it stretches the across-sector coordinate
// by at most sqrt(1 + (curl * rho)^2), and each ring divides its distance by
// exactly that factor. The ring skip uses the distance from the point to the
// ring's base circle in the meridian plane minus the quill's longest reach;
// distance in 3D to a solid of revolution's set is never less than that. The
// march still takes 0.75 of each step for the per-quill length jitter, and
// a sphere around the eye is carved out of everything.

#define DH_MAX_STEPS 112
#define DH_MAX_RINGS 11
/** Bounding sphere of the head: heart + the longest jittered quill + margin. */
#define DH_BOUND 2.1
/** The heart's radius; the quills' bases sit just inside it. */
#define DH_HEART 0.42
#define DH_BUBBLE 0.3

const vec3 DH_AXIS = vec3(0.0, 0.5, -0.8660254);
const vec3 DH_SIDE = vec3(0.0, 0.8660254, 0.5);

float gSpin;
float gMid;
float gChaos;
float gFray;
float gOpen;
float gExtra;
float gRings;
float gT;
vec3 gEye;
float gMat;
float gU;
float gV;
float gF;

vec3 toFlower(vec3 p) {
    // An orthonormal frame (x, axis, side), then the head's own turn: a pure
    // rotation, so distances measured in q are distances in the world.
    vec3 q = vec3(p.x, dot(p, DH_AXIS), dot(p, DH_SIDE));
    q.xz = chrysTurn(gSpin) * q.xz;
    return q;
}

float map(vec3 p) {
    vec3 q = toFlower(p);
    float rho = length(q.xz);
    float phi = atan(q.z, q.x);
    vec2 x = vec2(rho, q.y);
    float d = length(q) - DH_HEART;
    gMat = 0.0;
    gU = 0.0;
    gV = 0.0;
    gF = 0.0;
    for (int i = 0; i < DH_MAX_RINGS; i++) {
        float k = float(i);
        if (k >= gRings) break;
        // Counted from the inside; the extra rings get fk < 0 and pack in at
        // the heart, so Complexity densifies the centre, not the rim.
        float kk = k - gExtra;
        float fk = kk / 7.0;
        float f = clamp(fk, 0.0, 1.0);
        float theta = fk >= 0.0 ? 0.18 + (1.3 + 0.45 * gOpen) * fk : 0.18 * (1.0 + 1.6 * fk);
        vec2 base = (DH_HEART * 0.92) * vec2(sin(theta), cos(theta));
        float alpha = theta + mix(-0.14, 0.12 + 0.35 * gOpen, f);
        vec2 dir = vec2(sin(alpha), cos(alpha));
        float len = mix(0.3, 1.05, f) * (1.0 + 0.6 * min(fk, 0.0));
        float reach = len * (1.0 + 0.16 * gChaos) + 0.08;
        vec2 rel = x - base;
        if (length(rel) - reach > d) continue;

        float count = max(9.0 + 4.0 * kk, 5.0);
        float curl = (0.25 + 1.4 * gMid) * mix(1.6, 0.55, f) + 0.6 * uTwist;
        float a = phi + kk * CHRYS_GOLDEN - curl * (rho - base.x);
        float sector = CHRYS_TAU / count;
        float id = floor(a / sector + 0.5);
        float across = (a - id * sector) * rho;
        float lenJ = len * (1.0 + 0.16 * gChaos * chrysWobble(mod(id, count) + 3.0 * k, gT));
        float s = dot(rel, dir);
        float sc = clamp(s, 0.0, lenJ);
        float u = sc / lenJ;
        vec2 perp = rel - dir * sc;
        float w = 0.07 * lenJ * chrysProfile(u, 0.15, 0.85);
        w = max(min(w, 0.45 * sector * max(rho, 0.02)), 0.002);
        float pd = length(vec3(across, perp)) - w;
        pd += chrysFray(vec2(across, s), gFray);
        pd /= sqrt(1.0 + curl * curl * rho * rho);
        if (pd < d) {
            d = pd;
            gMat = 1.0;
            gU = u;
            gV = across / w;
            gF = f;
        }
    }
    return max(d, DH_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float kPulse = clamp(uKnob[0], 0.0, 1.0);
    float kTex = clamp(uKnob[1], 0.0, 1.0);
    float kTone = uKnob[3] - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0);
    float kSpace = clamp(uKnob[5], 0.0, 1.0);

    float mid = (0.6 * SND_MID_PRESENCE + 0.4 * mix(sndShaped(SND_LOW), sndSpectrum(0.57), 0.6)) * uIntensity;
    gMid = clamp(mid * (0.5 + kPulse), 0.0, 1.6);
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    gFray = gChaos * (0.5 + kTex);
    gOpen = clamp(0.5 + 0.8 * (kBuild - 0.5), 0.0, 1.0);
    gExtra = floor(3.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    gRings = 8.0 + gExtra;
    gSpin = uDrift * 0.25 + t * 0.015;
    gT = t;
    float hue = 0.92 + 0.03 * sin(t * 0.04) + 0.1 * kTone;

    vec3 ro;
    vec3 rd;
    camAround(uv, mix(3.2, 3.9, kSpace), 4.0 * CAM_SPIN_UNIT, ro, rd);
    gEye = ro;

    float r = length(uv);
    vec3 bg = pal(hue + 0.4) * 0.025 * (1.0 + exp(-r)) + pal(hue + 0.5) * 0.05 * exp(-r * 1.6);
    vec3 col = bg;

    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - DH_BOUND * DH_BOUND);
    if (h > 0.0) {
        float sq = sqrt(h);
        float tm = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        for (int i = 0; i < DH_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            float d = map(ro + rd * tm);
            float eps = 0.0008 * tm + 0.0005;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.75, eps);
        }
        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
            float ao = 1.0;
            if (uSteps >= 64.0) {
                float occ = 0.0;
                occ += (0.05 - map(p + n * 0.05));
                occ += (0.14 - map(p + n * 0.14)) * 0.6;
                ao = clamp(1.0 - 3.0 * occ, 0.3, 1.0);
            }
            // Restore the hit's material after the normal and AO taps.
            map(p);
            vec3 nf = dot(n, rd) > 0.0 ? -n : n;

            // The key light hangs at the viewer's upper left, as the flat
            // style's did, whichever side of the head the orbit is on.
            vec3 fwd = normalize(-ro);
            vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd) + vec3(1e-4, 0.0, 0.0));
            vec3 up = cross(fwd, right);
            vec3 lightDir = normalize(-0.6 * right + 0.8 * up - 0.5 * fwd);
            float diff = max(dot(nf, lightDir), 0.0);
            float spec = pow(max(dot(reflect(rd, nf), lightDir), 0.0), 24.0);
            float fres = pow(1.0 - clamp(dot(nf, -rd), 0.0, 1.0), 3.0);

            if (gMat < 0.5) {
                // A dark, tight heart: the youngest petals are still curled shut.
                vec3 hq = toFlower(p);
                float speck = 0.5 + 0.5 * sin(hq.x * 48.0) * sin(hq.z * 48.0);
                col = pal(hue + 0.02) * 0.08 * (0.5 + 0.5 * diff) * (0.7 + 0.3 * speck);
                col += pal(hue + 0.5) * 0.05;
            } else {
                float u = clamp(gU, 0.0, 1.0);
                float v = clamp(gV, -1.0, 1.0);
                float f = gF;
                vec3 base = pal(hue + 0.05 * f) * 0.35;
                vec3 tipc = mix(pal(hue + 0.08 + 0.05 * f), vec3(1.0, 0.95, 0.92), 0.45);
                vec3 pc = mix(base, tipc, smoothstep(0.05, 1.0, u));
                // Where a quill tucks into the ring above, it sits in shadow.
                float tuck = mix(0.5, 1.0, smoothstep(0.0, 0.35, u));
                col = pc * (0.3 + 0.8 * diff) * tuck;
                // Treble slides the streak across the roll.
                float streakAt = 0.35 - 0.5 * clamp(SND_TREBLE_PRESENCE, 0.0, 1.0);
                float streak = exp(-pow((v - streakAt) * 4.0, 2.0)) * smoothstep(0.1, 0.8, u);
                col += vec3(0.9, 0.9, 1.0) * (streak * 0.12 + spec * 0.14);
                col += tipc * fres * 0.12;
            }
            col *= ao;
            col = mix(col, bg, smoothstep(2.6, 6.0, hitT) * 0.5);
        }
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
