#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_bulb

// Bulb Hybrid: a bulb that alternates its power every iteration - 7, then 3,
// then 7 - so the orbit is pulled between a seven-fold crown and a lumpy
// three-fold knot, and the body is neither: a squat, gourd-like vessel with
// crowns of small knobs. Glazed like porcelain: a white body, a glossy clear
// coat with a sharp highlight and a soft one, and cobalt brushwork laid in
// the bands where the orbit hugged the pole axis. The camera holds still - a
// fixed three-quarter view on the rig, no orbit - and the piece turns on its
// own like a pot on a wheel, against a warm studio grey.
//
// The derivative takes each step's own power, which is exact for an
// alternating map: the chain rule multiplies the two stretches in turn.
//
// ---- audio ------------------------------------------------------------------
//
// The BEAT nods the vessel. Each beat (uEvents.x, 0.3 s) tips it toward the
// eye and back on a bump envelope, so it bobs in time like a head keeping the
// beat, while the accumulated rotation uDrift (eased across sections) turns
// it on its wheel. Tonal passages (uHarmony) spread the cobalt brushwork,
// percussive ones thin it. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the wheel's idle turn. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps
// the clay (bulbChaos, estimate divided by bulbChaosLip). Energy (uIntensity)
// scales the beat nod. Colour is pal() (glaze, ground); Space is the rig
// (camAround, no spin).

#define BY_MAX_STEPS 128
#define BY_BOUND 1.45
#define BY_CAM_R 3.3

mat3 gPose;
int gIters;
vec4 gTrap;

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

float map(vec3 p) {
    vec4 tr;
    float d = hybridDE(bulbChaos(gPose * p), tr) / bulbChaosLip();
    gTrap = tr;
    return d;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float beat = clamp(uEvents.x, 0.0, 1.0);
    float nod = 4.0 * beat * (1.0 - beat);
    float harmony = clamp(uHarmony, 0.0, 1.0);
    gPose = rotX(-0.25 * nod * bulbEnergy() - 0.3) * rotY(uDrift * 0.5 + 0.06 * bulbTime());
    gIters = bulbIters(7);

    vec3 ro;
    vec3 rd;
    camAround(uv, BY_CAM_R, 0.0, ro, rd);

    float hue = 0.62 + 0.02 * sin(bulbTime() * 0.01);

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BY_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BY_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            near = min(near, d);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.8, eps * 0.5);
        }
    }

    // Warm studio grey, lighter toward the top, and a soft contact shade
    // under the piece.
    vec3 ground = mix(vec3(0.20, 0.18, 0.16), mix(pal(hue + 0.45), vec3(0.5, 0.47, 0.43), 0.75) * 0.7, smoothstep(-0.8, 0.6, uv.y));
    vec3 col = ground * (1.0 - 0.35 * exp(-max(near, 0.0) * 6.0));

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        vec3 key = normalize(vec3(-0.45, 0.75, -0.5));
        float dif = max(dot(n, key), 0.0);
        float fill = 0.5 + 0.5 * n.y;
        float ndv = max(dot(n, -rd), 0.0);
        float ao = bulbStepAO(used) * (0.55 + 0.45 * smoothstep(0.05, 0.8, trap.x));
        // Cobalt brushwork in the bands near the pole axis.
        float brush = smoothstep(0.10 + 0.12 * harmony, 0.02, abs(fract(trap.z * 4.0) - 0.5) - 0.3);
        vec3 porcelain = vec3(0.92, 0.91, 0.88);
        vec3 cobalt = pal(hue) * 0.35;
        vec3 body = mix(porcelain, cobalt, brush * 0.85);
        col = body * (0.25 + 0.75 * dif + 0.25 * fill) * ao;
        // Clear coat: a sharp highlight, a broad sheen, and a fresnel lift.
        vec3 r = reflect(rd, n);
        col += vec3(1.0) * pow(max(dot(r, key), 0.0), 90.0) * 0.8 * ao;
        col += vec3(1.0) * pow(max(dot(r, key), 0.0), 8.0) * 0.08 * ao;
        col += ground * pow(1.0 - ndv, 4.0) * 0.6;
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
