#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// uKnob[6], the six style knobs read below, is declared in lib_scene_uniforms.

// Neon Chrome Sea: low flight over a sea of liquid chrome under a violet sky.
// A kaleidoscopic neon mandala hangs over the horizon like a portal, neon
// ribbons hang in the sky, and the swell mirrors all of it with a thin-film
// oil-slick sheen (orange-green-purple) on every slope.
//
// ---- knobs ------------------------------------------------------------------
//
//   k0  crest glow    PULSE    neon contour lines light up across the swell
//   k1  ripple        TEXTURE  more and sharper small waves riding the swell
//   k2  swell         FLOW     the height of the big waves
//   k3  hue           TONE     turns the neon ramp of sky, portal and film
//   k4  portal        BUILD    the mandala grows, opens and brightens
//   k5  altitude      SPACE    the eye rises above the sea for a wider view
//
// Provenance: written from scratch for MuzViz from general techniques - a sum
// of exp(sin) waves (sharp crests, broad troughs) where each wave's slope drags
// the sample point for the next one ("domain drag", which bunches small waves
// on the big crests); a heightfield march with a slope-bounded step and a
// linear refine; Schlick's Fresnel; a cosine thin-film interference tint on
// the view angle; and a kaleidoscopic polar fold for the mandala. No code was
// taken from any published shader.
//
// Every wave's z wavenumber is rounded to a whole multiple of 2pi/64, so the
// sea tiles along z with period 64 and the rig's 8192 travel wrap is invisible
// (the eye's z is rebased into one tile, exact in highp). The sky is at
// infinity and has no parallax to wrap.

/** Steps per ray: the compile-time ceiling. uSteps is the runtime budget. */
#define OC_MAX_STEPS 110
#define OC_GEO_WAVES 11
#define OC_NRM_WAVES 30
#define OC_SEA_Y -2.4
#define OC_FAR 90.0
/** 2pi / 64: the z wavenumber unit that keeps the sea periodic over the travel wrap. */
#define OC_KZ 0.09817477

float gAmp;
float gTime;
float gFalloff;
float gTone;
float gPortal;

/**
 * Exp-sine waves with domain drag. Returns height 0..1 (before gAmp). Each
 * wave turns its direction by a fixed irrational angle so no two align.
 */
float ocWaves(vec2 p, int count) {
    float freq = 0.55;
    float amp = 1.0;
    float speed = 1.6;
    float ang = 0.3;
    float sumH = 0.0;
    float sumA = 0.0;
    for (int i = 0; i < OC_NRM_WAVES; i++) {
        if (i >= count) break;
        vec2 k = vec2(sin(ang), cos(ang)) * freq;
        k.y = floor(k.y / OC_KZ + 0.5) * OC_KZ;
        float x = dot(k, p) + gTime * speed;
        float w = exp(sin(x) - 1.0);
        float dw = w * cos(x);
        sumH += w * amp;
        sumA += amp;
        // Drag: pull the sample point along the wave's slope, so later, smaller
        // waves ride and crowd the crests of the earlier, bigger ones.
        p -= normalize(k + 1e-4) * dw * amp * 0.38;
        freq *= 1.19;
        amp *= gFalloff;
        speed *= 1.06;
        ang += 2.39996;
    }
    return sumH / sumA;
}

float ocHeight(vec2 p, int count) {
    return OC_SEA_Y + gAmp * ocWaves(p, count);
}

vec3 ocNormal(vec2 p, float eps) {
    float h = ocHeight(p, OC_NRM_WAVES);
    float hx = ocHeight(p + vec2(eps, 0.0), OC_NRM_WAVES);
    float hz = ocHeight(p + vec2(0.0, eps), OC_NRM_WAVES);
    return normalize(vec3(h - hx, eps, h - hz));
}

float ocNoise2(vec2 p) {
    vec2 f = floor(p);
    vec2 t = p - f;
    t = t * t * (3.0 - 2.0 * t);
    ivec3 i = ivec3(ivec2(f), 7);
    float a = hashCell(i);
    float b = hashCell(i + ivec3(1, 0, 0));
    float c = hashCell(i + ivec3(0, 1, 0));
    float d = hashCell(i + ivec3(1, 1, 0));
    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

float ocFbm2(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        s += a * ocNoise2(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + 3.1;
        a *= 0.5;
    }
    return s / 0.96875;
}

vec3 gMoonDir;

vec3 neon(float t) {
    vec3 c = 0.5 + 0.5 * cos(6.2831853 * (t + gTone + vec3(0.60, 0.78, 0.98)));
    c = c * c * c * vec3(1.1, 1.0, 1.35) + vec3(0.02, 0.0, 0.06);
    return mix(c, pal(t + gTone), 0.3);
}

/** The neon mandala: rings, petals and spokes folded into 12 mirrored wedges. */
vec3 ocMandala(vec2 p) {
    float r = length(p);
    float a = atan(p.y, p.x);
    float seg = 6.2831853 / 12.0;
    float fa = abs(mod(a + uTime * 0.05, seg) - 0.5 * seg);
    vec2 q = vec2(cos(fa), sin(fa)) * r;
    float lr = log(max(r, 1e-4));
    vec3 col = vec3(0.0);
    // Rings flowing outward.
    float ring = abs(fract(lr * 3.0 - uTime * 0.15) - 0.5);
    col += neon(0.1 + lr * 0.3) * exp(-ring * 30.0) * 0.8;
    // Petals: curves r = R (1 + cos(k fa)) at a few radii.
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float R = 0.08 * pow(1.75, fi);
        float petal = R * (1.0 + 0.55 * cos(fa * (4.0 + 2.0 * fi) * 1.0 + uTime * 0.1 * (fi - 1.5)));
        float d = abs(r - petal) / R;
        col += neon(0.25 * fi + 0.4) * exp(-d * 24.0) * 1.2;
        float d2 = abs(q.y - q.x * tan(0.2 + 0.1 * fi)) / max(r, 1e-3);
        col += neon(0.25 * fi + 0.7) * exp(-d2 * 160.0) * smoothstep(R * 0.5, R, r) * smoothstep(R * 2.2, R * 1.2, r) * 0.3;
    }
    col += neon(0.55) * exp(-r * 18.0) * 1.5;
    return col * smoothstep(0.75, 0.45, r);
}

vec3 ocSky(vec3 d) {
    float up = max(d.y, 0.0);
    vec3 col = mix(vec3(0.07, 0.02, 0.16), vec3(0.008, 0.004, 0.03), pow(up, 0.5));
    col += vec3(0.10, 0.02, 0.18) * exp(-up * 9.0);
    // Neon ribbons hanging in the sky: bright folded curtains.
    float ax = atan(d.x, d.z);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float y = 0.18 + 0.12 * fi + 0.05 * sin(ax * (3.0 + fi) + uTime * 0.2 + fi * 2.0) + 0.03 * sin(ax * 11.0 - uTime * 0.3);
        float dy = d.y - y;
        float band = exp(-dy * dy * 1600.0) + 0.25 * exp(-max(-dy, 0.0) * 30.0) * step(dy, 0.0) * smoothstep(0.0, 0.1, d.y);
        col += neon(0.2 * fi + ax * 0.15) * band * (0.35 - 0.08 * fi);
    }
    // The portal over the horizon.
    vec3 tangent = normalize(cross(gMoonDir, vec3(0.0, 1.0, 0.0)));
    vec3 bitan = cross(tangent, gMoonDir);
    float md = dot(d, gMoonDir);
    if (md > 0.0) {
        vec2 mp = vec2(dot(d, tangent), dot(d, bitan)) / md / (gPortal * 0.7);
        col += ocMandala(mp) * (0.6 + 0.8 * gPortal) * smoothstep(0.015, 0.12, d.y);
    }
    col += neon(0.55) * pow(max(md, 0.0), 30.0) * 0.25 * gPortal;
    // Faint stars.
    if (d.y > 0.0) {
        vec3 p = d * 220.0;
        vec3 c = floor(p);
        float h = hash13(c);
        if (h > 0.95) {
            vec3 q = p - c - 0.5;
            col += vec3(0.7, 0.8, 1.0) * (h - 0.95) * 20.0 * exp(-dot(q, q) * 30.0) * 0.4;
        }
    }
    return col;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, 64.0);

    float kPulse = clamp(uKnob[0], 0.0, 1.0);
    float kTex = clamp(uKnob[1], 0.0, 1.0);
    float kFlow = clamp(uKnob[2], 0.0, 1.0);
    float kTone = clamp(uKnob[3], 0.0, 1.0);
    float kBuild = clamp(uKnob[4], 0.0, 1.0);
    float kSpace = clamp(uKnob[5], 0.0, 1.0);

    gTime = uTime * 0.8;
    gAmp = 0.45 * (0.7 + 1.1 * kFlow);
    gFalloff = mix(0.66, 0.78, kTex);
    gTone = kTone * 0.6;
    gPortal = mix(0.55, 1.15, kBuild);
    gMoonDir = normalize(vec3(0.0, 0.16, 1.0));

    ro.y = max(ro.y + 0.4 + 1.6 * kSpace, ocHeight(ro.xz, OC_GEO_WAVES) + 0.6);
    // Look down a touch as the eye climbs, so the sea keeps the frame.
    rd = normalize(rd + vec3(0.0, -0.12 * kSpace, 0.0));
    float top = OC_SEA_Y + gAmp;

    vec3 col;
    if (rd.y >= -0.0005) {
        col = ocSky(rd);
    } else {
        float t = max((ro.y - top) / -rd.y, 0.0);
        float tPrev = t;
        float gPrev = 1.0;
        bool hit = false;
        for (int i = 0; i < OC_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            vec3 p = ro + rd * t;
            float g = p.y - ocHeight(p.xz, OC_GEO_WAVES);
            if (g < 0.0) {
                t = mix(tPrev, t, gPrev / (gPrev - g));
                hit = true;
                break;
            }
            if (t > OC_FAR) break;
            tPrev = t;
            gPrev = g;
            t += max(g / (0.9 + abs(rd.y)) * 0.9, 0.006 * t + 0.01);
        }
        if (!hit) t = min(t, OC_FAR);
        vec3 p = ro + rd * t;

        float eps = 0.015 + 0.012 * t;
        vec3 n = ocNormal(p.xz, eps);
        n = normalize(mix(n, vec3(0.0, 1.0, 0.0), smoothstep(3.0, 35.0, t) * 0.88));

        float cosV = max(dot(n, -rd), 0.0);
        float fres = 0.55 + 0.45 * pow(1.0 - cosV, 5.0);
        vec3 refl = reflect(rd, n);
        refl.y = abs(refl.y);
        vec3 env = ocSky(refl);

        float hRel = (p.y - OC_SEA_Y) / max(gAmp, 1e-3);
        // Thin film: interference colour from the view angle and a film
        // thickness that varies with the swell, so slopes band into
        // orange-green-purple like oil on chrome.
        float film = 1.2 + 1.6 * hRel + 0.6 * sin(p.x * 0.7 + p.z * 0.4);
        vec3 irid = 0.5 + 0.5 * cos(6.2831853 * (film * (0.4 + cosV) + gTone + vec3(0.0, 0.33, 0.67)));
        vec3 chrome = env * fres * mix(vec3(1.0), irid * 1.4, 0.55);
        chrome += irid * vec3(0.05, 0.02, 0.09) * (0.5 + hRel);

        // Neon contour lines running along the swell: the pulse lights them.
        float iso = hRel * 7.0 - gTime * 0.3;
        float isoW = fwidth(iso);
        float isoLine = 1.0 - smoothstep(0.0, max(isoW * 1.5, 0.02), abs(fract(iso) - 0.5) * 2.0 - 0.0) ;
        isoLine *= (1.0 - smoothstep(10.0, 40.0, t)) * (1.0 - smoothstep(0.1, 0.35, isoW));
        chrome += neon(0.3 + hRel * 0.4) * isoLine * (0.08 + 0.9 * kPulse) * 0.6;
        // Crests glow from within.
        chrome += neon(0.5) * smoothstep(0.75, 1.0, hRel) * (0.05 + 0.25 * kPulse);
        col = chrome;

        // Violet haze toward the horizon.
        float fog = 1.0 - exp(-t * 0.02);
        col = mix(col, ocSky(normalize(vec3(rd.x, 0.015, rd.z))) * 0.8 + vec3(0.04, 0.0, 0.08), fog * 0.8);
    }

    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = aces(col * 1.5);
    fragColor = vec4(grade(col), 1.0);
}
