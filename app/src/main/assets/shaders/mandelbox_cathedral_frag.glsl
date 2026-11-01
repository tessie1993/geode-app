#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade

// uKnob[6], the six style knobs read below, is declared in lib_scene_uniforms.

// Mandelbox Cathedral: a flight down the nave of an endless Mandelbox vault,
// its surfaces iridescent glass-chrome laced with neon filigree from the
// fractal's own orbit, the air full of soft coloured glow.
//
// ---- knobs ------------------------------------------------------------------
//
//   k0  filigree    PULSE    the orbit-trap lines blaze brighter and wider
//   k1  lace        TEXTURE  finer, denser filigree lines on every surface
//   k2  fold        FLOW     the fold scale breathes: the vaults reshape
//   k3  hue         TONE     turns the neon ramp of filigree and film
//   k4  haze        BUILD    the volumetric glow in the air thickens
//   k5  lens        SPACE    the lens widens from a tight telephoto to a wide angle
//
// Provenance: written from scratch for MuzViz from general techniques - the
// Mandelbox distance estimator (box fold, sphere fold with a minimum and fixed
// radius, scale and offset, |p| / |dr|); domain repetition along z before the
// folds, so the vault is endless; a cylinder carved out of it for the nave;
// orbit traps (the minimum of |p.x|, |p.y|, |p.z| and of a shell |len(p) - k|
// over the iterations) turned into thin lines; step-count ambient occlusion;
// glow accumulated along the ray from exp(-k d); and a cosine thin-film tint.
// No code was taken from any published shader.
//
// The repetition period is 8 world units along z (a power of two), so the rig's
// 8192 travel wrap is invisible; the eye's z is rebased into one tile.

#define MC_MAX_STEPS 110
#define MC_ITER 9
#define MC_FAR 26.0
#ifndef MC_WORLD
#define MC_WORLD 0.5
#endif
#ifndef MC_S0
#define MC_S0 -1.6
#endif
#ifndef MC_C
#define MC_C vec3(1.0)
#endif
#ifndef MC_NAVE
#define MC_NAVE 0.0
#endif
#define MC_TAU 6.2831853

float gScale;
float gNave;
float gTone;
vec4 gTrap;

/**
 * The neon ramp: electric blue -> violet -> magenta -> orange -> acid green
 * and back, as five fixed stops blended smoothly, then pulled a little toward
 * the user's palette. Fixed stops rather than a cosine wheel, which passes
 * through greys and browns between its primaries.
 */
vec3 neon(float t) {
    float x = fract(t + gTone) * 5.0;
    vec3 s0 = vec3(0.05, 0.30, 1.00);
    vec3 s1 = vec3(0.50, 0.08, 1.00);
    vec3 s2 = vec3(1.00, 0.06, 0.62);
    vec3 s3 = vec3(1.00, 0.42, 0.03);
    vec3 s4 = vec3(0.30, 1.00, 0.18);
    float f = smoothstep(0.0, 1.0, fract(x));
    vec3 a = x < 1.0 ? s0 : x < 2.0 ? s1 : x < 3.0 ? s2 : x < 4.0 ? s3 : s4;
    vec3 b = x < 1.0 ? s1 : x < 2.0 ? s2 : x < 3.0 ? s3 : x < 4.0 ? s4 : s0;
    return mix(mix(a, b, f), pal(t + gTone), 0.2);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float map(vec3 pw) {
    // World to fractal space, repeated every 2 fractal units (8 world) along z.
    vec3 p = pw * MC_WORLD;
    p = mod(p + 1.0, 2.0) - 1.0;
    vec3 c = MC_C;
    float dr = 1.0;
    vec4 trap = vec4(1e3);
    for (int i = 0; i < MC_ITER; i++) {
        p = clamp(p, -1.0, 1.0) * 2.0 - p;
        float r2 = dot(p, p);
        float k = clamp(1.0 / max(r2, 0.25), 1.0, 4.0);
        // min radius^2 0.25, fixed radius^2 1: inside 0.25 scale by 4, then 1/r2.
        p *= k;
        dr *= k;
        p = p * gScale + c;
        dr = dr * abs(gScale) + 1.0;
        trap = min(trap, vec4(abs(p), abs(length(p) - 1.6)));
    }
    gTrap = trap;
    float d = length(p) / abs(dr) / MC_WORLD;
    // The nave: a corridor down the z axis, gently waving.
    vec2 axis = pw.xy - vec2(0.35 * sin(pw.z * MC_TAU / 8.0), 0.25 * sin(pw.z * MC_TAU / 16.0));
    float nave = gNave - length(axis * vec2(1.0, 0.8));
    return max(d, nave * MC_NAVE);
}

vec3 calcNormal(vec3 p, float t) {
    vec2 e = vec2(0.0008 * (1.0 + t), -0.0008 * (1.0 + t));
    return normalize(e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx));
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, 8.0);
    // SPACE is the lens: telephoto (flat, ornate) to wide (deep, sweeping).
    rd = normalize(uCamBasis * vec3(uv, uCamFocal * mix(1.5, 0.6, clamp(uKnob[5], 0.0, 1.0))));

    float kPulse = clamp(uKnob[0], 0.0, 1.0);
    float kTex = clamp(uKnob[1], 0.0, 1.0);
    float kFlow = clamp(uKnob[2], 0.0, 1.0);
    float kTone = clamp(uKnob[3], 0.0, 1.0);
    float kBuild = clamp(uKnob[4], 0.0, 1.0);
    float kSpace = clamp(uKnob[5], 0.0, 1.0);
    gScale = MC_S0 - 0.22 * kFlow + 0.03 * sin(uTime * 0.13);
    gNave = mix(0.9, 1.6, kSpace);
    // TONE turns the ramp by a quarter at most: enough to move every hue, not
    // so far that the film drifts into the olive end of the ramp.
    gTone = kTone * 0.25 - 0.08;
    // Keep the eye inside the nave.
    ro.xy = clamp(ro.xy, vec2(-0.4), vec2(0.4));

    float t = 0.02;
    bool hit = false;
    int steps = 0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < MC_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        steps = i;
        // Glow in the air near surfaces, coloured by the orbit there.
        float fil = exp(-min(min(gTrap.x, gTrap.y), gTrap.w) * 14.0);
        glow += neon(0.2 + gTrap.w * 0.8 + 0.1 * t) * exp(-d * 9.0) * (0.25 + fil) * exp(-t * 0.12);
        if (d < 0.0018 * (1.0 + t)) {
            hit = true;
            break;
        }
        t += d * 0.85;
        if (t > MC_FAR) break;
    }

    vec3 fogCol = vec3(0.04, 0.008, 0.09);
    vec3 col = fogCol;
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p, t);
        map(p);
        vec4 tr = gTrap;
        float cosV = max(dot(n, -rd), 0.0);
        float fres = 0.08 + 0.92 * pow(1.0 - cosV, 4.0);
        // Thin-film glass-chrome: interference over the view angle, the film
        // thickness taken from the orbit so every vault bands differently.
        float film = 1.0 + 2.5 * tr.w + 1.2 * tr.y;
        // Interference through the neon ramp (not an RGB cosine), so the film
        // stays saturated neon at every thickness instead of passing beige.
        // A triangle wave keeps the film inside cyan -> blue -> violet ->
        // magenta: dark orange reads as brown and orange-into-green as olive,
        // so those stay for the bright filigree only.
        float fv = film * (0.5 + cosV) * 0.45;
        vec3 irid = neon(0.5 * abs(2.0 * fract(fv) - 1.0));
        vec3 refl = reflect(rd, n);
        float env = 0.3 + 0.7 * max(refl.z, 0.0) + 0.3 * refl.y;
        float ao = clamp(1.0 - float(steps) / 90.0, 0.15, 1.0);
        ao *= ao;
        vec3 surf = (vec3(0.03, 0.01, 0.06) + irid * (0.25 + 0.5 * env) * (0.35 + 1.0 * fres)) * ao;
        // Neon filigree from the orbit traps: thin lines where the orbit grazes
        // the axis planes and the shell.
        float sharp = mix(1.0, 2.2, kTex) * mix(1.0, 0.6, kPulse);
        float lx = exp(-tr.x * 30.0 * sharp);
        float lw = exp(-tr.w * 22.0 * sharp);
        float ly = exp(-tr.z * 40.0 * sharp);
        vec3 fil = neon(0.15 + tr.w * 1.2) * lx + neon(0.55 + tr.x) * lw + neon(0.85) * ly;
        surf += fil * (0.45 + 1.6 * kPulse) * ao;
        // Glass-chrome highlight from a light down the nave.
        float spec = pow(max(dot(refl, normalize(vec3(0.3, 0.5, 1.0))), 0.0), 40.0);
        surf += mix(neon(0.1), vec3(1.0), 0.6) * spec * 0.8 * ao;
        col = surf;
        float fog = 1.0 - exp(-t * 0.09);
        col = mix(col, fogCol, fog);
    }
    col += glow * mix(0.003, 0.012, kBuild);

    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = aces(col * 1.2);
    fragColor = vec4(grade(col), 1.0);
}
