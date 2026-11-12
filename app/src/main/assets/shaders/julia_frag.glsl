#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade

// Julia: the audio-reactive Julia set, rebuilt as a solid. The same map,
// z -> z^2 + c, run on quaternions instead of complex numbers, so the set is a
// four-dimensional body; the picture is its 3D slice (real, i, j) at a k that
// Warp moves, lit like a sculpture and turned slowly on the shared rig
// (camAround). The old style's idea survives whole: c is still the one knob
// that reshapes everything, it still orbits slowly, and the music still pushes
// it. Its imaginary part is now a direction as well as a size, and that
// direction drifts through i, j and k, so the body folds over itself as it
// turns instead of only swapping one spiral for another.
//
// c is kept near the Douady rabbit (-0.123 + 0.745i), which Morph slides toward
// the Siegel disc (-0.391 + 0.587i). A quaternion Julia set is connected
// exactly when the complex number (c.x, |c.yzw|) is in the Mandelbrot set, so
// both ends are solid bodies and the small orbit around them only frays the
// crust; the 2D style's seahorse-valley c would be dust in 3D.
//
// ---- camera -----------------------------------------------------------------
//
// camAround at radius 4 about +y, three turns per 1024 world units of travel
// (an integer multiple of CAM_SPIN_UNIT, so the rig's 8192 travel wrap lands on
// a whole turn and is invisible). The subject is fixed at the origin and the
// eye's position never reads uCamPos, so there is no world z to rebase. The
// dolly (kick punches, drop pushes) pulls the eye in; it stays outside the
// bounding sphere at every knob and dolly value.
//
// ---- audio ------------------------------------------------------------------
//
// Pitch change (SND_HELD_NOTE_CHANGE) glides c out to a hashed spot and back,
//                      the spot re-picked per onset (pitchGlide), so the body
//                      reshapes on every new note without snapping.
// Onset (SND_ONSET_ENV) a brief hue phase step in the palette.
// Kick (SND_KICK_ENV)  the body grows by up to 6%: scale, not brightness.
// Bass, mids           still swing c's real part and its size.
// Treble (SND_TREBLE_ATT) stretches the palette across the orbit-trap bands.
// Spectrum             each trap band is lit by its own analyser band, as each
//                      escape band was in 2D.
// SND_INTENSITY        overall level, as everywhere.
//
// ---- macros and knobs -------------------------------------------------------
//
// Morph slides c from the rabbit toward the Siegel disc. Warp moves the k slice
// through the fourth dimension. Turbulence widens c's slow orbit. Intensity
// and the colour controls are grade(); Space is the rig. The six knobs are read
// as offsets from 0.5, so with Note & key reactions off the style is exactly
// as tuned: k0 PULSE the kick's growth, k1 TEXTURE the trap bands' density,
// k2 FLOW the orbit's width, k3 TONE the hue, k4 BUILD the halo, k5 SPACE the
// camera's distance.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The estimate is the standard one for a quadratic Julia set, |z| log|z| /
// (2 |z'|), with |z'| carried as a running bound (|d(z^2)| <= 2 |z| |dz| holds
// for quaternions, which is all the bound needs). It underestimates near the
// set and the march takes 0.8 of it besides. |z'| is carried squared and the
// escape radius is 4, so it grows at most 64x per iteration: 64^11 is ~7e19,
// far below highp's limit, and it is floored before the divide. The kick's
// scale is applied as s * DE(p / s), which is exact. The march is clipped to a
// bounding sphere that contains every c the music can reach, so a missed ray
// costs nothing past it, and both loops have constant bounds that uSteps and
// the iteration budget only break early.

#define JQ_MAX_STEPS 120
#define JQ_ITERS 11
#define JQ_BOUND 1.75
#define JQ_CAM_R 4.0
#define JQ_ESCAPE 16.0
const float JQ_SPIN = 3.0 * CAM_SPIN_UNIT;

vec4 gC;
float gSlice;
float gScale;
int gIters;
/** Set by map(): the smallest |z|^2 the orbit met, and how many steps it ran before escaping (0..1). */
float gTrap;
float gEsc;

float map(vec3 p) {
    vec4 z = vec4(p / gScale, gSlice);
    float dz2 = 1.0;
    float trap = 1e9;
    float m2 = dot(z, z);
    float n = 0.0;
    for (int i = 0; i < JQ_ITERS; i++) {
        if (i >= gIters) break;
        dz2 *= 4.0 * m2;
        z = vec4(z.x * z.x - dot(z.yzw, z.yzw), 2.0 * z.x * z.yzw) + gC;
        m2 = dot(z, z);
        trap = min(trap, m2);
        if (m2 > JQ_ESCAPE) break;
        n += 1.0;
    }
    gTrap = trap;
    gEsc = n / float(gIters);
    float d = 0.25 * log(max(m2, 1e-8)) * sqrt(m2 / max(dz2, 1e-30));
    return d * gScale;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/**
 * A hashed point in the unit disc for event number `n` (PCG integer hash, so
 * it is exact on every device). Used to pick where the next glide goes.
 */
vec2 hashSpot(float n) {
    uint h = uint(max(n, 0.0)) * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    float a = float(h & 0xffffu) * (6.2831853 / 65536.0);
    float r = sqrt(float(h >> 16u) * (1.0 / 65536.0));
    return vec2(cos(a), sin(a)) * r;
}

/**
 * The pitch-change glide, without frame-to-frame state (unchanged from the 2D
 * style). The SPOT is chosen by the onset count and eased from the previous
 * count's spot to the new one by the onset envelope, measured from 0.4 where
 * viz/Reactions triggers it, so it moves without ever stepping; the previous
 * count wraps like floor(SND_HIT_TIME) itself (4096). HOW FAR c travels toward
 * the spot is the pitch-change envelope as a hump, 4e(1-e), 0 before the event
 * and at its instant rise, so c eases out and back over the envelope's fall.
 */
vec2 pitchGlide() {
    float g = (1.0 - smoothstep(0.05, 0.4, clamp(SND_ONSET_ENV, 0.0, 1.0)));
    vec2 spot = mix(hashSpot(mod(floor(SND_HIT_TIME) - 1.0, 4096.0)), hashSpot(floor(SND_HIT_TIME)), g);
    float e = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    return spot * (4.0 * e * (1.0 - e));
}

void main() {
    vec2 uv = view();

    float k0 = uKnob[0] - 0.5;
    float k1 = uKnob[1] - 0.5;
    float k2 = uKnob[2] - 0.5;
    float k3 = uKnob[3] - 0.5;
    float k4 = uKnob[4] - 0.5;
    float k5 = uKnob[5] - 0.5;

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);

    // uTime is already the speed-integrated clock; never times Speed again.
    float t = uTime * 0.15;
    vec2 base = mix(vec2(-0.123, 0.745), vec2(-0.391, 0.587), smoothstep(0.0, 1.0, clamp(uMorph, 0.0, 1.0)));
    float amp = 0.035 * (1.0 + 1.5 * clamp(uTurbulence, 0.0, 1.0)) * max(1.0 + k2, 0.2);
    vec2 c2 = base + amp * vec2(cos(t), sin(t * 1.3));
    c2 += vec2(sndLevel(SND_BASS) * 0.04, sndShaped(SND_MIDDLE) * 0.025);
    // c is the most sensitive parameter there is; 0.04 already reshapes the body.
    c2 += 0.04 * pitchGlide();
    // The imaginary part's direction wanders through i, j and k.
    vec3 dir = normalize(vec3(cos(t * 0.37), sin(t * 0.37), 0.35 + 0.55 * sin(t * 0.23)));
    gC = vec4(c2.x, c2.y * dir);
    gSlice = 0.35 * clamp(uWarp, 0.0, 1.0) * sin(uTime * 0.05 + 1.0);
    gScale = 1.0 + 0.06 * clamp(1.0 + 1.2 * k0, 0.0, 2.0) * kick;
    gIters = uSteps < 80.0 ? 8 : JQ_ITERS;

    vec3 ro;
    vec3 rd;
    camAround(uv, JQ_CAM_R * clamp(1.0 - 0.3 * k5, 0.8, 1.2), JQ_SPIN, ro, rd);

    // Clip the march to the bounding sphere.
    float bound = JQ_BOUND * gScale;
    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - bound * bound);
    float hitT = -1.0;
    float used = 0.0;
    float closest = 1e9;
    float trap = 1.0;
    float esc = 0.0;
    if (h > 0.0) {
        float sh = sqrt(h);
        float tt = max(-b - sh, 0.0);
        float tEnd = -b + sh;
        for (int i = 0; i < JQ_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tt > tEnd) break;
            used = float(i);
            float d = map(ro + rd * tt);
            closest = min(closest, d);
            float eps = (uSteps < 80.0 ? 0.0014 : 0.0007) * tt + 0.0003;
            if (d < eps) {
                hitT = tt;
                trap = gTrap;
                esc = gEsc;
                break;
            }
            tt += max(d * 0.8, eps);
        }
    }

    float hue = 0.58 + 0.04 * sin(uTime * 0.02) + 0.25 * k3 + 0.12 * onset;
    // A dim vertical wash behind the body.
    vec3 col = pal(hue + 0.5) * (0.025 + 0.04 * (0.5 + 0.5 * uv.y)) + vec3(0.004, 0.004, 0.008);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0012, 0.0010 * hitT));
        // Trap bands: nested shells of related hue, stretched by the treble.
        float f = clamp(sqrt(trap) * (1.0 + 1.5 * k1), 0.0, 1.0);
        float idx = f * (1.0 + sndLevel(SND_TREBLE_ATT)) + 0.3 * esc + hue;
        vec3 albedo = pal(idx) * (0.25 + f * 1.2 + sndSpectrum(f) * 0.8);

        vec3 key = normalize(vec3(0.6, 0.75, -0.35));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float diff = max(dot(n, key), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float rim = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), key), 0.0), 28.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.3, 0.2, 1.0);
        col = albedo * (0.12 + 0.85 * diff) * ao
            + pal(hue + 0.33) * sky * 0.10 * ao
            + pal(hue + 0.15) * rim * 0.45
            + vec3(spec) * 0.35 * ao;
    } else if (closest < 1e8) {
        // The halo: how close a missed ray came to the crust.
        float halo = exp(-closest * 9.0) * 0.35 * clamp(1.0 + 1.5 * k4, 0.0, 2.0);
        col += pal(hue + 0.1) * halo * (0.6 + 0.6 * swell);
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
