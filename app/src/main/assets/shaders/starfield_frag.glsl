#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
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

// Starfield: a true 3D field of band-lit stars, flown through on the shared
// camera rig (camFly), so travel, look, banking, FOV and dolly all come from
// the rig and the composite leaves the picture alone.
//
// What reacts to what:
//   - the rig's travel carries the eye through the field; nothing here has a
//     clock of its own for that, so the speed is the Camera tab's.
//   - each star is lit by its own analyser band (aband), as the 2D version's
//     were, and its hue is its band position on the palette.
//   - hats (uHits.z) twinkle a hashed subset of stars, re-picked on every
//     onset through uHitCount. Local sparkle only: a twinkle brightens one
//     star, never the frame.
//   - onsets (uHits.w) stretch every star along the travel axis into a
//     streak, and the stretch falls back with the envelope.
//   - the core glow behind the field rides the slew-limited bass.
//
// The field is cell-hashed: space is cut into cubes of 1/STAR_SCALE world
// units and each cube holds at most one star. A ray walks the cubes it
// crosses (Amanatides-Woo), so a star is a fixed point in the world and the
// view can turn any way the rig points it.

/** Star cells per world unit. A power of two, so the z tiling below stays a power of two too. */
#define STAR_SCALE 4.0
/**
 * The field repeats along z every this many cells: 8192 world units (the rig's
 * uCamPos.z wrap) times STAR_SCALE, minus one, as an AND mask. Tiling with the
 * rig's own wrap period is what makes that wrap land on the same stars.
 */
#define STAR_Z_MASK 32767
/** Compile-time ceiling on the cell walk (the old loop's 48). uSteps scales the runtime budget. */
#define STAR_MAX_CELLS 48
/** Fraction of cells that hold a star. */
#define STAR_DENSITY 0.2
/**
 * Most cell crossings a unit of ray length can cost: |x|+|y|+|z| of a unit
 * vector, at most sqrt(3). The walk ends on a STEP count, so a ray along an
 * axis would see three times deeper than a diagonal one and the field would
 * show a bright cross; fading at budget / this makes every direction see the
 * same depth.
 */
#define STAR_CROSSINGS 1.75
/** A star's physical radius in cells, before the pixel floor below. */
#define STAR_SIZE 0.012
/**
 * Streak half-length in cells at full onset. The z jitter (0.45..0.55) leaves
 * room for it inside the cell; a longer streak would be cut at the cell wall,
 * which is why only near stars smear visibly (as real motion blur does).
 */
#define STAR_STREAK 0.4

void main() {
    // view() first, so zoom, rotation, kaleidoscope and the rest still steer
    // the ray, then the rig turns that screen point into a world ray.
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    // Work relative to the eye's own cell: the cell ids are exact integers in
    // float, and every distance below is measured in [0, 1)-sized offsets
    // rather than at a world z of thousands, where float has no fraction left.
    vec3 sp = ro * STAR_SCALE;
    vec3 base = floor(sp);
    vec3 o = sp - base;
    ivec3 baseId = ivec3(base);

    vec3 stepDir = vec3(rd.x >= 0.0 ? 1.0 : -1.0, rd.y >= 0.0 ? 1.0 : -1.0, rd.z >= 0.0 ? 1.0 : -1.0);
    ivec3 stepId = ivec3(stepDir);
    vec3 tDelta = 1.0 / max(abs(rd), vec3(1e-5));
    vec3 side = (stepDir * (0.5 - o) + 0.5) * tDelta;
    ivec3 cell = ivec3(0);

    // Pixel footprint as an angle: the screen is two units tall at uCamFocal.
    float pixAngle = 2.0 / (max(uResolution.y, 1.0) * uCamFocal);
    float streak = STAR_STREAK * clamp(uHits.w, 0.0, 1.0);
    float hat = clamp(uHits.z, 0.0, 1.0);
    float twinkleSeed = fract(uHitCount * 0.618034);
    float budget = uSteps * (float(STAR_MAX_CELLS) / 128.0);
    float far = budget / STAR_CROSSINGS;

    vec3 col = vec3(0.0);
    for (int i = 0; i < STAR_MAX_CELLS; i++) {
        if (float(i) >= budget) break;
        ivec3 id = baseId + cell;
        id.z = id.z & STAR_Z_MASK;
        uint h = uhash3(uvec3(id));
        float hOcc = uhashUnit(h);
        uint h2 = uhash(h);
        uint h3 = uhash(h2);
        uint h4 = uhash(h3);
        uint h5 = uhash(h4);
        // Jittered inside the cell with a margin, so a star's glow is never
        // cut by the cell wall the ray did not cross.
        vec3 star = vec3(cell) + vec3(0.25 + 0.5 * uhashUnit(h2), 0.25 + 0.5 * uhashUnit(h3), 0.45 + 0.1 * uhashUnit(h4));
        float hue = uhashUnit(h5);

        vec3 rel = star - o;
        float tc = max(dot(rel, rd), 0.0);
        vec3 dv = rd * tc - rel;
        float size = STAR_SIZE + tc * pixAngle * 1.4;
        // The onset streak: the star becomes an ellipsoid along z, the travel
        // axis, which on screen is a radial smear from the vanishing point.
        dv.z *= size / (size + streak);
        float glow = exp(-dot(dv, dv) / (size * size));

        float fade = (1.0 - smoothstep(far * 0.35, far, tc)) * smoothstep(0.05, 0.7, tc);
        // One star in seven, so a busy hi-hat sparkles across the field
        // without lifting the field as a whole.
        float twinkle = 1.0 + 1.8 * hat * step(0.86, fract(hue * 7.31 + twinkleSeed));
        float lit = (0.35 + aband(hue)) * twinkle;
        col += pal(hue + tc * 0.01) * glow * fade * lit * step(hOcc, STAR_DENSITY);

        // Step into whichever neighbouring cell the ray reaches first.
        vec3 m = step(side, side.yzx) * step(side, side.zxy);
        side += m * tDelta;
        cell += ivec3(m) * stepId;
    }

    // The core glow sits on the world's +z, the direction of travel on the
    // glide and spiral paths, so it is the vanishing point you fly toward.
    float core = rd.z > 0.0 ? exp(-8.0 * length(rd.xy) / rd.z) : 0.0;
    col += pal(0.5) * core * (0.2 + uBassSmooth * 0.8);
    fragColor = vec4(grade(col), 1.0);
}
