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
//#include lib_nebula

// Nebula Pillars: three towering columns of cold dust rising out of the
// bottom of the frame, lit from behind by a young star cluster hidden beyond
// them. Their bodies are dark; their edges and heads blaze where starlight
// gets through the thin dust and scatters toward us; their light-facing skins
// glow with the ionisation front that is slowly eating them; behind them the
// cluster lights a wall of emission gas.
//
// Volume-marched. Each pillar is a tapered, leaning, lumpy capsule with a
// bulbous head, eroded by four octaves of 3D noise into a soft-edged column of
// dust. The camera is fixed on the group (camAround, spin 0): the rig's look
// and dolly still apply, but nothing orbits. Light is faked with ONE extra
// density tap toward the cluster (nebLightTap) and a strong forward-scattering
// phase (nebPhase), which is what makes a backlit edge glow while the core
// stays black. Empty space is skipped by the capsules' own distance, so most
// of the budget is spent inside the dust.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBassSmooth with the bass band region) FEEDS THE IONISATION FRONT:
// the lit skins of the pillars - their heads and light-facing edges - glow
// hotter on the low end, so a bass line makes the tops of the columns
// smoulder while the dark bodies hold still. The cluster's light follows
// uSwell mildly. The key turns the emission gas behind and the front's hue.
// The hats twinkle the background stars.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow erosion drift and the twinkle. Complexity:
// uMorph adds a noise octave to the dust (nebVolOctaves, at most four).
// Chaos: uWarp/uTurbulence swirl the dust through nebWarp3; uTwist via
// view(). Colour: front, gas and dust tint are pal(). Energy: uIntensity
// scales the bass feed. Space: the rig (camAround, spin 0).

#define PL_CAM_R 3.6
#define PL_BASE -2.3
#define PL_SIGMA 10.0

const vec3 PL_LIGHT = vec3(0.35, 1.3, 3.6);
const vec3 PL_CENTRE = vec3(0.0, -0.7, 0.1);

float gT;
float gOct;

/** A tapered, leaning capsule with a swollen head. Negative inside, roughly a distance. */
float column(vec3 p, vec3 base, float h, float r0, float r1, float lean) {
    float y = clamp(p.y - base.y, 0.0, h);
    float k = y / h;
    vec3 axisPt = vec3(base.x + lean * k * k, base.y + y, base.z);
    float rad = mix(r0, r1, k) * (1.0 + 0.12 * sin(p.y * 3.1 + base.x * 5.0)) + 0.08 * smoothstep(0.72, 1.0, k);
    return length(p - axisPt) - rad;
}

float shapeDist(vec3 p) {
    float a = column(p, vec3(-0.72, PL_BASE, 0.25), 2.25, 0.36, 0.15, 0.10);
    float b = column(p, vec3(0.02, PL_BASE, -0.1), 3.15, 0.30, 0.13, -0.16);
    float c = column(p, vec3(0.68, PL_BASE, 0.35), 2.5, 0.22, 0.11, 0.12);
    return min(a, min(b, c));
}

/** Dust density 0..1 and, through sd, the capsules' distance (for skipping empty space). */
float density(vec3 p, float oct, out float sd) {
    sd = shapeDist(p);
    if (sd > 0.32) return 0.0;
    vec3 q = nebWarp3(p, gT) * 2.4 + vec3(0.0, 0.0, -gT * 0.02);
    float n = nebFbm3(q, oct);
    float d = sd + (n - 0.5) * 0.5;
    return 1.0 - smoothstep(-0.12, 0.06, d);
}

void main() {
    vec2 uv = view();
    gT = nebTime();
    float e = nebEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(0.6 * uBassSmooth + 0.4 * uBandsLo.y, 0.0, 1.3) * e;
    float hats = clamp(uHits.z, 0.0, 1.0);
    float key = nebKeyShift(0.12);
    gOct = nebVolOctaves(3.0);

    vec3 ro;
    vec3 rd;
    camAround(uv, PL_CAM_R, 0.0, ro, rd);

    vec3 lightCol = mix(vec3(1.0, 0.93, 0.82), nebEmission(0.12 + key, 0.6), 0.3) * (0.85 + 0.25 * swell);
    vec3 front = nebEmission(0.08 + key, 0.35);
    vec3 dustAmb = mix(vec3(0.05, 0.035, 0.03), pal(0.6 + key) * 0.04, 0.4);

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    vec2 tb = nebSphere(ro, rd, PL_CENTRE, 2.3);
    if (tb.y > 0.0) {
        float tt = tb.x + nebDither() * 0.04;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > tb.y) break;
            vec3 p = ro + rd * tt;
            float sd;
            float dens = density(p, gOct, sd);
            float dt = nebVolDt(tt - tb.x, 0.025, 0.012);
            if (dens > 0.002) {
                vec3 L = normalize(PL_LIGHT - p);
                float sdL;
                float dl = density(p + L * 0.25, 2.0, sdL);
                float lit = nebLightTap(dl, 5.0);
                float ph = nebPhase(dot(rd, L), 0.55);
                float skin = smoothstep(0.03, 0.35, dens) * (1.0 - smoothstep(0.35, 0.9, dens));
                vec3 emit = dustAmb + lightCol * lit * (0.04 + 0.03 * ph);
                emit += front * lit * skin * (0.25 + 0.7 * bass);
                nebVolStep(acc, emit, dens * PL_SIGMA, dt);
                if (acc.a < NEB_TRANS_MIN) break;
            } else {
                dt = max(dt, (sd - 0.32) * 0.8);
            }
            tt += dt;
        }
    }

    // Behind the pillars: the cluster and the gas wall it lights.
    vec3 Lc = normalize(PL_LIGHT - PL_CENTRE);
    float cosL = max(dot(rd, Lc), 0.0);
    vec2 sky = nebSky(rd);
    float wall = nebFbm2(sky * 2.0 + vec2(3.3, 1.1), 3.0);
    vec3 bg = nebEmission(0.5 + key, 0.1) * (0.05 + 0.4 * pow(cosL, 6.0)) * (0.5 + 0.9 * wall);
    bg += lightCol * (pow(cosL, 400.0) * 2.0 + pow(cosL, 40.0) * 0.25);
    bg += nebStars(sky, 6.0, 2, vec2(0.0), 1.0, 0.2, hats, gT);

    vec3 col = acc.rgb + acc.a * bg;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
