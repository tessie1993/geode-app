#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the view and the water (core/viz/pool), not on the screen.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_palette
//#include lib_scene_grade

// Pool: a swimming pool's tiled floor through its moving water, from a slant along it (Morph turns the
// eye toward straight down). core/viz/pool/PoolMath owns every number of the surface and pins them in
// pool_test: travelling waves, ripple rings from three wandering sources, a wake wherever a finger
// lands, the view, and the look the controls ask for. This file draws the same sums per pixel:
// refraction bends the floor by the slope, caustics gather on it by the curvature, the sky and the sun
// reflect off it by Fresnel.
//
// What it hears, each a share in 0..1 of the heights PoolMath bounds:
//   the swell   the level (SND_VOL_ATT)       the long waves stand taller as the song swells
//   the chop    the treble (SND_TREBLE_ATT)   the short waves, which mostly show in the caustics
//   the rings   the bass (SND_BASS_ATT)       the ripples spreading from the wandering sources
// All three fade with SND_PRESENCE, and the clocks stop in silence: no sound, a still pool.
// k3 TONE turns the tiles' hue; the other knobs reach the water through PoolMath.

uniform highp vec4 uPoolWaves[8];   // kx, ky, phase, height
uniform highp vec4 uPoolRings[3];   // x, y, phase, height
uniform highp vec4 uPoolWakes[6];   // x, y, sigma, height
uniform highp vec4 uPoolLook;       // refraction, focus, caustics, shade
uniform highp vec4 uPoolView;       // sin tilt, cos tilt, eye height, field of view

// pool::kRingK, kRingReach, kRingSoft, kFocusFloor, kMaxRefraction, kHorizon
#define POOL_RING_K 10.0
#define POOL_RING_REACH 0.7
#define POOL_RING_SOFT 0.05
#define POOL_FOCUS_FLOOR 0.2
#define POOL_MAX_REFRACTION 0.12
#define POOL_HORIZON 0.02
#define POOL_TILE 0.15

vec3 gGain;  // swell, chop, rings

/** The surface's slope at q; hess gets its curvature (xx, xy, yy). pool::gradient and pool::hessian. */
vec2 poolSlope(vec2 q, out vec3 hess) {
    vec2 g = vec2(0.0);
    hess = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        vec4 w = uPoolWaves[i];
        float a = w.w * (i < 4 ? gGain.x : gGain.y);
        float th = dot(w.xy, q) - w.z;
        g += a * cos(th) * w.xy;
        hess -= a * sin(th) * vec3(w.x * w.x, w.x * w.y, w.y * w.y);
    }
    float inv = 1.0 / (POOL_RING_REACH * POOL_RING_REACH);
    for (int i = 0; i < 3; i++) {
        vec4 r = uPoolRings[i];
        float a = r.w * gGain.z;
        vec2 d = q - r.xy;
        float s = sqrt(dot(d, d) + POOL_RING_SOFT * POOL_RING_SOFT);
        float w = exp(-0.5 * s * s * inv);
        float w1 = -s * inv * w;
        float w2 = (s * s * inv * inv - inv) * w;
        float th = POOL_RING_K * s - r.z;
        float sn = sin(th);
        float cs = cos(th);
        float f1 = a * (POOL_RING_K * cs * w + sn * w1);
        float f2 = a * (-POOL_RING_K * POOL_RING_K * sn * w + 2.0 * POOL_RING_K * cs * w1 + sn * w2);
        float iso = f1 / s;
        float radial = (f2 - iso) / (s * s);
        g += iso * d;
        hess += vec3(radial * d.x * d.x + iso, radial * d.x * d.y, radial * d.y * d.y + iso);
    }
    for (int i = 0; i < 6; i++) {
        vec4 w = uPoolWakes[i];
        if (w.w <= 0.0) continue;
        vec2 d = q - w.xy;
        float inv2 = 1.0 / (w.z * w.z);
        float u = dot(d, d) * inv2;
        float e = exp(-0.5 * u);
        float d1 = -w.w * e * (0.25 * u - 1.0) * 2.0 * inv2;
        float d2 = -w.w * e * (0.75 - 0.125 * u) * 4.0 * inv2 * inv2;
        g += d1 * d;
        hess += vec3(d2 * d.x * d.x + d1, d2 * d.x * d.y, d2 * d.y * d.y + d1);
    }
    return g;
}

/** pool::causticLuma: within [-shade, 1] times uPoolLook.z. */
float poolCaustic(vec3 h) {
    float d = uPoolLook.y;
    float det = 1.0 + d * (h.x + h.z) + d * d * (h.x * h.z - h.y * h.y);
    float gain = 1.0 / max(abs(det), POOL_FOCUS_FLOOR) - 1.0;
    return uPoolLook.z * (gain > 0.0 ? gain / (1.0 / POOL_FOCUS_FLOOR - 1.0) : uPoolLook.w * gain);
}

float tileHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/** The floor's own colour at q: glazed tiles, grout between them and a dark lane line every twelve. */
vec3 floorAlbedo(vec2 q, float hue, float far) {
    vec2 t = q / POOL_TILE;
    vec2 cell = floor(t);
    vec2 f = abs(fract(t) - 0.5);
    float aa = max(fwidth(t.x) + fwidth(t.y), 1e-4);
    float grout = 1.0 - smoothstep(0.455 - aa, 0.475 + aa, max(f.x, f.y));
    grout = mix(grout, 0.85, far);
    vec3 tile = mix(vec3(0.82), pal(hue + 0.06 * (tileHash(cell) - 0.5)), 0.55);
    tile *= 0.9 + 0.1 * tileHash(cell + 17.0);
    vec3 col = mix(tile * 0.62, tile, grout);
    float lane = abs(fract((cell.x + 0.5) / 12.0) - 0.5) * 12.0;
    float stripe = (1.0 - smoothstep(0.5 - aa, 0.5 + aa, lane)) * (1.0 - far);
    return mix(col, pal(hue + 0.5) * 0.18, stripe * 0.85);
}

vec3 skyColour(vec3 r, float hue) {
    float up = clamp(r.z, 0.0, 1.0);
    vec3 col = mix(mix(pal(hue + 0.45), vec3(0.95), 0.55) * 0.75, pal(hue + 0.6) * 0.35, sqrt(up));
    vec3 sun = normalize(vec3(-0.35, 0.75, 0.55));
    col += vec3(1.0, 0.96, 0.88) * min(pow(max(dot(r, sun), 0.0), 220.0) * 2.5, 0.6);
    return col;
}

void main() {
    // Life's births (lib_grow): no field loop here to run them in, so each is the picture's middle again,
    // small, at its spawn point; uv itself at Life 0.
    vec2 uv = growBirths2(view());
    float here = clamp(SND_PRESENCE, 0.0, 1.0);
    gGain = here * vec3(0.4 + 0.6 * min(sndLevel(SND_VOL_ATT), 1.0),
                        0.5 + 0.5 * min(sndLevel(SND_TREBLE_ATT), 1.0),
                        min(sndLevel(SND_BASS_ATT), 1.0));
    float hue = 0.52 + 0.5 * (uKnob[3] - 0.5);

    // pool::surfacePoint: the eye at uPoolView.z, looking down and along by the tilt.
    float sinT = uPoolView.x;
    float cosT = uPoolView.y;
    vec3 rd = normalize(vec3(uv.x * uPoolView.w, sinT + uv.y * uPoolView.w * cosT, -cosT + uv.y * uPoolView.w * sinT));
    if (rd.z >= -POOL_HORIZON * length(rd)) {
        fragColor = vec4(grade(skyColour(rd, hue)), 1.0);
        return;
    }
    float t = uPoolView.z / -rd.z;
    vec2 p = rd.xy * t;
    // Past a few metres the ripples are finer than a pixel: they settle flat rather than shimmer.
    float far = smoothstep(5.0, 14.0, t);

    vec3 hs;
    vec2 g = poolSlope(p, hs) * (1.0 - far);
    vec3 n = normalize(vec3(-g, 1.0));
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float fres = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    // pool::refraction: the floor is seen displaced along the slope, capped.
    vec2 off = -g * uPoolLook.x;
    float len = length(off);
    if (len > 1e-6) off *= POOL_MAX_REFRACTION * tanh(len / POOL_MAX_REFRACTION) / len;
    vec2 q = p + off;

    vec3 hq;
    poolSlope(q, hq);
    float caustic = poolCaustic(hq) * (1.0 - far);

    vec3 albedo = floorAlbedo(q, hue, far);
    float path = 1.0 + 0.35 * t;
    vec3 water = pal(hue + 0.02) * vec3(0.55, 0.85, 1.0);
    float clear = exp(-0.08 * path);
    vec3 under = mix(water * 0.3, albedo * mix(vec3(1.0), water, 0.4) * 0.62, clear);

    vec3 r = reflect(rd, n);
    r.z = abs(r.z);
    vec3 col = mix(under, skyColour(r, hue), fres);
    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.6, 2.0, length(uv)));
    col = grade(col);
    // After the grade, so no control stretches the caustics past their ceiling: the light through the
    // water, at most what pool::causticLuma allows.
    vec3 light = clamp(mix(vec3(1.0, 0.98, 0.92), pal(hue + 0.1), 0.25), 0.0, 1.0);
    col += (1.0 - fres) * clear * mix(vec3(1.0), albedo, 0.3) * caustic * light;
    fragColor = vec4(max(col, 0.0), 1.0);
}
