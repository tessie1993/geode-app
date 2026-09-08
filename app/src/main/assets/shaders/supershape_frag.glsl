#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_superformula
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch

// Supershape: the formula, as a body.
//
// One solid, raymarched: the spherical product of the two music-driven
// superformula curves (lib_superformula, viz/SuperShape). Drawn not as a
// surface but as a SHELL of luminous motes along its parameter lines, seen
// through to its far side, so it reads as a lattice hanging in the dark
// rather than as a lit object - the look of the reference clips.
//
// ---- what the music does here -----------------------------------------------
//
// Almost nothing in this file reads a level. The body is whatever the
// superformula currently is, and the superformula is what the music moves:
//
//   snare       walks the lobe count (a crossfade to the next closed curve)
//               and turns the body a little
//   kick        pinches every lobe toward a point, relaxing back over ~300ms
//   bass/treble fatten the cosine and sine lobes respectively
//   hats        twinkle the sine lobes
//   harmonicity a pad sits round, a drum break sits spiky
//   buildup     the snare walk climbs, so the silhouette gets busier
//   drop        the count resets to a bold low one, and the body is held
//               larger for the seconds after
//   section     a new count, the spin reverses, a new lattice density
//   bar clock   a nod once per bar, faded in with uRhythmLock
//   key         tints the shell, faded in with uKeyStrength
//
// So a change in the music is a change in WHAT IS DRAWN, not in how big or
// how bright. The only luminance terms are uSpike and uArrival, both
// slew-limited.
//
// ---- the march ---------------------------------------------------------------
//
// sdSupershape() already divides the field by the bound SuperShape computed
// this frame, so the surface march steps by it directly. The far side is
// found by a second, bounded march INSIDE the body: the sign of the field
// flips at the exit and a four-step bisection pins it, so that march is
// robust even where the bound is not (near the poles, at a cusp). It spends
// SS_BACK_STEPS at most, inside the RAYMARCH caps in lib_sdf3.

#define SS_MAX_STEPS 128
#define SS_BACK_STEPS 32
#define SS_BISECT 4
#define SS_FAR 8.0
#define SS_FOCAL 1.6
#define SS_TAU 6.2831853
#define SS_PI 3.14159265

// Shared by the march, the normal taps and the shell material.
float gRadius;
mat3 gTilt;
float gKeyShift;
vec2 gLattice;

float map(vec3 p) {
    return sdSupershape(gTilt * p, gRadius);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

// The shell at a surface point. `facing` is the surface normal as seen from
// the ray - outward on the near side, inward on the far side.
vec3 shell(vec3 p, vec3 facing, vec3 rd, float weight) {
    vec2 ang = shapeAngles(gTilt * p);
    // Parameter lines: so many around, so many pole to pole. The motes sit at
    // the crossings, a faint wire joins them.
    vec2 g = vec2(ang.x / SS_TAU * gLattice.x, (ang.y / SS_PI + 0.5) * gLattice.y);
    vec2 cell = abs(fract(g) - 0.5);
    float mote = smoothstep(0.34, 0.06, length(cell));
    float wire = smoothstep(0.05, 0.0, min(cell.x, cell.y)) * 0.22;
    // A shell is densest where the view grazes it, which is what makes the
    // rim bright and the middle sparse.
    float graze = pow(1.0 - clamp(abs(dot(facing, -rd)), 0.0, 1.0), 2.0);
    // The tips: where the silhouette reaches furthest out, the motes burn white.
    float tip = smoothstep(1.15, 1.9, ashape(ang.x));
    float latitude = ang.y / SS_PI + 0.5;
    vec3 tint = pal(0.52 + 0.20 * (latitude - 0.5) + gKeyShift);
    vec3 col = tint * (mote * (0.45 + 1.1 * graze) + wire * (0.6 + 0.8 * graze));
    col += vec3(1.0) * mote * tip * 0.85;
    col += tint * graze * 0.10;
    return col * weight;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float finger = touchFalloff(uv, 0.6);

    // The body. Held larger for the seconds after a drop (uDrop is a state
    // with a 0.35 Hz release, so this is a slow inflation, not a pop).
    gRadius = 0.92 * (1.0 + 0.06 * swell + 0.14 * uDrop + 0.08 * finger);
    // A precession so the pole is not always at the camera, and a nod once
    // per bar that only shows once the bar clock can be trusted.
    float nod = 0.16 * uRhythmLock * sin(uBarPhase * SS_TAU);
    gTilt = rotX(0.55 + 0.22 * sin(uTime * 0.09) + nod) * rotY(uTime * 0.05);
    gKeyShift = (uKeyHue - 0.5) * 0.30 * uKeyStrength;
    // Each section gets its own lattice density; the glide between them
    // slides the motes along the parameter lines rather than re-dealing them.
    gLattice = vec2(36.0 + 16.0 * uSectionPhase, 18.0 + 8.0 * uSectionPhase);

    vec3 ro = vec3(0.0, 0.0, -3.3);
    mat3 cam = rotY(0.12 * sin(uTime * 0.08) + uMoveDir.x * 0.20) * rotX(0.08 * sin(uTime * 0.11) + uMoveDir.y * 0.20);
    ro = cam * ro;
    vec3 rd = cam * normalize(vec3(uv, SS_FOCAL));

    // ---- the near side ------------------------------------------------------
    float t = 0.0;
    float hitT = -1.0;
    // Closest approach in world units - the halo for rays that miss.
    float near = 1e9;
    for (int i = 0; i < SS_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > SS_FAR) break;
        float d = map(ro + rd * t);
        float eps = 0.0009 * t + 0.0004;
        near = min(near, d * uShapeLip.y);
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d, eps);
    }

    // ---- the far side -------------------------------------------------------
    float backT = -1.0;
    if (hitT > 0.0) {
        float tb = hitT + 0.01;
        float inside = tb;
        for (int i = 0; i < SS_BACK_STEPS; i++) {
            if (tb > SS_FAR) break;
            float d = map(ro + rd * tb);
            if (d > 0.0) {
                // Stepped out through the far wall: bisect back onto it.
                float lo = inside;
                float hi = tb;
                for (int k = 0; k < SS_BISECT; k++) {
                    float mid = 0.5 * (lo + hi);
                    if (map(ro + rd * mid) > 0.0) hi = mid; else lo = mid;
                }
                backT = 0.5 * (lo + hi);
                break;
            }
            inside = tb;
            tb += max(-d, 0.004);
        }
    }

    vec3 fog = pal(0.60) * 0.030;
    vec3 col = fog;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float e = max(0.0012 * hitT, 0.0006);
        vec3 n = normalAt(p, e);
        col += shell(p, n, rd, 1.0);
        if (backT > 0.0) {
            vec3 pb = ro + rd * backT;
            vec3 nb = normalAt(pb, max(0.0012 * backT, 0.0006));
            // Seen from inside, so the facing normal is the inward one, and
            // the far side is dimmed by what it is seen through.
            col += shell(pb, -nb, rd, 0.42);
        }
    } else {
        // The halo: a true silhouette glow from the closest approach.
        col += pal(0.50 + gKeyShift) * exp(-near * 7.0) * (0.22 + 0.30 * uSpike + 0.20 * uDrop);
    }

    // Music returning after silence lights the whole shell; slew-limited.
    col *= 1.0 + 0.45 * uArrival;

    // The mote layer, so the style sits with the rest of the fluid family.
    col += mix(pal(0.52), vec3(1.0), 0.35) * fluidMotes(uv, 5.0, 0.14) * 0.16;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.70 + 0.30 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
