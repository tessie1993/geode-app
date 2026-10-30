#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Prism: a triangular glass prism on a dark stage, a thin white beam
// striking its left face and leaving the right face as a fan of rainbow. Drawn
// in the plane of the prism's cross-section, where the optics are exact: the
// beam is clipped against the triangle's three faces (the Kay-Kajiya slab test,
// in 2D), refracted in by Snell, crossed to whichever face it reaches first,
// and refracted out - once for red, once for green, once for blue, each with
// its own IOR (glassIors). The red and blue exit rays are the edges of the fan;
// every pixel between them finds its wavelength from its angle about the
// fan's virtual apex (where the two edge rays meet when traced backward), so
// the band is continuous and costs no more than the three traces. Dust in the
// air catches the beam and the fan, and the prism itself is a dark glass block
// whose edges catch the light.
//
// ---- audio ------------------------------------------------------------------
//
// Highs (uTrebleSmooth, steadied with the presence band uBandsHi.y) widen the
// fan: they raise the IOR spread, so a bright cymbal-heavy passage throws a
// broad rainbow and a dull one a narrow ribbon. The beam's angle of incidence
// wanders slowly with uOrbit (re-targeted on novelty and section boundaries),
// so the whole fan swings over seconds. The key tints the dark stage and
// leans the rainbow toward the palette. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the prism's slow rock and the dust drift. Complexity
// (uMorph) raises the base IOR, bending the fan further down. Chaos (uWarp,
// uTurbulence) jitters the faces, so the fan shivers and splits. Energy
// (uIntensity) scales how far the highs widen it. Colour: the stage and a
// share of the rainbow from pal(). Space is the rig's flat projection.

#define PR_IOR 1.5
#define PR_INRADIUS 0.32

vec2 gN[3];
vec2 gC;

float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

vec2 refract2(vec2 i, vec2 n, float eta) {
    vec2 r = refract(i, n, eta);
    return dot(r, r) < 1e-6 ? reflect(i, n) : r;
}

/** The ray a + t d against the triangle: (t in, t out) and the faces it enters and leaves by. */
vec2 prismClip(vec2 a, vec2 d, out int fin, out int fout) {
    float tin = -1e5;
    float tout = 1e5;
    fin = 0;
    fout = 0;
    for (int i = 0; i < 3; i++) {
        float den = dot(gN[i], d);
        float num = PR_INRADIUS - dot(gN[i], a - gC);
        if (abs(den) < 1e-6) {
            if (num < 0.0) tout = -1e5;
        } else {
            float tt = num / den;
            if (den < 0.0) {
                if (tt > tin) {
                    tin = tt;
                    fin = i;
                }
            } else if (tt < tout) {
                tout = tt;
                fout = i;
            }
        }
    }
    return vec2(tin, tout);
}

/** One wavelength through the prism: entry point, exit point and exit direction. */
void prismTrace(vec2 a, vec2 d, float ior, out vec2 pin, out vec2 pout, out vec2 dir) {
    int fi;
    int fo;
    vec2 tt = prismClip(a, d, fi, fo);
    pin = a + d * tt.x;
    vec2 d1 = refract2(d, gN[fi], 1.0 / ior);
    vec2 start = pin + d1 * 1e-3;
    vec2 t2 = prismClip(start, d1, fi, fo);
    pout = start + d1 * t2.y;
    dir = normalize(refract2(d1, -gN[fo], ior));
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float h = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - a - ab * h);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float highs = clamp(0.5 * uTrebleSmooth + 0.5 * uBandsHi.y, 0.0, 1.5);
    float spread = 0.028 + 0.07 * highs * glassEnergy();
    float ior = PR_IOR + 0.12 * glassComplexity();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chaos = glassChaos();

    gC = vec2(-0.3, -0.1);
    float rock = 0.07 * sin(t * 0.11) + 0.04 * uOrbit.x;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float a = rock - 1.5707963 + fi * 2.0943951 + chaos * 0.05 * sin(t * 0.9 + fi * 2.3);
        gN[i] = vec2(cos(a), sin(a));
    }

    // The beam aims at the middle of the left face, rising at about the angle
    // of minimum deviation so the light crosses the glass near horizontal.
    vec2 aim = gC + gN[2] * PR_INRADIUS;
    vec2 d = normalize(vec2(1.0, 0.34 + 0.06 * uOrbit.y + 0.03 * sin(t * 0.09)));
    vec2 a = aim - d * 6.0;

    vec3 iors = glassIors(ior, spread);
    vec2 pin;
    vec2 peR;
    vec2 peG;
    vec2 peB;
    vec2 dR;
    vec2 dG;
    vec2 dB;
    prismTrace(a, d, iors.x, pin, peR, dR);
    prismTrace(a, d, iors.y, pin, peG, dG);
    prismTrace(a, d, iors.z, pin, peB, dB);

    // The dark stage.
    vec3 col = mix(pal(hue + 0.6) * 0.015, pal(hue + 0.5) * 0.06, smoothstep(-1.0, 1.0, uv.y));
    float dust = 0.7 + 0.3 * glassNoise(uv * 7.0 + vec2(-t * 0.12, t * 0.05));

    // Inside or outside the glass.
    float inside = -1e3;
    for (int i = 0; i < 3; i++) inside = max(inside, dot(gN[i], uv - gC) - PR_INRADIUS);
    float body = (1.0 - smoothstep(-0.004, 0.004, inside));

    // The fan: the angle about the virtual apex, from the red ray to the blue.
    float cr = cross2(dR, dB);
    vec2 apex = peG - dG;
    if (abs(cr) > 1e-4) apex = peR + dR * (cross2(peB - peR, dB) / cr);
    vec2 u = uv - apex;
    float angU = atan(cross2(dR, u), dot(dR, u));
    float angB = atan(cr, dot(dR, dB));
    float s = angU / angB;
    float along = dot(uv - peG, dG);
    float band = smoothstep(-0.08, 0.06, s) * (1.0 - smoothstep(0.94, 1.08, s));
    float fan = band * smoothstep(0.0, 0.05, along) * (1.0 - body) * step(0.0, dot(u, dG));
    vec3 rainbow = mix(glassSpectrum(clamp(s, 0.0, 1.0)), pal(hue + 0.7 * s), 0.3);
    col += rainbow * fan * 0.95 / (1.0 + 0.7 * max(along, 0.0)) * dust;

    // The white beam in, with a soft halo of lit dust.
    float bd = segDist(uv, a, pin);
    col += vec3(1.0, 0.98, 0.94) * (exp(-bd * bd * 9000.0) * 1.3 + exp(-bd * 30.0) * 0.12 * dust);

    // The prism: dark glass, the beam crossing it, lit edges, an entry flare.
    vec3 glass = pal(hue + 0.55) * 0.05 + vec3(0.02, 0.025, 0.03);
    glass += pal(hue + 0.5) * 0.06 * smoothstep(gC.y - PR_INRADIUS, gC.y + 2.0 * PR_INRADIUS, uv.y);
    float inner = segDist(uv, pin, peG);
    glass += mix(vec3(1.0), glassSpectrum(0.5), 0.3) * exp(-inner * inner * 4000.0) * 0.7;
    glass += vec3(0.85, 0.9, 1.0) * exp(-abs(inside) * 140.0) * 0.45;
    col = mix(col, glass, body);
    col += vec3(1.0, 0.97, 0.92) * exp(-length(uv - pin) * 38.0) * 0.45;
    col += rainbow * exp(-length(uv - peG) * 30.0) * 0.25;

    col *= 0.85 + 0.18 * swell;
    fragColor = vec4(grade(col), 1.0);
}
