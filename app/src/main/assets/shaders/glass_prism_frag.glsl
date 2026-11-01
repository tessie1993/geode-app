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
//#include lib_glass

// Glass Prism: a triangular prism on a dark stage, a white beam striking its
// left face and leaving the right as a fan of rainbow that lands on a screen
// at the right as a spectrum band. Drawn in the plane of the prism's
// cross-section, where the optics are exact: the beam is clipped against the
// triangle's three faces (a 2D slab test), refracted in by Snell, crossed to
// whichever face it reaches first and refracted out - once per wavelength,
// each with its own IOR (glassIors). Every pixel between the red and blue exit
// rays finds its wavelength from its angle about the fan's virtual apex, so the
// band is continuous for the cost of three traces.
//
// Two bounces: the exit face also reflects its Fresnel share back inside; that
// ray crosses to the third face and leaves as a second, fainter fan, the ghost
// every real prism throws. The glass body has bevelled edges that catch the
// light, shows the stage behind it displaced by refraction, and glows faintly
// where the beam crosses it. Haze in the air lights up along every ray, with
// curling smoke (noise warped by noise) that the rays carve into bright
// ribbons.
//
// ---- audio ------------------------------------------------------------------
//
//   centroid       the fan's width: bright mixes spray a broad rainbow, dull ones a
//                  narrow ribbon (the IOR spread);
//   pitch height   the beam's angle of incidence: the fan swings up and down the
//                  screen with the melody;
//   snare          fires a bright packet of light down the beam, through the glass
//                  and out across the fan;
//   bass           the beam's width;
//   pan            rocks the prism;
//   flatness       thickens the haze (frost): noise fills the room with light;
//   hat            dust motes sparkle in the beams.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the rock and the dust drift. Complexity raises the base IOR,
// bending the fan further. Chaos jitters the faces, so the fan shivers. Energy
// scales every reaction. Colour: the stage and a share of the rainbow from pal().
//
// ---- cost -------------------------------------------------------------------
//
// Four 3-plane traces per pixel (three wavelengths and the ghost), a handful of
// segment distances and two noise reads. Flat and analytic; Saver-cheap.

#define PR_IOR 1.5
#define PR_INRADIUS 0.4
#define PR_SCREEN 1.55

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
    GlassEar ear = glassListen();
    float spread = 0.015 + 0.045 * ear.sharp;
    float ior = PR_IOR + 0.12 * glassComplexity();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chaos = glassChaos();
    float haze = 0.6 + 0.8 * ear.frost;
    float bw = 1.0 + 1.5 * clamp(ear.bass, 0.0, 1.2);

    gC = vec2(-0.45, -0.12);
    float rock = 0.42 + 0.07 * sin(t * 0.11) + 0.12 * ear.pan;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float a = rock - 1.5707963 + fi * 2.0943951 + chaos * 0.05 * sin(t * 0.9 + fi * 2.3);
        gN[i] = vec2(cos(a), sin(a));
    }

    vec2 aim = gC + gN[2] * PR_INRADIUS;
    vec2 d = normalize(vec2(1.0, 0.3 + 0.32 * ear.height + 0.03 * sin(t * 0.09)));
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

    // The ghost: the exit face's reflected share crosses to the third face and leaves.
    int fi0;
    int fo0;
    vec2 d1 = refract2(d, gN[2], 1.0 / ior);
    vec2 t1 = prismClip(pin + d1 * 1e-3, d1, fi0, fo0);
    vec2 dIn = reflect(d1, -gN[fo0]);
    vec2 gStart = peG + dIn * 1e-3;
    int fi1;
    int fo1;
    vec2 t2 = prismClip(gStart, dIn, fi1, fo1);
    vec2 gOut = gStart + dIn * t2.y;
    vec2 gDir = normalize(refract2(dIn, -gN[fo1], ior));
    float ghostK = glassFresnel(abs(dot(d1, gN[fo0])), ior) * 4.0;

    // The stage: a back wall and floor in the palette's deep colour, with a screen at the right.
    // Curling smoke for the light to catch: value noise read through a domain warp of itself.
    vec2 sw = vec2(glassNoise(uv * 2.0 + vec2(t * 0.07, 0.0)), glassNoise(uv * 2.3 + vec2(0.0, -t * 0.09) + 5.0));
    float smoke = 0.5 + 0.5 * glassNoise(uv * 3.5 + 1.6 * sw + vec2(-t * 0.12, t * 0.05));
    float dust = 0.25 + 1.5 * smoke * smoke;
    vec3 stage = mix(pal(hue + 0.6) * 0.05, pal(hue + 0.3) * 0.16, smoothstep(-1.2, 1.0, uv.y + 0.3 * uv.x));
    stage += pal(hue + 0.1) * 0.08 * exp(-dot(uv - vec2(-0.4, -0.1), uv - vec2(-0.4, -0.1)) * 1.5);
    stage *= 1.0 - 0.5 * (1.0 - smoothstep(-0.62, -0.55, uv.y));

    // Inside or outside the glass, and how far from an edge (for the bevel).
    float inside = -1e3;
    for (int i = 0; i < 3; i++) inside = max(inside, dot(gN[i], uv - gC) - PR_INRADIUS);
    float body = 1.0 - smoothstep(-0.003, 0.003, inside);

    // The fan, as before: wavelength from the angle about its virtual apex.
    float cr = cross2(dR, dB);
    vec2 apex = peG - dG;
    if (abs(cr) > 1e-4) apex = peR + dR * (cross2(peB - peR, dB) / cr);
    vec2 u = uv - apex;
    float angU = atan(cross2(dR, u), dot(dR, u));
    float angB = atan(cr, dot(dR, dB));
    float s = angU / angB;
    float along = dot(uv - peG, dG);
    float band = smoothstep(-0.08, 0.06, s) * (1.0 - smoothstep(0.94, 1.08, s));
    float beyond = step(PR_SCREEN, uv.x);
    float fan = band * smoothstep(0.0, 0.05, along) * (1.0 - body) * step(0.0, dot(u, dG)) * (1.0 - beyond);
    vec3 rainbow = mix(glassSpectrum(clamp(s, 0.0, 1.0)), pal(hue + 0.7 * s), 0.25);
    // The snare's packet: a bright pulse travelling the whole path.
    float pulseAt = (1.0 - clamp(SND_SNARE_ENV, 0.0, 1.0)) * 9.0 - 6.0;
    float pathPos = along;
    float pulse = ear.snare * exp(-(pathPos - pulseAt) * (pathPos - pulseAt) * 8.0) * 2.0;
    vec3 col = stage;
    col += rainbow * fan * (0.45 * haze * dust + pulse) / (1.0 + 0.5 * max(along, 0.0));
    // Where the fan lands: a bright spectrum band on the screen.
    float screen = smoothstep(PR_SCREEN - 0.004, PR_SCREEN + 0.004, uv.x);
    col = mix(col, stage * 0.6 + rainbow * band * step(0.0, dot(u, dG)) * 1.6, screen);
    col += rainbow * band * step(0.0, dot(u, dG)) * exp(-abs(uv.x - PR_SCREEN) * 25.0) * 0.6 * (1.0 - screen);

    // The ghost fan: one ray's worth, thin and faint.
    vec2 gu = uv - gOut;
    float gAlong = dot(gu, gDir);
    float gd = length(gu - gDir * max(gAlong, 0.0));
    float gw = 0.004 + 0.03 * max(gAlong, 0.0) * spread * 4.0;
    col += mix(vec3(1.0), pal(hue + 0.3), 0.3) * ghostK * exp(-gd * gd / (gw * gw)) * step(0.0, gAlong) * (1.0 - body) * (0.4 + 0.6 * haze * dust) * (1.0 - screen);

    // The white beam in, with its haze halo and the snare's packet.
    float bd = segDist(uv, a, pin);
    float bAlong = dot(uv - pin, d);
    float bPulse = ear.snare * exp(-(bAlong - pulseAt) * (bAlong - pulseAt) * 8.0) * 3.0;
    col += vec3(1.0, 0.98, 0.94) * (exp(-bd * bd * 9000.0 / (bw * bw)) * (1.3 + bPulse) + exp(-bd * 25.0 / bw) * 0.1 * haze * dust);

    // Dust motes in the light: sparkles where a beam or the fan passes, on the hat.
    vec2 mg = uv * 30.0 + vec2(t * 0.3, sin(t * 0.2));
    vec2 mid = floor(mg);
    float mh = glassHash(mid);
    float lightHere = clamp(fan * 4.0 + exp(-bd * 12.0), 0.0, 1.0);
    if (mh > 0.9) {
        float md = length(fract(mg) - 0.5 - (glassHash2(mid) - 0.5) * 0.5);
        col += vec3(1.0) * exp(-md * md * 120.0) * lightHere * (0.2 + 1.5 * ear.hat);
    }

    // The prism: the stage behind it shifted by refraction, the beam's path glowing inside, bevelled edges.
    vec2 shift = -gN[0] * 0.12;
    vec3 behind = mix(pal(hue + 0.6) * 0.05, pal(hue + 0.3) * 0.16, smoothstep(-1.2, 1.0, uv.y + shift.y + 0.3 * (uv.x + shift.x)));
    vec3 glass = behind * 1.3 + pal(hue + 0.55) * 0.03;
    float inner = segDist(uv, pin, peG);
    glass += mix(vec3(1.0), glassSpectrum(0.5), 0.3) * (exp(-inner * inner * 3000.0) * 0.8 + exp(-inner * 18.0) * 0.12);
    float ginner = segDist(uv, peG, gOut);
    glass += vec3(0.8, 0.85, 1.0) * exp(-ginner * ginner * 5000.0) * ghostK * 0.5;
    float bevel = exp(-abs(inside) * 90.0);
    // Edge highlights: brightest on the faces turned toward the beam.
    float facing = 0.0;
    for (int i = 0; i < 3; i++) {
        float on = exp(-abs(dot(gN[i], uv - gC) - PR_INRADIUS) * 120.0);
        facing += on * (0.3 + 0.7 * max(dot(gN[i], -d), 0.0));
    }
    glass += vec3(0.85, 0.9, 1.0) * (bevel * 0.25 + facing * 0.6);
    col = mix(col, glass, body);
    col += vec3(1.0, 0.97, 0.92) * exp(-length(uv - pin) * 30.0) * (0.5 + bPulse * 0.2);
    col += rainbow * exp(-length(uv - peG) * 26.0) * 0.35;

    fragColor = vec4(grade(glassTone(col)), 1.0);
}
