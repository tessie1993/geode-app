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
//#include lib_nebula

// Nebula Nova: a supernova's blast shell. A thin, bright shock front expands
// from a dying star, decelerating as it sweeps up gas; its edge breaks into
// fingers as it slows; it starts white-hot and cools through the palette to
// a dull red as it grows. Behind it a compact remnant glows at the centre;
// around it lies faint ambient nebulosity and a field of stars.
//
// The shell's age is the music's, not the clock's: it is one minus the
// envelope of the last drop or section change, so the blast happens ON the
// event and the shell coasts out to its full size as the envelope dies, then
// holds there as a cool remnant until the next one. When a new blast goes,
// the old shell does not vanish: it stays drawn at full size, fading on the
// new envelope, until the new front reaches it.
//
// The fingers are noise read on the circle of DIRECTIONS, not on an angle -
// no atan, no seam - and their pattern is re-rolled per section
// (uSectionCount), so each section's blast has its own shape.
//
// ---- audio ------------------------------------------------------------------
//
// A drop (uEvents.y) or a section change (uEvents.z) RESTARTS THE SHELL from
// the star. Its colour is its age: hot at the blast, cooling as it expands.
// The new shell is small when it is hottest, so the blast is a point of light,
// not a full-frame flash. The hats twinkle the stars. The key turns the cooled
// remnant's hue. Overall level follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the churn of the shell's texture and the twinkle (the
// expansion is the music's). Complexity: uMorph adds octaves to the shell
// (nebOctaves). Chaos: uWarp/uTurbulence curl the whole remnant (nebWarp2);
// uTwist via view(). Colour: the cooling ramp and the nebulosity are pal().
// Energy: uIntensity scales the fingers' reach. Space: view().

/** A shell at radius rr with the fingers already in it: sharp outside, softer behind the front. */
float shellAt(float r, float rr, float w) {
    float x = (r - rr) / w;
    return x > 0.0 ? exp(-x * x * 3.0) : exp(-x * x * 0.6);
}

void main() {
    vec2 uv = view();
    float t = nebTime();
    float e = nebEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float oct = nebOctaves(3.0);
    float key = nebKeyShift(0.1);

    float ev = max(clamp(uEvents.y, 0.0, 1.0), clamp(uEvents.z, 0.0, 1.0));
    float age = 1.0 - ev;
    float heat = pow(ev, 1.5);

    vec2 p = nebWarp2(uv, t);
    float r = length(p);
    vec2 dir = p / max(r, 1e-4);
    float sect = mod(uSectionCount, 64.0) * 1.37;

    float fingers = nebFbm2(dir * 3.0 + vec2(sect, t * 0.015), 3.0) - 0.5;
    float tex = nebFbm2(p * 7.0 + vec2(sect, -t * 0.03), oct);

    // The current shell: decelerating, its fingers growing as it slows.
    float R = 0.06 + 0.92 * pow(age, 0.55);
    float rr = R * (1.0 + fingers * (0.1 + 0.3 * age) * (0.5 + 0.5 * e));
    float w = 0.02 + 0.07 * R;
    float shell = shellAt(r, rr, w) * (0.5 + 0.9 * tex);
    vec3 hot = nebEmission(mix(0.02, 0.6, heat) + key * (1.0 - heat), heat);
    vec3 col = hot * shell * (0.55 + 0.45 * heat);
    col += hot * 0.1 * (1.0 - smoothstep(0.0, rr, r)) * tex;

    // The previous shell, at full size, fading on the new envelope.
    float rOld = 0.98 * (1.0 + fingers * 0.4 * (0.5 + 0.5 * e));
    float old = shellAt(r, rOld, 0.089) * (0.5 + 0.9 * tex) * ev * (1.0 - smoothstep(rOld * 0.6, rOld * 0.9, rr));
    col += nebEmission(0.02 + key, 0.0) * old * 0.55;

    // The compact remnant.
    col += vec3(0.85, 0.92, 1.0) * (exp(-r * r * 1200.0) * 1.0 + exp(-r * 14.0) * 0.2 * (0.4 + 0.6 * heat));

    // Ambient nebulosity and stars.
    float amb = nebFbm2(p * 1.4 + vec2(9.1, 2.3), 3.0);
    col += nebEmission(0.55 + key, 0.0) * smoothstep(0.5, 0.85, amb) * 0.07;
    col += nebStars(uv, 8.0, 2, vec2(0.0), 1.0, 0.2, hats, t) * (1.0 - 0.6 * min(shell, 1.0));

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
