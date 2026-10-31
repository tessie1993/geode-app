#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_nebula

// Nebula Cluster: a dense globular star cluster, as if through a lens held
// slightly out of focus for its brightest members. Thousands of point stars
// crowd toward the centre on a King-like profile over the faint glow of the
// stars too small to resolve; the brightest few dozen are drawn as soft,
// flat bokeh discs with a brighter rim, the way an out-of-focus lens renders
// a point.
//
// The bokeh layers are this style's own: one disc per grid cell, its radius
// and jitter chosen so the disc never crosses its cell (the same rule
// nebStars keeps), and the chance that a cell holds a disc is read at the
// cell's CENTRE, so the crowding toward the core never slices a disc in half.
//
// ---- audio ------------------------------------------------------------------
//
// Every onset IGNITES A HANDFUL OF STARS: the onset counter (SND_ONSET_COUNT)
// re-hashes which cells flare, so each hit picks a new random dozen; those
// discs swell and take a palette hue as the onset envelope (SND_ONSET_ENV) rises,
// then settle back. Only those discs change - the field around them holds
// still. The hats twinkle the point stars. The key turns the glow's hue. The
// core glow follows SND_LEVEL gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the twinkle and the shimmer of the glow. Complexity:
// uMorph adds octaves to the unresolved glow's grain (nebOctaves). Chaos:
// uWarp/uTurbulence scatter the stars through nebWarp2; uTwist via view().
// Colour: the glow and the ignition hues are pal(). Energy: uIntensity scales
// how far an ignition swells. Space: view().

#define CL_BOKEH_LAYERS 2

void main() {
    vec2 uv = view();
    float t = nebTime();
    float e = nebEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    // Clamped: an ignited disc may not outgrow its cell, whatever the Energy.
    float onset = clamp(SND_ONSET_ENV * e, 0.0, 1.0);
    float oct = nebOctaves(2.0);
    float key = nebKeyShift(0.1);

    vec2 p = nebWarp2(uv, t);
    float r2 = dot(p, p);
    float profile = 1.0 / (1.0 + r2 / 0.06);

    // The unresolved stars: a soft glow with a fine grain.
    float grain = nebFbm2(p * 22.0 + vec2(t * 0.01, 0.0), oct);
    vec3 col = nebEmission(0.1 + key, 0.6) * profile * (0.16 + 0.1 * grain) * (0.85 + 0.2 * swell);

    // Resolved point stars, crowding toward the core.
    col += nebStars(p, 11.0, 3, vec2(0.0), 0.8, 0.08 + 0.7 * profile, hats, t);

    // Bokeh discs for the brightest; onsets ignite a fresh random few.
    float seed = mod(floor(SND_ONSET_COUNT), 512.0);
    for (int i = 0; i < CL_BOKEH_LAYERS; i++) {
        float fi = float(i);
        float scale = 3.2 + 2.1 * fi;
        vec2 shift = fi * vec2(3.7, 9.1);
        vec2 q = p * scale + shift;
        vec2 cell = floor(q);
        vec2 f = q - cell - 0.5;
        vec2 h2 = nebHash22(cell + 13.0);
        float h = nebHash(cell * 1.7 + 5.0);
        vec2 cc = (cell + 0.5 - shift) / scale;
        float fill = 0.12 + 0.55 / (1.0 + dot(cc, cc) / 0.08);
        float on = 1.0 - smoothstep(fill - 0.03, fill, h);
        float pick = nebHash(cell + vec2(seed * 0.731, fi * 11.0 + 2.0));
        float ignite = smoothstep(0.86, 0.9, pick) * onset;
        float rad = (0.13 + 0.08 * h2.y) * (1.0 + 0.4 * ignite);
        // Jitter sized for the largest an ignited disc grows, so it swells in place.
        vec2 off = (h2 - 0.5) * 2.0 * 0.16;
        float d = length(f - off);
        float disc = 1.0 - smoothstep(rad - 0.035, rad, d);
        float rim = smoothstep(rad * 0.5, rad, d) * disc;
        float shape = disc * (0.35 + 0.55 * rim);
        vec3 tint = mix(nebStarTint(h2.x), nebEmission(nebHash(cell + seed) + key, 0.3), ignite);
        col += tint * shape * (on * 0.24 + ignite * 0.4) / (1.0 + fi * 0.6);
    }

    fragColor = vec4(grade(col), 1.0);
}
