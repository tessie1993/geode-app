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
//#include lib_truchet

// Truchet Maze: Truchet's original diagonal split, the one-line "10 PRINT"
// labyrinth. Each cell holds a single wall on one of its diagonals; the walls
// meet corner to corner into a maze of corridors, drawn as glowing tubes of
// light on a dark floor. Small and dense, it reads as a circuit seen from far
// above.
//
// ---- audio ------------------------------------------------------------------
//
// On every beat a pulse of light runs out through the maze from the centre:
// a diamond-shaped front (the maze's own 45-degree geometry) set by the
// analyser's beat-clock phase (sndPhase(SND_HIT_TIME, 1.0)), lighting the walls it
// passes and fading as it travels, so it is a moving band, never a full-frame
// flash; it fades away in silence (sndShaped(SND_LEVEL)). Onsets swing a scattered
// third of the walls a quarter turn onto the other diagonal, so corridors
// open and close on the hits. The bass breathes the scale. The walls' hue
// shifts with distance from the centre and with the key.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the hue drift and the wall shimmer. Complexity: uMorph
// packs the maze tighter. Chaos: uWarp/uTurbulence bend the maze before it is
// tiled; uTwist in view(). Energy: uIntensity scales the pulse and the bass
// breathing. Colour: walls, glow and pulse are pal(). Space: view().

/** Distance from f (relative to the cell centre) to cell id's wall, re-rolled by onsets. */
float wallDist(vec2 f, vec2 id, float hits) {
    vec3 roll = truchetRoll(id, hits, SND_ONSET_ENV, 3.0);
    float bit = step(0.5, roll.y);
    if (step(0.5, roll.x) != bit && roll.z < 1.0) {
        // Swing the old wall a quarter turn onto the new diagonal.
        f = truchetRot(-roll.z * TRUCHET_PI * 0.5) * f;
        bit = step(0.5, roll.x);
    }
    // The wall is the diagonal segment from corner to corner.
    vec2 end = bit > 0.5 ? vec2(0.5, -0.5) : vec2(0.5, 0.5);
    float h = clamp(dot(f, end) / dot(end, end), -1.0, 1.0);
    return length(f - end * h);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float morph = truchetMorph();

    float scale = (4.5 + 2.5 * morph) * truchetBreath(SND_BASS_PRESENCE);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    // The four cells around p (its own among them), so the glow measures the
    // true nearest wall: a cell's own diagonal is not always the closest
    // wall, and a halo measured from it alone would step at every border.
    float hits = floor(SND_ONSET_COUNT);
    vec2 base = floor(p - 0.5);
    float ad = 1e3;
    for (int k = 0; k < 4; k++) {
        vec2 id = base + vec2(float(k - 2 * (k / 2)), float(k / 2));
        ad = min(ad, wallDist(p - id - 0.5, id, hits));
    }

    // The beat pulse: a diamond front running outward once per beat.
    float diamond = (abs(p.x) + abs(p.y)) / scale;
    float ph = sndPhase(SND_HIT_TIME, 1.0);
    float front = ph * 3.2;
    float amp = smoothstep(0.0, 0.08, ph) * (1.0 - ph) * clamp(sndShaped(SND_LEVEL) * 2.5, 0.0, 1.0) * e;
    float ring = exp(-pow((diamond - front) * 5.0, 2.0)) * amp;

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.18 * diamond;
    vec3 wallC = pal(hue + 0.55);
    vec3 pulseC = pal(hue + 0.12);

    float core = truchetLine(ad, 0.035, px);
    float glow = exp(-ad * 16.0) * 0.45 + exp(-ad * 5.0) * 0.12;
    // A faint shimmer runs along the walls; continuous across cells.
    float shimmer = 0.85 + 0.15 * sin((p.x - p.y) * 2.1 + t * 0.7) * sin((p.x + p.y) * 1.7 - t * 0.5);

    vec3 col = pal(hue + 0.8) * 0.03;
    col += wallC * (glow * shimmer + core * 0.7);
    col += pulseC * ring * (core * 1.1 + glow * 1.4);
    col += vec3(1.0) * core * 0.18;

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
