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
//#include lib_glass

// Glass Pool: looking straight down into a swimming pool on a sunny day. Pale
// tiles with grey grout and a dark lane stripe lie under a sheet of rippling
// water; the ripples bend the tile lines (the floor is read at the point the
// refracted ray lands, offset by the surface slope), and the light that got
// through them lies across the floor as a moving net of thin bright caustic
// lines. Glints of sun sparkle on the crests. Flat, analytic, one pass: the
// five-wave surface of glassWaves gives height, slope and the caustic in one
// call, and the deep end of the pool, at the top of the frame, is darker.
//
// ---- audio ------------------------------------------------------------------
//
// Kicks (uHits.x) drop a stone: a ring of ripples launches from a point and
// spreads across the pool as the kick envelope falls, bending the tiles and
// raking a line of glints across the surface as it goes - the ripple is a
// slope added to the surface, so it moves the picture without lighting it. Each
// onset (uHitCount, uHits.w) slides the drop point to a new spot. The key
// (uKeyHue, uKeyStrength) tints the water a little. Brightness follows uSwell.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the waves. Complexity (uMorph) adds lane stripes.
// Chaos (uWarp, uTurbulence) makes the water choppier, so the caustic net
// tightens and the tiles swim. Energy (uIntensity) scales the kick ripples.
// Colour: the water, the tiles' tint and the lane from pal(). Space is the
// rig's flat projection.

#define PL_TILE 0.42
#define PL_DEPTH 0.30

void main() {
    vec2 uv = view();
    float t = glassTime();
    float energy = glassEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float hue = 0.5 + 0.08 * uKeyStrength * sin(GLASS_TAU * uKeyHue);

    vec2 p = uv * 1.6;
    vec4 w = glassWaves(p * 1.7, t * 0.8, 1.0 + 0.8 * glassChaos(), 0.85);
    vec2 slope = w.yz;

    // The stone: a damped ring wave launched at the kick, travelling out as
    // the envelope falls. Its slope is added to the surface's.
    float kick = clamp(uHits.x, 0.0, 1.0);
    vec2 rc = (vec2(glassSeed(uHitCount, uHits.w), glassSeed(uHitCount + 37.0, uHits.w)) - 0.5) * vec2(2.4, 1.4);
    vec2 rel = p - rc;
    float dist = length(rel);
    float x = dist - (0.06 + 1.9 * (1.0 - kick));
    float ringEnv = exp(-x * x * 22.0);
    float amp = 0.09 * kick * energy;
    float dh = amp * ringEnv * (13.0 * cos(x * 13.0) - 44.0 * x * sin(x * 13.0));
    slope += dh * rel / max(dist, 1e-3);

    // The floor, read where the refracted ray lands.
    vec2 fp = p + slope * PL_DEPTH;
    vec2 q = fp / PL_TILE;
    vec2 cell = floor(q);
    vec2 f = fract(q);
    float edge = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    float grout = 1.0 - smoothstep(0.03, 0.065, edge);
    float th = glassHash(cell + 11.0);
    vec3 tile = mix(vec3(0.84, 0.93, 0.96), pal(hue + 0.08 * th), 0.22) * (0.93 + 0.07 * th);
    vec3 floorCol = mix(tile, vec3(0.52, 0.6, 0.64), grout);
    float laneW = PL_TILE * mix(7.0, 4.0, step(0.5, glassComplexity()));
    float laneDist = abs(fract(fp.x / laneW + 0.5) - 0.5) * laneW;
    float lane = 1.0 - smoothstep(0.085, 0.11, laneDist);
    floorCol = mix(floorCol, pal(hue + 0.62) * 0.3 + vec3(0.02, 0.05, 0.12), lane);

    // The caustic net, and the water's colour above it.
    float caustic = glassCaustic(w.w, 0.12);
    vec3 water = mix(vec3(0.18, 0.6, 0.78), pal(hue), 0.3);
    float deep = smoothstep(-1.3, 1.3, uv.y);
    vec3 col = floorCol * water * (0.5 + 1.3 * caustic * vec3(1.0, 0.98, 0.9)) * mix(1.05, 0.55, deep);

    // The surface: sun glints on the crests and a faint sheen on the slopes.
    vec3 n = normalize(vec3(-slope * 1.2, 1.0));
    vec3 sun = normalize(vec3(-0.4, 0.5, 0.75));
    float spec = pow(max(dot(reflect(vec3(0.0, 0.0, -1.0), n), sun), 0.0), 90.0);
    col += vec3(1.0, 0.97, 0.9) * spec * 0.6 + water * pow(1.0 - n.z, 1.5) * 0.35;

    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
