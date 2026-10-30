#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_lucid

// Lucid Tunnel: flying down a tube whose wall is a sine lattice folded
// through itself four times, lit like a black-light poster. The wall is drawn
// in (angle, depth) coordinates - depth is 1/radius, so the centre is the far
// end - and every term of the wall field uses only WHOLE multiples of the
// angle, so the lattice wraps the tube with no seam even after it has been
// pushed around by its own warp (a field periodic in the angle, displaced by
// offsets that are themselves periodic in the angle, stays periodic).
//
// Black-light recolour: the wall is near-black and only the zero crossings
// of the field glow, in two saturated palette colours that trade places down
// the tube, so it reads as day-glo paint under UV.
//
// ---- audio ------------------------------------------------------------------
//
// A DROP (uEvents.y) corkscrews the tunnel: it winds a twist into the wall
// that grows with depth and spins the far end round, easing in as the drop's
// 2.5 s envelope falls and unwinding after it (lucidBump: continuous across
// the hit, no snap). Flight speed rides uFlowPhase. uEnergySmooth widens the
// glowing lines a little. Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the flight. Complexity: uMorph past 0.5
// adds a nest level. Chaos: uTurbulence/uWarp raise how far each level pushes
// the next; uTwist (in lucidView) winds the wall. Color: both glow colours and
// the wall's undertone are pal(), saturated. Energy: uIntensity scales the
// drop corkscrew and the line widening. Space: uZoom/uRotation through
// lucidView. (The user's kaleidoscope applies through lucidView.)

/** The wall: sine lattice, every angular frequency a whole number. x = angle, y = depth. */
float wall(vec2 q, float level) {
    float a = q.x;
    float z = q.y;
    float s = sin(3.0 * a + 1.7 * sin(z * 0.9 + 2.0 * a + level));
    s *= cos(2.0 * a - z * 1.3 + level * 1.9);
    s += 0.5 * sin(5.0 * a + z * 2.1 - level);
    return s * 0.66;
}

/** Push a palette colour toward full saturation: fluorescent paint, not pastel. */
vec3 dayglo(vec3 c) {
    float lo = min(c.r, min(c.g, c.b));
    float hi = max(c.r, max(c.g, c.b));
    return clamp((c - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
}

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float energy = clamp(uEnergySmooth, 0.0, 1.5) * uIntensity;
    float swell = clamp(uSwell, 0.0, 1.5);

    int depthLevels = lucidDetail(4, 3);
    float chaos = lucidChaos();

    float r = max(length(uv), 1e-3);
    float ang = atan(uv.y, uv.x);
    float z = 0.42 / r;
    float travel = flow * 0.9 + t * 0.25;

    float screw = lucidBump(drop) * uIntensity;
    ang += (0.12 + 0.55 * screw) * z + 1.4 * screw + t * 0.04;
    vec2 q = vec2(ang, z + travel);

    // The nest, written out for this field: whole-number angular offsets only.
    vec2 push = vec2(0.55, 0.9) * chaos;
    vec2 w = vec2(0.0);
    for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
        if (i >= depthLevels) break;
        float lv = float(i);
        vec2 qi = vec2(q.x, q.y * (0.6 + 0.2 * lv)) + push * w;
        w = vec2(wall(qi, lv * 1.3), wall(qi + vec2(0.0, 3.1), lv * 2.1 + 0.7));
    }
    float f = wall(q + push * w, 4.2);

    float width = 0.06 + 0.04 * clamp(energy, 0.0, 1.0);
    float line = width / (abs(f) + width);
    line = line * line;
    float band = 0.5 + 0.5 * sin(q.y * 0.7 + w.x * 1.5);
    vec3 glow = mix(dayglo(pal(0.5)), dayglo(pal(0.85)), smoothstep(0.2, 0.8, band));
    vec3 col = pal(0.75) * (0.05 + 0.04 * w.y);
    col += glow * line * 1.1;
    col = lucidCubic(col, 0.4);

    // The far end is dark; the near wall rushes past.
    col *= smoothstep(0.0, 0.22, r) * (0.6 + 0.4 * smoothstep(0.0, 1.2, r));
    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
