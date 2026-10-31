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

// Nebula Spiral: a grand-design spiral galaxy seen face on. A warm, bright
// bulge; two blue arms winding out of it on a logarithmic spiral; a dark dust
// lane hugging the inner edge of each arm; pink star-forming knots strung
// along the arms; a speckle of resolved disk stars; field stars beyond.
//
// The arms are a DENSITY WAVE, which is how real spiral arms work and how
// this style moves: the arm pattern's phase advances slowly and its pitch
// breathes, while the clumpy gas and the stars stay where they are. So the
// arms sweep through the material - knots brighten as an arm reaches them and
// fade as it passes - and the frame itself never turns. The arm count is a
// whole number (nebSpiralWave) so there is no seam along the negative x axis.
//
// ---- audio ------------------------------------------------------------------
//
// A kick (SND_KICK_ENV) sends a ring of compression out through the disk: the arms
// brighten and the knots light up where the ring passes, travelling outward
// as the kick level falls. It brightens arm material only where the ring is,
// never the frame. Mids (sndShaped(SND_MIDDLE)) lift the star-forming knots. The key
// turns the arm hue. The bulge follows SND_LEVEL gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the arms' winding and the twinkle. Complexity: uMorph
// adds fbm octaves to the clumps (nebOctaves). Chaos: uWarp/uTurbulence warp
// the disk before the spiral is read (nebWarp2), bending the arms; uTwist via
// view(). Colour: bulge, arms, knots and dust from pal(). Energy: uIntensity
// scales the kick ring and the mid lift. Space: view().

void main() {
    vec2 uv = view();
    float t = nebTime();
    float e = nebEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float mid = clamp(sndShaped(SND_MIDDLE), 0.0, 1.3) * e;
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float oct = nebOctaves(4.0);
    float key = nebKeyShift(0.1);

    vec2 g = uv * 1.25;
    vec2 gw = nebWarp2(g, t);
    float r = length(gw);

    // The density wave: pattern phase and pitch move, the material does not.
    float wind = t * 0.035;
    float pitch = 0.36 + 0.05 * sin(t * 0.021);
    float wave = nebSpiralWave(gw, 2.0, pitch, wind);
    float arm = pow(0.5 + 0.5 * wave, 2.5);
    float lane = pow(0.5 + 0.5 * nebSpiralWave(gw, 2.0, pitch, wind + 0.8), 8.0);

    float disk = exp(-r * 2.5);
    float bulge = exp(-r * r / 0.02);
    float clump = nebFbm2(g * 3.2 + vec2(4.0, 1.3), oct);

    // The kick ring: where the compression front is right now.
    float front = 0.08 + 1.1 * (1.0 - kick);
    float ring = exp(-pow((r - front) / 0.09, 2.0)) * kick * e;

    vec3 armCol = nebEmission(0.56 + key, 0.25);
    vec3 col = armCol * disk * (0.12 + 0.95 * arm * (0.6 + 0.6 * clump)) * (1.0 + 0.9 * ring);

    // Star-forming knots: dense clumps that sit in an arm.
    float knots = smoothstep(0.6, 0.82, clump) * arm * disk;
    col += nebEmission(0.92 + key, 0.2) * knots * (0.7 + 0.6 * mid + 1.2 * ring);

    // Resolved disk stars, thicker in the arms.
    col += nebStars(g, 16.0, 1, vec2(0.0), 0.7, (0.15 + 0.5 * arm) * disk * 1.6, hats, t) * 0.8;

    // The bulge: old, warm, overexposed at its heart.
    col += nebEmission(0.08 + key, 0.85) * (bulge * 1.25 + exp(-r * 6.0) * 0.25) * (0.85 + 0.2 * swell);

    // The dust lane trails each arm's inner edge.
    col *= exp(-lane * disk * 2.4 * (0.6 + 0.6 * clump) * nebAbsorption(0.3 + key));

    // Field stars, in front of nothing.
    col += nebStars(uv, 6.0, 2, vec2(0.0), 1.0, 0.18, hats, t) * (1.0 - min(disk * 1.5, 0.9));

    fragColor = vec4(grade(col), 1.0);
}
