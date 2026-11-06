#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the vanes below, not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Spiral: the flat style's log-spiral arms, built as a vortex you fly down.
//
// The scene is a tube around the z axis whose wall grows spiral vanes, thin
// blades that curl inward from the wall toward an open core. In every cross
// section a blade is a logarithmic spiral arm, the 2D style's picture exactly
// (phase = arms * angle + wind * log(r) - time); along z the arms also wind as
// a helix, so the tunnel is a rifled vortex turning about the flight axis.
// The vanes stop at an inner radius, leaving an open core for the flight, and
// down that core runs a glowing filament: the spiral's bright eye, seen
// end-on as the hot centre at the vanishing point and side-on as a thread
// of light when the rig wanders off the axis. Rings of light run along the
// blades toward the eye, sharpened by the level as the 2D arms were.
//
// The camera is the shared rig (camFly). Every z term has a period dividing
// 16 (the helix wind is 3 turns per 16 units, the wobble 1 per 8, the hue
// drift 1 per 16), and the eye is rebased by 16, a power of two that divides
// the rig's 8192 travel wrap, so neither the wrap nor the rebase ever shows.
//
// Controls kept from the 2D style: Morph sets the arm count (2..6, as before,
// now crossfaded between counts instead of jumping), Warp curls the arms
// harder, Turbulence wobbles the blades along z, Intensity and the rest of
// the grade as everywhere. Rotation and Sway still roll the view.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_ATT)        the arms wind tighter, as in 2D, and the eye
//                            filament burns brighter.
// Level (SND_LEVEL)          the light rings on the blades sharpen and
//                            brighten (the 2D arms' pow(v, 3 - level)).
// Kick (SND_KICK_ENV)        the blades thicken, pulsing the vortex walls.
// Snare (SND_SNARE_ENV)      the blades' inner edges flare in a contrasting hue.
// Treble (SND_TREBLE_ATT)    the specular sheen on the blades.
// Drop (SND_HELD_SURGE)      the vortex opens: the core widens and the palette
//                            swaps to its far side, on a hump, so it glides.
// Pitch (SND_HELD_NOTE_CHANGE) slides the arm hue, as a hump.
// SND_INTENSITY              overall level.
//
// ---- knobs -----------------------------------------------------------------
//
// k0 PULSE the kick's thickening; k1 TEXTURE the ring density on the blades;
// k2 FLOW extra radial winding; k3 TONE the hue; k4 BUILD the eye's glow;
// k5 SPACE the width of the open core. All read as continuous offsets around
// 0.5, never multiplying a clock, so a knob move never jumps the picture.
//
// ---- why the distance estimate is safe --------------------------------------
//
// A blade is the set where the phase s sits within gSpTh of a multiple of
// 2pi. Its estimate is the phase distance divided by |grad s|, which is known
// in closed form: the angle and log(r) terms are orthogonal with gradient
// arms / r and wind / r, the helix term is the constant SP_KZ, and the wobble
// adds at most its own amplitude times its rates. That is a first-order
// estimate of a curved sheet, so it is capped at SP_SHEET_CAP and the march
// steps at 0.7 of it. The blades are cut to r > gSpIn by a max, the outer wall
// r < SP_OUT is a plain cylinder (exact), and a sphere around the eye is
// carved out of everything, so the eye never starts inside a blade. The
// filament is not geometry: it is an analytic glow, so it cannot be hit.

#define SP_MAX_STEPS 96
#define SP_FAR 42.0
#define SP_ZWRAP 16.0
#define SP_TAU 6.2831853
/** Helix wind along z: 3 turns of phase per 16 world units. */
#define SP_KZ (SP_TAU * 3.0 / 16.0)
/** Blade wobble along z: 1 period per 8 world units. */
#define SP_KW (SP_TAU / 8.0)
#define SP_OUT 5.6
#define SP_BUBBLE 0.8
#define SP_SHEET_CAP 0.9
#define SP_FOG 0.045

float gSpArmsA;
float gSpArmsB;
float gSpArmMix;
float gSpWind;
float gSpTh;
float gSpIn;
float gSpTurb;
float gSpSpin;
vec3 gSpEye;

/** Signed distance to the blades of an `arms`-armed spiral at phase base, radius r. */
float spSheet(float ang, float base, float arms, float r, float wobGrad) {
    float s = ang * arms + base;
    float u = abs(fract(s / SP_TAU + 0.5) - 0.5) * SP_TAU;
    float g = sqrt((arms * arms + gSpWind * gSpWind) / (r * r) + SP_KZ * SP_KZ) + wobGrad;
    return (u - gSpTh) / g;
}

float map(vec3 w) {
    float r = max(length(w.xy), 1e-3);
    // atan(0, 0) is undefined in GLSL ES; the guard only matters on the axis,
    // deep in the open core where the max below makes the angle irrelevant.
    float ang = r > 2e-3 ? atan(w.y, w.x) : 0.0;
    float wobA = gSpTurb * 0.35;
    float wob = wobA * sin(w.z * SP_KW + ang * 2.0 + uTime * 0.6);
    float wobGrad = wobA * (SP_KW + 2.0 / r);
    float base = gSpWind * log(r) + SP_KZ * w.z - gSpSpin + wob;
    float dA = spSheet(ang, base, gSpArmsA, r, wobGrad);
    float dB = spSheet(ang, base, gSpArmsB, r, wobGrad);
    float d = min(mix(dA, dB, gSpArmMix), SP_SHEET_CAP);
    // Blades only outside the core, with a rounded inner edge.
    d = smax(d, gSpIn - r, 0.25);
    // The outer tube wall.
    d = min(d, SP_OUT - r);
    d = max(d, SP_BUBBLE - length(w - gSpEye));
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float bass = sndLevel(SND_BASS_ATT);
    float level = sndShaped(SND_LEVEL);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float treble = sndLevel(SND_TREBLE_ATT);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    // Morph picks the arm count as the 2D style did (2 + floor(morph * 4)),
    // crossfading into the next count over the last fifth of each step.
    float m = clamp(uMorph, 0.0, 1.0) * 4.0;
    float mf = floor(min(m, 3.999));
    gSpArmsA = 2.0 + mf;
    gSpArmsB = gSpArmsA + 1.0;
    gSpArmMix = smoothstep(0.8, 1.0, m - mf);
    gSpWind = 2.5 + 3.0 * clamp(bass, 0.0, 1.2) + 3.0 * clamp(uWarp, 0.0, 2.0) + 2.0 * (uKnob[2] - 0.5);
    gSpTh = 0.42 + (0.45 + 0.5 * (uKnob[0] - 0.5)) * kick;
    gSpIn = 2.8 + 0.8 * (uKnob[5] - 0.5) + 0.9 * dropBump;
    gSpTurb = clamp(uTurbulence, 0.0, 2.0);
    // The 2D arms' own spin, a plain multiple of uTime.
    gSpSpin = uTime * 1.8;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 16, so this rebase is exact.
    ro.z = mod(ro.z, SP_ZWRAP);
    gSpEye = ro;

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < SP_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > SP_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.7, eps);
    }

    float hue = 0.04 * sin(uTime * 0.02) + 0.5 * dropBump + 0.22 * pitchBump + (uKnob[3] - 0.5) * 0.4;
    float ahead = pow(max(rd.z, 0.0), 10.0);
    vec3 fogCol = pal(hue + 0.5) * (0.03 + 0.18 * ahead) + vec3(0.004, 0.003, 0.008);
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : SP_FAR;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float r = max(length(p.xy), 1e-3);
        float ang = r > 2e-3 ? atan(p.y, p.x) : 0.0;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 32.0);
        // The core light: the filament on the axis lights the blades' inner faces.
        vec3 toAxis = normalize(vec3(-p.xy, 0.0) + 1e-4);
        float core = max(dot(n, toAxis), 0.0) * exp(-(r - gSpIn) * 0.6);

        // The 2D colour law, angle plus radius, plus a slow drift along z
        // whose period is 16, the rebase.
        float ch = ang / SP_TAU + r * 0.12 + 0.08 * sin(p.z * SP_TAU / 16.0) + hue;
        vec3 base = pal(ch);
        // Rings travelling inward along the blades toward the eye, sharpened
        // by the level exactly as the 2D arms were.
        float ringK = 5.0 + 8.0 * uKnob[1];
        float v = 0.5 + 0.5 * sin(log(r) * ringK * 2.0 + uTime * 2.2);
        v = pow(v, 3.0 - clamp(level, 0.0, 1.0) * 1.6);
        vec3 surf = base * (0.08 + 0.55 * ndv) * ao
                  + base * v * (0.25 + level) * (0.6 + 0.4 * ndv)
                  + pal(hue + 0.5) * core * (0.3 + 0.7 * bass) * ao
                  + vec3(spec) * (0.15 + 0.5 * clamp(treble, 0.0, 1.0)) * ao;
        // The blades' inner edges flare on the snare.
        float edge = 1.0 - smoothstep(0.0, 0.45, r - gSpIn);
        surf += pal(ch + 0.5) * edge * (0.12 + 1.1 * snare);
        // The outer wall sits back in shadow so the blades read in front of it.
        surf *= mix(1.0, 0.35, smoothstep(SP_OUT - 0.5, SP_OUT - 0.05, r));
        col = mix(surf, fogCol, 1.0 - exp(-hitT * SP_FOG));
    }

    // The eye: a glowing filament on the z axis, integrated analytically along
    // the ray. Closest approach in xy is dPerp at t0; a Gaussian profile
    // integrates to sqrt(pi) / (k * s) over the line, clipped to the visible
    // length, and faded where the closest point is behind the eye or the wall.
    float s = max(length(rd.xy), 1e-4);
    float dPerp = abs(ro.x * rd.y - ro.y * rd.x) / s;
    float t0 = -dot(ro.xy, rd.xy) / (s * s);
    float k = 3.0;
    float along = min(1.7725 / (k * s), sceneT);
    float seen = smoothstep(-1.0, 1.0, t0 * s * k) * smoothstep(-1.0, 1.0, (sceneT - t0) * s * k);
    float fil = exp(-k * k * dPerp * dPerp) * along * seen;
    float eyeGain = (0.12 + 0.6 * bass) * (0.6 + 0.8 * uKnob[4]);
    vec3 eyeCol = mix(pal(hue + 0.5), vec3(1.0), 0.3);
    col += eyeCol * (1.0 - exp(-fil * eyeGain * 0.5)) * 0.9;

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
