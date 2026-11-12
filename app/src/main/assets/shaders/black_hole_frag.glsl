#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the disc and the sky (lib_neon), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon
//#include lib_sdf3

// Black Hole: a lensed neon singularity. A Schwarzschild hole wears an
// accretion disc woven from glowing filaments and log-spiral arms, wrapped by
// the photon ring, over a dense neon filigree nebula that the hole bends into
// rings and arcs around itself. Inside the shadow, a dim mandala lattice of
// the light still circling the hole keeps the centre from going flat black.
//
// ---- what moves it -----------------------------------------------------------
//
// The style was drawn around six looks (flare, filaments, swirl, hue,
// charge, reach); each is now driven by the shared controls (lib_neon) or the
// sound, so Customize, the macros, the director and the DNA all reach it:
//
// - flare: SND_KICK_ENV, and SND_SNARE_ENV a little less, flash the inner
//   disc and the photon ring white-hot, and each kick sends a shockwave
//   ring out from the photon sphere, lensed like everything else.
// - filaments (Morph, neonForm): the disc's threads sharpen and brighten.
//   A hard cut (SND_CUT_COUNT) changes how many spiral arms there are.
// - swirl (Warp, neonFlow, plus the bass weight): the disc winds on and the
//   haze above it thickens. The disc turns on SND_BASS_TIME, so it races in
//   a heavy passage and drifts in a thin one, never backwards.
// - Turbulence (neonChaos) sets the disc precessing: its tilt wobbles.
// - hue: the key (neonKeyTurn) turns the ramp; SND_CHORD_HUE tints the disc.
// - charge: SND_BUILD_UP brightens the disc and wakes the sky swirl; the
//   drop (SND_HELD_SURGE) blazes it and pulls the eye in toward the hole.
// - reach: in silence (neonQuiet) the eye drifts back, wider and calmer.
// - SND_HAT_ENV twinkles the stars. Intensity (neonGlow) is the emission.
//
// Provenance: written from scratch for BeatRealms from textbook techniques - the
// photon geodesic in Cartesian form (a = -3/2 h^2 x / r^5, units where the
// Schwarzschild radius is 1), integrated with semi-implicit Euler at a step
// proportional to r; a thin disc found by plane crossings between steps; the
// relativistic Doppler factor D = 1 / (gamma (1 - v.n)) with I ~ D^3 (and a hue
// shift toward blue on the approaching side); and contour lines of a value-noise
// fbm for the filaments. No code was taken from any published shader.
//
// Every ray is bent, so the disc behind the hole is seen lensed over the top
// and under the bottom of the shadow, and rays that skim the photon sphere
// (r = 1.5) pile up into the photon ring. Rays that escape read the sky in
// their FINAL direction, so the swirl and stars around the shadow are lensed.

/** Steps per ray: the compile-time ceiling. uSteps is the runtime budget. */
#define BH_MAX_STEPS 120
#define BH_DISC_IN 2.3
#define BH_DISC_OUT 24.0
#define BH_ESCAPE 48.0

float bhNoise3(vec3 p) {
    vec3 f = floor(p);
    vec3 t = p - f;
    t = t * t * (3.0 - 2.0 * t);
    ivec3 i = ivec3(f);
    float a = hashCell(i);
    float b = hashCell(i + ivec3(1, 0, 0));
    float c = hashCell(i + ivec3(0, 1, 0));
    float d = hashCell(i + ivec3(1, 1, 0));
    float e = hashCell(i + ivec3(0, 0, 1));
    float g = hashCell(i + ivec3(1, 0, 1));
    float h = hashCell(i + ivec3(0, 1, 1));
    float k = hashCell(i + ivec3(1, 1, 1));
    return mix(mix(mix(a, b, t.x), mix(c, d, t.x), t.y), mix(mix(e, g, t.x), mix(h, k, t.x), t.y), t.z);
}

float bhFbm(vec3 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * bhNoise3(p);
        p = p * 2.03 + vec3(5.1, 1.7, 9.3);
        a *= 0.5;
    }
    return s / 0.9375;
}

float gTone;
float gWake;
vec3 gAxis;
float gArms;

/**
 * The neon ramp: electric blue -> violet -> magenta -> orange -> acid green
 * and back, as five fixed stops blended smoothly, then pulled a little toward
 * the user's palette so the colour controls still move it. Fixed stops rather
 * than a cosine wheel: a cosine wheel passes through pastel greys and browns
 * between its primaries, which is what made the first version look washed.
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
    return neonPal(mix(a, b, f), t + gTone);
}

/**
 * Line coverage for a family x = const where x may jump by whole numbers
 * across a branch cut: fwidth taken on two copies of x cut in different
 * places, keeping the smaller, so the cut does not draw a line of its own.
 */
float bhLine(float x, float xAlt, float sharp) {
    float fw = max(min(fwidth(x), fwidth(xAlt)), 1e-4);
    float d = abs(fract(x) - 0.5) / fw;
    float core = exp(-d * d * sharp);
    float glow = 0.10 / (1.0 + d * 0.5);
    return mix(min(fw, 1.0) * 0.5, core + glow, smoothstep(0.35, 0.12, fw));
}

vec3 bhStars(vec3 d, float twinkle) {
    vec3 col = vec3(0.0);
    for (int l = 0; l < 3; l++) {
        float sc = 38.0 + 46.0 * float(l);
        vec3 p = d * sc;
        vec3 c = floor(p);
        vec3 f = p - c - 0.5;
        float h = hash13(c + float(l) * 31.7);
        if (h > 0.72) {
            vec3 o = vec3(hash13(c + 1.31), hash13(c + 2.77), hash13(c + 4.13)) - 0.5;
            vec3 q = f - o * 0.55;
            float dd = dot(q, q);
            float b = (h - 0.72) / 0.28;
            b = b * b * (1.0 + twinkle * (0.5 + 0.5 * sin(uTime * 3.0 + h * 90.0)));
            vec3 tint = mix(vec3(1.0, 0.75, 0.55), vec3(0.65, 0.8, 1.0), fract(h * 13.7));
            col += tint * b * (exp(-dd * 600.0) * 5.0 + exp(-dd * 60.0) * 0.12);
        }
    }
    // The nebula: a dense iridescent filigree swirl centred on the point of
    // the sky behind the hole, so lensing wraps it into rings and arcs.
    vec3 ax = gAxis;
    vec3 e1 = normalize(cross(ax, vec3(0.0, 1.0, 0.0)));
    vec3 e2 = cross(e1, ax);
    float th = acos(clamp(dot(d, ax), -1.0, 1.0));
    float psi = atan(dot(d, e2), dot(d, e1));
    float psiAlt = atan(-dot(d, e2), -dot(d, e1)) + 3.1415927;
    float lr = log(max(th, 1e-3));
    float neb = bhFbm(vec3(lr * 1.5, cos(psi) * 1.3, sin(psi) * 1.3) + 1.7);
    vec3 base = mix(vec3(0.10, 0.03, 0.26), vec3(0.02, 0.10, 0.30), neb);
    col += base * (0.45 + 0.8 * neb * neb) * gWake;
    col += neon(0.15 + 0.6 * neb) * smoothstep(0.55, 0.9, neb) * 0.35 * gWake;
    float arms = gArms;
    float flow = uTime * 0.15;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float n = arms * (1.0 + fi * 0.8) + 2.0 * fi;
        float tw = (1.6 + 0.6 * fi) * (i == 1 || i == 3 ? -1.0 : 1.0);
        float u = lr * tw * n / 6.2831853 - flow * (1.0 + 0.3 * fi);
        float x1 = u + psi * n / 6.2831853;
        float x2 = u + psiAlt * n / 6.2831853;
        float l = bhLine(x1, x2, 0.6);
        float vary = 0.4 + 0.6 * smoothstep(0.3, 0.7, bhFbm(vec3(lr * 2.0 + fi, cos(psi) * 2.0, sin(psi) * 2.0)));
        col += neon(0.2 * fi + lr * 0.25 + 0.3 * neb) * l * vary * (0.30 - 0.045 * fi) * gWake;
    }
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    // The six looks the disc was drawn around, each 0..1 with 0.5 as tuned,
    // now driven: see "what moves it" above.
    float surge = neonSurge();
    float quiet = neonQuiet();
    float kPulse = clamp(0.15 + 0.85 * max(neonKick(), 0.7 * sndShaped(clamp(SND_SNARE_ENV, 0.0, 1.0))) + 0.5 * surge, 0.0, 1.0) * (1.0 - 0.7 * quiet);
    float kTex = mix(0.5, 1.0, neonForm());
    float kFlow = clamp(0.5 + 0.5 * neonFlow() + 0.25 * (neonWeight() - 0.5), 0.0, 1.0);
    float kBuild = clamp(0.5 + 0.5 * neonBuild() + 0.8 * surge - 0.3 * quiet, 0.0, 1.5);
    float kSpace = clamp(0.5 + 0.45 * quiet - 0.35 * surge, 0.0, 1.0);
    float glow = neonGlow();
    // Note & key reactions' style knobs ride on top (lib_neon).
    kPulse = clamp(kPulse + neonKnob(0), 0.0, 1.0);
    kTex = clamp(kTex + neonKnob(1), 0.0, 1.0);
    kFlow = clamp(kFlow + neonKnob(2), 0.0, 1.0);
    kBuild = clamp(kBuild + neonKnob(4), 0.0, 1.5);
    kSpace = clamp(kSpace + neonKnob(5), 0.0, 1.0);
    gTone = 0.12 + neonKeyTurn() + 0.6 * neonKnob(3);
    gWake = 0.75 + 0.4 * kBuild;
    // A count, so it changes only on a hard cut (lib_neon: never a count on Morph).
    gArms = 6.0 + 2.0 * mod(floor(SND_CUT_COUNT + 0.5), 3.0);

    camAround(uv, mix(16.0, 23.0, kSpace), CAM_SPIN_UNIT * 3.0, ro, rd);
    // Look down on the disc a little: tilt the world about the eye's own
    // horizontal axis, so the slant is the same from every orbit angle.
    // Turbulence sets it precessing.
    float wobble = 0.14 * neonChaos() * sin(uTime * 0.6);
    mat3 tilt = rotAxis(cross(ro, vec3(0.0, 1.0, 0.0)), 0.10 + wobble);
    ro = tilt * ro;
    rd = tilt * rd;
    gAxis = normalize(-ro);

    // The disc clock: a slow drift of its own plus the bass clock, so it
    // races in a heavy passage and never runs backwards; Warp winds it on.
    float spin = uTime * 0.12 + SND_BASS_TIME * 0.45 + kFlow * 3.0;
    float stepScale = clamp(96.0 / max(uSteps, 24.0), 0.75, 2.5);

    vec3 p = ro;
    vec3 v = rd;
    vec3 hc = cross(p, v);
    float h2 = dot(hc, hc);

    vec3 col = vec3(0.0);
    float trans = 1.0;
    float ring = 0.0;
    float haze = 0.0;
    bool captured = false;

    for (int i = 0; i < BH_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        float r = length(p);
        if (r < 1.0) {
            captured = true;
            break;
        }
        if (r > BH_ESCAPE && dot(p, v) > 0.0) break;
        float dt = clamp(0.085 * r, 0.035, 2.0) * stepScale;
        // Near the photon sphere, take finer steps so the ring stays crisp.
        dt *= mix(0.45, 1.0, smoothstep(1.3, 2.6, r));
        vec3 acc = -1.5 * h2 * p / (r * r * r * r * r);
        vec3 pn = p + (v + acc * dt) * dt;
        v += acc * dt;

        // Photon-ring glow: path length spent hugging r = 1.5.
        float dr = r - 1.5;
        ring += dt * exp(-dr * dr * 60.0);
        // A thin hot haze hugging the disc plane, lensed with everything else.
        float rr = length(p.xz);
        haze += dt * exp(-abs(p.y) * (5.5 - 3.0 * kFlow)) * smoothstep(BH_DISC_OUT, BH_DISC_IN, rr) * smoothstep(BH_DISC_IN - 0.6, BH_DISC_IN + 0.8, rr);

        if (p.y * pn.y < 0.0) {
            float f = p.y / (p.y - pn.y);
            vec3 hit = mix(p, pn, f);
            float hr = length(hit.xz);
            if (hr > BH_DISC_IN * 0.85 && hr < BH_DISC_OUT * 1.1) {
                // Keplerian shear, bounded: two copies of the texture, each
                // winding for one period and then fading out while the other
                // takes over, so the disc never shears into endless lines.
                float omega = 1.6 * pow(hr, -1.5);
                float baseAng = atan(hit.z, hit.x);
                float dens = 0.0;
                float fil = 0.0;
                float hueN = 0.0;
                // Fixed: a line family's frequency on Morph would re-pattern the
                // disc on every beat. Morph sharpens the threads instead.
                float lineCount = 8.5;
                for (int k = 0; k < 2; k++) {
                    float ph = fract(spin * 0.12 + 0.5 * float(k));
                    float w = 1.0 - abs(2.0 * ph - 1.0);
                    float ang = baseAng - omega * ph * 22.0 + float(k) * 2.4;
                    vec3 q = vec3(cos(ang) * 3.2, sin(ang) * 3.2, log(hr) * 4.5 + float(k) * 7.0);
                    float n = bhFbm(q);
                    float lanes = bhFbm(vec3(cos(ang) * 1.1, sin(ang) * 1.1, log(hr) * 13.0 + float(k) * 3.0));
                    dens += w * smoothstep(0.3, 0.8, n) * (0.4 + 0.6 * smoothstep(0.35, 0.65, lanes));
                    // Filaments: contour lines of the turbulence, thin and bright.
                    float fl = abs(fract(n * lineCount + lanes * 2.0) - 0.5);
                    fil += w * exp(-fl * mix(22.0, 40.0, kTex));
                    hueN += w * n;
                }
                // Spiral arms: log-spirals in the disc plane, turning with it.
                float armAng = baseAng - spin * 0.25;
                float armX = log(hr) * 2.2 * gArms / 6.2831853 + armAng * gArms / 6.2831853;
                float armXAlt = log(hr) * 2.2 * gArms / 6.2831853 + (atan(-hit.z, -hit.x) + 3.1415927 - spin * 0.25) * gArms / 6.2831853;
                float armL = bhLine(armX, armXAlt, 0.5);
                dens = max(dens, 0.0) + 0.5 * armL;
                fil += armL;
                float edge = smoothstep(BH_DISC_IN * 0.85, BH_DISC_IN * 1.25, hr) * smoothstep(BH_DISC_OUT * 1.1, BH_DISC_OUT * 0.55, hr);

                // Temperature: hottest just outside the inner edge, falling outward.
                float x = BH_DISC_IN / hr;
                float temp = max(1.55 * pow(x, 0.75) * pow(max(1.0 - sqrt(x * 0.8), 0.0), 0.25), 0.45);
                // Doppler: disc gas orbits about +y; n points from the gas toward the eye.
                vec3 tang = normalize(vec3(-hit.z, 0.0, hit.x));
                float beta = clamp(sqrt(0.5 / hr), 0.0, 0.7);
                vec3 toEye = -normalize(v);
                float gamma = inversesqrt(1.0 - beta * beta);
                float dop = 1.0 / (gamma * (1.0 - beta * dot(tang, toEye)));
                float grav = sqrt(max(1.0 - 1.0 / hr, 0.05));
                float shift = dop * grav;
                float bright = shift * shift * shift;

                // Neon emission: the hue follows radius and turbulence and
                // slides toward blue on the approaching side.
                float hue = 0.1 + 1.2 * x + 0.7 * hueN + 0.15 * log(hr) - 0.3 * (shift - 1.0) + 0.2 * SND_CHORD_HUE;
                vec3 em = neon(hue) * (0.10 + 0.9 * dens + 1.6 * fil * (0.3 + 0.7 * dens));
                em *= temp * bright * 3.0 * (0.75 + 0.35 * kBuild) * glow * (1.0 - 0.5 * quiet);
                // The pulse flares the inner disc toward white.
                em += vec3(1.0, 0.92, 1.0) * kPulse * x * x * x * bright * (0.5 + fil) * 1.5;
                float alpha = clamp((0.25 + 0.6 * dens) * edge, 0.0, 0.9);
                col += trans * em * alpha;
                trans *= 1.0 - alpha;
                if (trans < 0.02) break;
            }
        }
        p = pn;
    }

    // The ring and the haze are emission picked up along the way.
    col += neon(0.75) * haze * 0.004 * (0.8 + 1.2 * kFlow);
    if (captured) {
        // Inside the shadow: a photon-ring lattice, a mandala of the light
        // still circling the hole, fading toward the centre.
        float b = sqrt(h2) / 2.6;
        float a = atan(uv.y, uv.x);
        float seg = 6.2831853 / 12.0;
        float fa = abs(mod(a + uTime * 0.04, seg) - 0.5 * seg);
        float rings = exp(-abs(fract(log(max(b, 1e-3)) * 5.0 - uTime * 0.1) - 0.5) * 22.0);
        float petal = 0.0;
        for (int i = 0; i < 3; i++) {
            float R0 = 0.3 + 0.22 * float(i);
            float pr = R0 * (1.0 + 0.25 * cos(fa * (6.0 + 2.0 * float(i))));
            petal += exp(-abs(b - pr) * 40.0);
        }
        float spokes = exp(-fa * b * 40.0) * smoothstep(0.1, 0.5, b);
        vec3 inner = neon(0.05 + b * 0.5) * rings * 0.35 + neon(0.25 + b * 0.4) * petal * 0.6 + neon(0.6) * spokes * 0.25;
        inner += vec3(0.06, 0.02, 0.16) * (0.4 + b);
        col += trans * inner * (0.5 + 0.5 * kBuild) * smoothstep(0.0, 0.4, b) * (0.7 + 0.6 * kPulse);
    }
    if (!captured) {
        col += trans * mix(neon(0.5), vec3(1.0), 0.5) * ring * (0.6 + 1.2 * kPulse) * glow;
        col += trans * bhStars(normalize(v), 0.3 + 1.5 * neonSparkle());
        // The kick's shockwave: a ring of light at an impact parameter that
        // runs outward from the photon ring as the envelope falls. Measured
        // in impact parameter, not on screen, so the hole lenses it too.
        float kick = neonKick() * (1.0 - quiet);
        float wave = 2.7 + (1.0 - kick) * 9.0;
        float bImpact = sqrt(h2);
        col += trans * neon(0.35 + 0.05 * wave) * exp(-abs(bImpact - wave) * 2.5) * kick * 0.9 * glow;
    }

    fragColor = vec4(neonFinish(col, 0.95), 1.0);
}
