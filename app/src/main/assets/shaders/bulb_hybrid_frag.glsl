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
//#include lib_sdf3
//#include lib_bulb

// Bulb Hybrid: a bulb that alternates its power every iteration - 7, then 3,
// then 7 - so the orbit is pulled between a seven-fold crown and a lumpy
// three-fold knot, and the body is neither: a squat, gourd-like vessel with
// crowns of small knobs. The derivative takes each step's own power, which is
// exact for an alternating map: the chain rule multiplies the two stretches in
// turn.
//
// Grid cell INSIDE x LINE (see lib_bulb). The vessel is turned inside out
// like Cavern's (q = K p / |p|^2, so its outside folds into a closed room and
// its knobs hang from the walls), and the room is PORCELAIN: white walls with
// almost no shading, drawn in cobalt brush lines - iso-lines of the orbit
// trap's distance to the pole axis - with a blue-black outline wherever a wall
// turns away from the eye and in the deepest creases. The far end fades to
// white, not to dark. Cavern is the same idea lit like a wet grotto; this is
// its negative: a pale room drawn in ink, the family's dark-on-light interior.
//
// THE ESTIMATE. As in Cavern: inversion scales lengths by K / |p|^2, so the
// estimate is converted back by |p|^2 / K and the march takes 0.6 of it.
//
// ---- audio ------------------------------------------------------------------
//
// No beat tips the room any more (owner 2026-11-12, no style spins to the
// music); the accumulated rotation uDrift turns the room slowly round. Tonal passages (uHarmony) multiply the brushwork, percussive
// ones thin it. Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the room's idle turn. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps
// the clay before it is inverted (bulbChaos in q, estimate divided by
// bulbChaosLip). Colour is tone(),
// the delft ramp pal() tints. Space is the rig: travel drifts the eye round a
// small loop at the room's centre, look and shake ride on top (bulbAim).

#define BY_MAX_STEPS 128
#define BY_K 2.6
#define BY_FAR 7.0
#define BY_LOOP_R 0.45
const float BY_LOOP_RATE = 5.0 * CAM_SPIN_UNIT;

mat3 gPose;
int gIters;

// Its own ramp: delft cobalt, ultramarine and an ink violet.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.18, 0.26, 0.55), vec3(0.12, 0.15, 0.25), vec3(1.0), vec3(0.60, 0.55, 0.50));
}

float hybridDE(vec3 p, out vec4 tr) {
    vec3 z = p;
    float dr = 1.0;
    float r = max(length(z), 1e-6);
    float esc = float(gIters);
    tr = vec4(dot(p, p), abs(p.y), length(p.xz), 1.0);
    for (int i = 0; i < BULB_MAX_ITERS; i++) {
        if (i >= gIters) break;
        r = max(length(z), 1e-6);
        if (r > 2.0) {
            esc = float(i);
            break;
        }
        float n = (i % 2 == 0) ? 7.0 : 3.0;
        dr = n * pow(r, n - 1.0) * dr + 1.0;
        z = bulbPow(z, r, n, n, vec2(0.0)) + p;
        tr.x = min(tr.x, dot(z, z));
        tr.y = min(tr.y, abs(z.y));
        tr.z = min(tr.z, length(z.xz));
    }
    r = max(length(z), 1e-6);
    tr.w = esc / float(max(gIters, 1));
    return 0.5 * log(r) * r / dr;
}

/** The style's body: its distance and orbit trap (mat unused). */
BulbHit bulbMap(vec3 p) {
    p = gPose * p;
    float r2 = max(dot(p, p), 1e-4);
    vec3 q = p * (BY_K / r2);
    vec4 tr;
    float d = hybridDE(bulbChaos(q), tr) / bulbChaosLip();
    return BulbHit(d * (r2 / BY_K) * 0.6, tr, 0.0);
}

//#include lib_bulb_field

/** A thin line on every integer of f, `w` pixels wide, antialiased. */
float lineOn(float f, float w) {
    float fw = max(fwidth(f), 1e-4);
    float dd = abs(fract(f + 0.5) - 0.5) / fw;
    return 1.0 - smoothstep(0.5 * w, 0.5 * w + 1.0, dd);
}

void main() {
    vec2 uv = view();
    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);

    float harmony = clamp(uHarmony, 0.0, 1.0);
    gPose = rotX(-0.3) * rotY(uDrift * 0.5 + 0.06 * bulbTime());
    // Four iterations, as in Cavern: inverted, the finest crust is a fur the
    // march never resolves; fewer leave smooth porcelain to draw on.
    gIters = bulbIters(4);

    // INSIDE: a small loop at the room's centre, looking along it.
    float a = uCamTravel * BY_LOOP_RATE;
    vec3 eye = vec3(BY_LOOP_R * cos(a), 0.15 * sin(2.0 * a), BY_LOOP_R * sin(a));
    vec3 fwd = normalize(vec3(-sin(a), 0.2 * cos(2.0 * a), cos(a)));
    vec3 ro;
    vec3 rd;
    bulbAim(uv, eye, eye + fwd, ro, rd);
    // Grow and layers (lib_grow): the body at the origin is the subject, measured from this eye, kept off it.
    growSetup(ro, 3.0, true);

    float hue = 0.02 * sin(bulbTime() * 0.01);
    vec3 paper = vec3(0.93, 0.92, 0.89);

    float t = 0.02;
    float hitT = -1.0;
    float used = 0.0;
    vec4 trap = vec4(1.0);
    // The last step's trap, for a ray that ran out of steps at a wall.
    vec4 lastTrap = vec4(1.0);
    for (int i = 0; i < BY_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > BY_FAR) break;
        used = float(i);
        BulbHit h = bulbField(ro + rd * t);
        float d = h.d;
        lastTrap = h.trap;
        float eps = bulbEps(t) * 2.5;
        if (d < eps) {
            hitT = t;
            trap = h.trap;
            break;
        }
        t += max(d, eps * 0.5);
    }
    // A ray that ran out of steps short of the far wall is at a wall, crawling
    // a crease: shade it as a hit (Cavern's rule).
    if (hitT < 0.0 && t < BY_FAR) {
        hitT = t;
        trap = lastTrap;
    }

    // Brush coordinates for every pixel (fwidth needs the whole quad).
    float bands = 4.0 + 4.0 * harmony;
    float brush = lineOn(trap.z * bands, 1.2 + 1.6 * harmony);
    float rings = lineOn(sqrt(trap.x) * 3.0, 0.9);

    vec3 col = paper;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = bulbNormal(p, bulbEps(hitT) * 1.5);
        float ndv = dot(n, -rd);
        // Porcelain: white, with only a whisper of light to tell the walls apart.
        vec3 body = paper * (0.86 + 0.14 * max(dot(n, normalize(vec3(-0.3, 0.8, -0.4))), 0.0));
        vec3 cobalt = tone(hue + 0.1 * trap.w);
        body = mix(body, cobalt, brush * 0.85);
        body = mix(body, tone(hue + 0.3), rings * 0.45);
        // Ink: where a wall turns away from the eye, and in the deepest creases.
        float edge = (1.0 - smoothstep(0.18, 0.30, ndv));
        float crease = smoothstep(0.55, 0.75, used / max(uSteps, 1.0));
        body = mix(body, tone(hue + 0.5) * 0.25, max(edge, crease));
        // Distance fades to white, not to dark.
        col = mix(body, paper, 1.0 - exp(-hitT * 0.20));
    }

    col *= 0.9 + 0.1 * swell;
    fragColor = vec4(grade(col), 1.0);
}
