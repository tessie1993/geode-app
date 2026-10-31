#version 300 es
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

// Particle Tunnel: a curving tube of glowing points and streaks, flown down.
//
// There is no surface. The ray walks the unit grid cell by cell (a 3D DDA, so
// each cell it crosses is visited exactly once). A cell in the tube's shell
// may hold one particle, and the particle's glow comes from the ray's closest
// approach to it, worked out exactly, so no step size can skip or alias it.
// Streaks are the same points measured in a space squashed along z.
//
// The camera is the shared rig (camFly). The tube's centre line and the cell
// hash both repeat with power-of-two periods along z, so the 8192 wrap is
// invisible. The shell starts at radius 3.95, outside the rig's wander.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (SND_HAT_ENV) make about half the particles swell and twinkle; the rest
// stay put. An onset (SND_ONSET_ENV) spawns a thin ring ahead that expands out to
// the wall as its envelope falls, coloured by SND_ONSET_COUNT so each one differs.
// A section change (SND_HELD_HARD_CUT) steps the whole tunnel's hue by a third and
// glides it back over the event's 1.5 s. Overall level rides SND_LEVEL.

/** Cells walked per ray: the compile-time ceiling. uSteps is the runtime budget. */
#define PT_MAX_CELLS 96
#define PT_TAU 6.2831853
#define PT_RADIUS 5.0
/** Half-thickness of the particle shell around PT_RADIUS. */
#define PT_SHELL 1.05
#define PT_FAR 60.0
/** Cells along z before the hash repeats: 1024 world units, which divides 8192. */
#define PT_ZWRAP 1024.0

vec2 tubeCentre(float z) {
    return vec2(0.8 * sin(z * (PT_TAU / 128.0)), 0.6 * sin(z * (PT_TAU / 64.0) + 1.3));
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, PT_ZWRAP);

    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float hueStep = 0.33 * smoothstep(0.0, 0.3, clamp(SND_HELD_HARD_CUT, 0.0, 1.0));

    // DDA setup. A zero direction component is nudged so its crossing is
    // effectively never reached instead of dividing by zero.
    vec3 cell = floor(ro);
    vec3 pos = step(0.0, rd);
    vec3 dirSign = pos * 2.0 - 1.0;
    vec3 inv = 1.0 / max(abs(rd), vec3(1e-4));
    vec3 tMax = (cell + pos - ro) * dirSign * inv;

    vec3 col = vec3(0.0);
    for (int i = 0; i < PT_MAX_CELLS; i++) {
        if (float(i) >= uSteps) break;
        vec3 c = cell;
        float tExit = min(tMax.x, min(tMax.y, tMax.z));
        vec3 m = step(tMax.xyz, tMax.yzx) * step(tMax.xyz, tMax.zxy);
        tMax += m * inv;
        cell += m * dirSign;
        if (tExit > PT_FAR) break;

        vec3 cc = c + 0.5;
        vec2 rel = cc.xy - tubeCentre(cc.z);
        float rr = length(rel);
        // Outside the shell and heading further out: this ray is done.
        if (rr > PT_RADIUS + PT_SHELL + 0.9 && dot(rel, rd.xy) > 0.0) break;
        if (abs(rr - PT_RADIUS) > PT_SHELL) continue;

        uint u = uhash3(uvec3(ivec3(int(c.x), int(c.y), int(mod(c.z, PT_ZWRAP)))));
        if (uhashUnit(u) < 0.6) continue;
        uint u2 = uhash(u);
        vec3 jit = vec3(float(u2 & 1023u), float((u2 >> 10u) & 1023u), float((u2 >> 20u) & 1023u)) * (1.0 / 1023.0);
        uint u3 = uhash(u2);
        float s1 = uhashUnit(u3);
        float s2 = uhashUnit(uhash(u3));

        // Kept inside 0.1..0.9 of the cell so the glow is not cut at its edge.
        vec3 pt = c + 0.2 + 0.6 * jit;
        pt += 0.1 * sin(vec3(uTime * 0.7, uTime * 0.53, uTime * 0.37) + jit * 6.0);
        float streak = s1 > 0.72 ? 1.0 : 0.0;
        // Streaks sit mid-cell along z so their length has room.
        pt.z = mix(pt.z, c.z + 0.5, streak);
        float stretch = mix(1.0, 2.0 + 1.8 * s2, streak);

        vec3 o = ro - pt;
        o.z /= stretch;
        vec3 dd = vec3(rd.xy, rd.z / stretch);
        float tc = -dot(o, dd) / dot(dd, dd);
        if (tc < 0.0) continue;
        vec3 cl = o + dd * tc;
        float d2 = dot(cl, cl);

        float spark = step(0.5, s2);
        float tw = 0.5 + 0.5 * sin(uTime * (7.0 + 11.0 * s1) + s2 * 61.0);
        float pop = hats * spark * tw;
        float size = (0.03 + 0.035 * s1) * (1.0 + 1.3 * pop);
        float k = size * size;
        float g = k / (d2 + k);
        float glow = g * g + 0.05 * g;
        // Fade with depth, and out right at the eye so nothing blows up.
        float fade = exp(-tc * 0.05) * smoothstep(0.2, 1.4, tc);
        float hue = hueStep + 0.25 * jit.x + 0.18 * sin(cc.z * (PT_TAU / 64.0)) + 0.10 * rel.y / max(rr, 1e-3);
        vec3 pc = mix(pal(hue), vec3(1.0), 0.15 * spark * tw + 0.5 * pop);
        col += pc * glow * fade * (0.9 + 1.6 * pop) * mix(1.0, 0.7, streak);
    }

    // The onset ring, in a plane ahead of the eye. It starts near the axis and
    // reaches the wall as the envelope falls.
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    if (onset > 0.002 && rd.z > 0.05) {
        float seed = fract(SND_ONSET_COUNT * 0.618);
        float zr = ro.z + 6.0 + 8.0 * seed;
        float tr = (zr - ro.z) / rd.z;
        vec3 q = ro + rd * tr;
        float ringR = PT_RADIUS * (1.0 - 0.8 * onset);
        float dr = length(q.xy - tubeCentre(zr)) - ringR;
        float w = 0.05 + 0.004 * tr;
        float ring = w * w / (dr * dr + w * w);
        col += mix(pal(seed + hueStep), vec3(1.0), 0.2) * ring * sqrt(onset) * 0.8 * exp(-tr * 0.05);
    }

    // The far end of the tube and a faint haze, so the black has depth.
    vec3 axis = normalize(vec3(tubeCentre(ro.z + 40.0) - ro.xy, 40.0));
    float endGlow = pow(max(dot(rd, axis), 0.0), 18.0);
    col += pal(0.62 + hueStep) * (0.03 + 0.25 * endGlow) + pal(0.80 + hueStep) * 0.015;

    col *= 0.85 + 0.35 * swell + 0.10 * energy;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
