#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_chrys

// Chrysanthemum Corolla: one flower head in 3D, raymarched, orbited with
// camAround. A domed, floret-textured core, and around it three rings of
// thin curved petals. Each petal's centreline is a circular arc in its own
// (radius, height) half-plane, so one number - the arc's curvature - takes it
// from a bud petal curled up over the core to an open petal drooping at the
// tip, and the distance to an arc is exact and cheap (no iteration). The
// petals are thin enough to be lit from behind: a key light that falls on
// the far side of a petal glows through it.
//
// The head is tilted toward the camera's starting side, so the orbit sweeps
// from a face-on bloom round to its pale undersides and back.
//
// Budget: 112-step ceiling, broken on uSteps; one bounding-sphere test first
// so rays that miss the flower never march. Below uSteps 72 the inner ring
// and the near-miss glow are dropped (the low-quality path). Normals are the
// 4-tap tetrahedron.
//
// ---- audio ------------------------------------------------------------------
//
// The drop (uEvents.y) UNFOLDS the flower: across its 2.5 s envelope every
// ring's elevation and curvature swing from the half-open rest pose to a full
// bloom, the outer petals laid nearly flat with drooping tips, and fold back.
// chrysBump shapes it, so the unfolding starts from rest on the drop and
// never snaps. uSwell opens the rest pose a little, so loud passages sit
// fuller. The bass band breathes the core's radius by a few percent. The head
// turns on its own axis with uDrift. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the head's own spin and the hue clock. Complexity:
// uMorph adds a fourth, innermost ring (high-quality path only; the march
// ceiling is unchanged). Chaos: uTurbulence + uWarp give each petal its own
// length and lift, a shaggy, wind-tossed head; uTwist swirls the rings into
// a pinwheel around the axis. Energy: uIntensity scales the drop unfold and
// the bass breath. Colour: pal() for petals, core and backdrop. Camera: the
// rig, via camAround (uZoom still applies through view()).

#define CO_MAX_STEPS 112
#define CO_BOUND 1.8

const vec3 CO_AXIS = vec3(0.0, 0.5, -0.8660254);
const vec3 CO_SIDE = vec3(0.0, 0.8660254, 0.5);

float gOpen;
float gSpin;
float gBass;
bool gLowQ;
float gMat;
float gU;
float gChaos;
float gRings;

vec3 toFlower(vec3 p) {
    vec3 q = vec3(p.x, dot(p, CO_AXIS), dot(p, CO_SIDE));
    q.xz = chrysTurn(gSpin) * q.xz;
    return q;
}

// One ring of petals. Returns (distance, u along the petal).
vec2 petalRing(vec3 q, float count, float r0, float len, float width, float elev, float bend, float twist) {
    float rho = length(q.xz);
    float phi = atan(q.z, q.x) + twist + uTwist * rho;
    float sector = CHRYS_TAU / count;
    float id = floor(phi / sector + 0.5);
    float across = (phi - id * sector) * rho;
    float hid = chrysHash1(mod(id, count) + r0 * 31.0);
    float l = len * (0.9 + 0.2 * hid) * (1.0 + 0.2 * gChaos * (chrysHash1(hid * 7.0 + 1.0) - 0.5));
    elev += 0.25 * gChaos * (hid - 0.5);
    // The centreline: a circle of radius 1/bend leaving (r0, 0) at angle elev.
    float k = abs(bend) < 0.05 ? (bend < 0.0 ? -0.05 : 0.05) : bend;
    float rad = 1.0 / k;
    vec2 x = vec2(rho, q.y);
    vec2 p0 = vec2(r0, 0.0);
    vec2 c = p0 + rad * vec2(-sin(elev), cos(elev));
    vec2 v0 = p0 - c;
    vec2 vx = x - c;
    float th = atan(v0.x * vx.y - v0.y * vx.x, dot(v0, vx));
    float u = (th / k) / l;
    float dn = abs(length(vx) - abs(rad));
    float w = width * l * chrysProfile(u, 0.4, 0.6);
    w = min(w, 0.48 * sector * max(rho, 0.05));
    float d = max(max(dn - 0.011, abs(across) - w), (abs(u - 0.5) - 0.5) * l);
    return vec2(d, u);
}

float map(vec3 p) {
    vec3 q = toFlower(p);
    float core = (length(vec3(q.x, q.y * 1.6, q.z)) - 0.3 * (1.0 + 0.06 * gBass)) / 1.6;
    core += 0.006 * sin(q.x * 42.0) * sin(q.z * 42.0);
    float d = core;
    gMat = 0.0;
    gU = 0.0;
    float rings = gLowQ ? 2.0 : gRings;
    for (int i = 0; i < 4; i++) {
        float k = float(i);
        if (k >= rings) break;
        float t = min(k * 0.5, 1.0);
        float elev = mix(mix(1.2, 1.5, t), mix(0.1, 0.95, t), gOpen);
        float bend = mix(mix(1.5, 2.6, t), mix(-0.6, -0.15, t), gOpen);
        float count = max(16.0 - 3.0 * k, 6.0);
        vec2 pr = petalRing(q, count, 0.26 - 0.045 * k, max(1.3 - 0.3 * k, 0.45), 0.42 + 0.04 * k, elev, bend, k * 0.37);
        if (pr.x < d) {
            d = pr.x;
            gMat = 1.0 + k;
            gU = pr.y;
        }
    }
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float t = uTime * uSpeed;
    float drop = clamp(uEvents.y, 0.0, 1.0);
    gBass = clamp(uBandsLo.y * uIntensity, 0.0, 1.5);
    gOpen = clamp(0.36 + 0.12 * swell + 0.6 * uIntensity * chrysBump(drop), 0.0, 1.0);
    gSpin = t * 0.04 + uDrift * 0.1;
    gLowQ = uSteps < 72.0;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    gRings = 3.0 + step(0.5, uMorph);
    float hue = 0.04 + 0.03 * sin(t * 0.025);

    vec3 ro;
    vec3 rd;
    camAround(uv, 3.6, 6.0 * CAM_SPIN_UNIT, ro, rd);

    vec3 bg = pal(hue + 0.6) * 0.025 + pal(hue + 0.52) * 0.09 * exp(-length(uv) * 1.4);
    vec3 col = bg;

    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - CO_BOUND * CO_BOUND);
    if (h > 0.0) {
        float sq = sqrt(h);
        float t = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        float used = 0.0;
        float glow = 0.0;
        for (int i = 0; i < CO_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tEnd) break;
            used = float(i);
            float d = map(ro + rd * t);
            float eps = 0.0008 * t + 0.0005;
            if (d < eps) {
                hitT = t;
                break;
            }
            if (!gLowQ) glow += exp(-d * 30.0) * 0.006;
            t += max(d * 0.6, eps);
        }
        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
            map(p);
            float mat = gMat;
            float u = clamp(gU, 0.0, 1.0);
            // A petal has two faces; light the one we are looking at.
            vec3 nf = dot(n, rd) > 0.0 ? -n : n;
            vec3 lightDir = normalize(vec3(0.45, 0.85, -0.3));
            float diff = max(dot(nf, lightDir), 0.0);
            float through = max(dot(-nf, lightDir), 0.0);
            float spec = pow(max(dot(reflect(rd, nf), lightDir), 0.0), 20.0);
            float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
            if (mat < 0.5) {
                vec3 q = toFlower(p);
                float speck = 0.5 + 0.5 * sin(q.x * 42.0) * sin(q.z * 42.0);
                col = mix(pal(hue + 0.12) * 0.5, vec3(1.0, 0.85, 0.5), 0.4) * (0.45 + 0.55 * diff) * (0.75 + 0.25 * speck);
                col += pal(hue + 0.1) * 0.15;
            } else {
                float ring = mat - 1.0;
                vec3 base = pal(hue + 0.05 * ring) * 0.4;
                vec3 tipc = mix(pal(hue + 0.05 * ring + 0.08), vec3(1.0, 0.94, 0.9), 0.3);
                vec3 pc = mix(base, tipc, smoothstep(0.0, 0.9, u));
                col = pc * (0.25 + 0.85 * diff) + pc * through * 0.55 + vec3(spec) * 0.12;
            }
            col *= ao;
            col = mix(col, bg, smoothstep(2.0, 6.0, hitT) * 0.5);
        }
        col += pal(hue + 0.08) * glow;
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
