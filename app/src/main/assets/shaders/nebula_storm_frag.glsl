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
//#include lib_nebula

// Nebula Storm: a vast swirling storm of gas, a thick disk of cloud wound
// into three spiral bands around a clear eye, seen from above and to the
// side. The cloud churns; its tops catch a distant light; and inside it,
// lightning: on a snare a bolt fires somewhere in the cloud and lights the
// gas around it from within.
//
// Volume-marched, from a fixed vantage (camAround, spin 0). The storm's body
// is a flattened disk, tipped toward the camera, thickened along three log-spiral bands - cos(3 theta +
// k ln r - phase), a whole number of arms so the atan seam cancels - and
// eroded by 3D noise that is itself stirred by a slow time-varying swirl.
// The bands' phase advances, so they sweep round through the churning
// cloud: the storm turns, the frame does not. Lighting is one density tap
// toward the light (nebLightTap).
//
// ---- audio ------------------------------------------------------------------
//
// Snares (SND_SNARE_ENV) FIRE LIGHTNING INSIDE THE CLOUD: each onset counter value
// (floor(SND_HIT_TIME)) hashes a new position and direction in the storm, and on the
// snare a short bolt there lights the gas around it - a glow that is strong
// only within a few tenths of a unit of the bolt, so it is a flash in the
// cloud, never a flash of the frame. Bass (sndShaped(SND_LOW)) thickens the spiral
// bands. The key turns the cloud's hue. The cloud tops' light follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE))
// gently. The hats twinkle the stars.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the bands' sweep and the churn. Complexity: uMorph
// adds a noise octave (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence
// swirl the cloud further through nebWarp3; uTwist via view(). Colour: cloud,
// lit tops and lightning are pal(). Energy: uIntensity scales the bolt and the
// band thickening. Space: the rig (camAround, spin 0).

#define ST_CAM_R 3.4
#define ST_SIGMA 7.0

const vec3 ST_LIGHT = vec3(-0.3, 0.9, -0.3);
/** cos/sin of the 0.6 rad tip, as nebRot(0.6). */
const mat2 ST_TILT = mat2(0.8253356, 0.5646425, -0.5646425, 0.8253356);

float gT;
float gBand;
vec3 gBoltA;
vec3 gBoltB;

/** World to storm frame: the disk is tipped toward the camera so we look down onto its spiral. */
vec3 stormFrame(vec3 p) {
    p.yz = ST_TILT * p.yz;
    return p;
}

float density(vec3 p, float oct) {
    p = stormFrame(p);
    float rad = length(p.xz);
    float radial = smoothstep(0.14, 0.34, rad) * (1.0 - smoothstep(1.0, 1.6, rad));
    if (radial <= 0.0) return 0.0;
    float th = atan(p.z, p.x);
    float band = cos(3.0 * th + 3.2 * log(max(rad, 0.05)) - gT * 0.12);
    float h = (0.2 + 0.1 * band * (1.0 + gBand)) * (1.0 - 0.4 * smoothstep(0.6, 1.5, rad))
            + 0.16 * exp(-pow((rad - 0.36) / 0.1, 2.0));
    float vert = 1.0 - smoothstep(0.0, max(h, 0.02), abs(p.y));
    float shape = vert * radial * (0.65 + 0.35 * band);
    if (shape < 0.01) return 0.0;
    vec3 q = nebWarp3(p, gT) * 2.2;
    q += 0.35 * vec3(sin(q.y * 1.7 + gT * 0.21), sin(q.z * 1.3 - gT * 0.17), sin(q.x * 1.5 + gT * 0.19));
    float n = nebFbm3(q, oct);
    return smoothstep(0.3, 0.7, n + 0.35 * (shape - 0.5));
}

/** Distance from p to the bolt segment. */
float boltDist(vec3 p) {
    p = stormFrame(p);
    vec3 ab = gBoltB - gBoltA;
    float k = clamp(dot(p - gBoltA, ab) / dot(ab, ab), 0.0, 1.0);
    return length(p - gBoltA - ab * k);
}

void main() {
    vec2 uv = view();
    gT = nebTime();
    float e = nebEnergy();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float snare = clamp(SND_SNARE_ENV * e, 0.0, 1.0);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    gBand = 0.6 * clamp(sndShaped(SND_LOW), 0.0, 1.3) * e;
    float oct = nebVolOctaves(3.0);
    float key = nebKeyShift(0.12);

    // Where this onset's bolt is: a hashed point in the cloud and a direction.
    float seed = mod(floor(SND_HIT_TIME), 512.0);
    float a = NEB_TAU * nebHash1(seed);
    float rr = 0.45 + 0.7 * nebHash1(seed + 3.1);
    vec3 centre = vec3(cos(a) * rr, 0.08 * (nebHash1(seed + 5.7) - 0.5), sin(a) * rr);
    float b = NEB_TAU * nebHash1(seed + 9.3);
    vec3 bdir = normalize(vec3(cos(b), 0.5 * (nebHash1(seed + 1.9) - 0.5), sin(b)));
    gBoltA = centre - bdir * 0.22;
    gBoltB = centre + bdir * 0.22;

    vec3 ro;
    vec3 rd;
    camAround(uv, ST_CAM_R, 0.0, ro, rd);
    vec3 L = normalize(ST_LIGHT);

    vec3 body = nebEmission(0.62 + key, 0.0);
    vec3 tops = mix(nebEmission(0.12 + key, 0.4), vec3(1.0), 0.3) * (0.85 + 0.2 * swell);
    vec3 boltCol = mix(nebEmission(0.78 + key, 0.6), vec3(1.0), 0.45);

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    vec2 tb = nebSphere(ro, rd, vec3(0.0), 1.65);
    if (tb.y > 0.0) {
        float tt = tb.x + nebDither() * 0.035;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > tb.y) break;
            vec3 p = ro + rd * tt;
            float dt = nebVolDt(tt - tb.x, 0.022, 0.012);
            float dens = density(p, oct);
            if (dens > 0.002) {
                float lit = nebLightTap(density(p + L * 0.2, 2.0), 4.0);
                vec3 emit = body * 0.035 + tops * lit * 0.2;
                float bd = boltDist(p);
                emit += boltCol * snare * (exp(-bd * bd * 30.0) * 0.8 + exp(-bd * bd * 900.0) * 1.5);
                nebVolStep(acc, emit, dens * ST_SIGMA, dt);
                if (acc.a < NEB_TRANS_MIN) break;
            }
            tt += dt;
        }
    }

    vec3 bg = pal(0.68 + key) * 0.015 + nebStars(nebSky(rd), 6.0, 2, vec2(0.0), 1.0, 0.2, hats, gT);
    vec3 col = acc.rgb + acc.a * bg;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
