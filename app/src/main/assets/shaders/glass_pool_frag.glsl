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
//#include lib_glass

// Glass Pool: looking straight down into a swimming pool in low sun. The floor
// is a mosaic of small palette tiles with grout lines and a dark lane stripe,
// seen through a moving surface that bends it (the refraction offset is the
// surface slope times the depth, read once per wavelength, so tile edges
// fringe where the water is steep). Across the floor runs the caustic net in
// three wavelengths (glassCausticRGB): one wave sum, three determinants, so
// its lines split into thin rainbows where they fold. On top, the surface
// reflects the dusk sky - faint face-on, as water does - and the sun glitters
// on the wave crests.
//
// ---- audio ------------------------------------------------------------------
//
//   kick         drops a ring ripple at a spot that hops per onset; it spreads and
//                fades, bending the tiles and pulling the caustics into a ring;
//   snare        a gust: the chop rises and the net tightens;
//   bass         the swell's height;
//   centroid     caustic sharpness: bright mixes cut thin, bright lines;
//   flatness     turbidity: noise clouds the water, softening the net;
//   pan          the sun's bearing: the net leans and the glitter walks;
//   hat          the glitter sparkles.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the waves. Complexity makes the tiles smaller. Chaos stirs a
// cross-current into the surface. Energy scales every reaction. The rig's look
// tilts the view a little (parallax between surface and floor).
//
// ---- cost -------------------------------------------------------------------
//
// Five-wave sum twice (surface, floor), three tile reads, one sky lookup. No
// loop over objects; cheap enough for Saver.

#define PL_DEPTH 0.55

GlassLook gLk;

/** The pool floor at `f`: mosaic tiles carrying the FRACTAL world as their glaze, with grout. */
vec3 tiles(vec2 f, float hue, float size) {
    vec2 g = f / size;
    vec2 id = floor(g);
    vec2 e = abs(fract(g) - 0.5);
    float grout = smoothstep(0.455, 0.48, max(e.x, e.y));
    float h = glassHash(id);
    vec3 c = glassFractal2(f * 0.35, gLk, 0.0) * 2.0 + pal(hue + 0.5) * 0.05 * (0.5 + h);
    return mix(c * 0.6, c * 0.3 + 0.05, grout);
}

/** The ring ripple dropped by the last kick: height in .x, slope in .yz; its Hessian added to `hess`. */
vec3 ripple(vec2 p, vec2 at, float age, float amp, inout vec3 hess) {
    vec2 d = p - at;
    float r = length(d) + 1e-4;
    float front = age * 2.2;
    float env = exp(-(r - front) * (r - front) * 6.0) * exp(-age * 1.4) * amp;
    float k = 9.0;
    float ph = k * (r - front);
    float h = env * sin(ph) * 0.05;
    vec2 n = d / r;
    vec2 g = n * env * cos(ph) * 0.05 * k;
    float h2 = -env * sin(ph) * 0.05 * k * k;
    hess += h2 * vec3(n.x * n.x, n.y * n.y, n.x * n.y);
    return vec3(h, g);
}

void main() {
    vec2 uv = view();
    float t = glassTime() * 0.8;
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_DUSK, hue, ear);
    gLk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    vec2 p = uv * 2.2 + uCamLook.xy * 0.3;
    float chop = 0.55 + 0.35 * clamp(ear.bass, 0.0, 1.2) + 0.5 * ear.snare;
    float sharp = mix(0.05, 0.012, ear.sharp) * (1.0 + 2.0 * ear.frost);
    vec2 sunLean = vec2(0.25 * ear.pan, 0.1);
    vec2 cross = glassChaos() * 0.4 * vec2(sin(p.y * 1.3 + t), cos(p.x * 1.1 - t));

    vec3 hess;
    vec3 w = glassWaves(p + cross, t, chop, hess);
    // The kick's ring: dropped at a spot that hops with each onset, ageing as the kick envelope falls.
    vec2 at = (vec2(glassHash1(floor(SND_ONSET_COUNT)), glassHash1(floor(SND_ONSET_COUNT) + 9.0)) - 0.5) * vec2(3.0, 1.8);
    float age = (1.0 - clamp(SND_KICK_ENV, 0.0, 1.0)) * 1.6;
    w += ripple(p, at, age, clamp(glassEnergy(), 0.0, 1.5), hess);
    vec2 slope = w.yz;

    // The floor, refracted: the offset grows with depth and slope, per wavelength.
    float size = mix(0.22, 0.12, glassComplexity());
    vec2 off = slope * PL_DEPTH * 0.75;
    vec2 fp = p + off;
    vec3 floorCol = vec3(
        tiles(fp + off * 0.06, hue, size).r,
        tiles(fp, hue, size).g,
        tiles(fp - off * 0.06, hue, size).b);
    // The caustic net on the floor, from the waves above the floor point.
    vec3 fh;
    glassWaves(fp + sunLean + cross, t, chop, fh);
    ripple(fp + sunLean, at, age, clamp(glassEnergy(), 0.0, 1.5), fh);
    vec3 caus = glassCausticRGB(fh, 2.6, 0.06 + 0.06 * ear.sharp, sharp);
    vec3 sun = mix(vec3(1.0, 0.92, 0.75), pal(hue + 0.1), 0.2);
    floorCol *= 0.6 + 2.2 * caus * sun;
    // Water: absorption toward the palette's deep colour, clouded by noise.
    vec3 water = mix(vec3(0.05, 0.35, 0.45), pal(hue + 0.5), 0.35);
    floorCol = mix(floorCol, water * 0.4, 0.18 + 0.3 * ear.frost);

    // The surface: dusk sky reflected (a few percent face on), sun glitter on the crests.
    vec3 n = normalize(vec3(-slope.x, 1.0, -slope.y));
    vec3 rd = normalize(vec3(uv.x * 0.15, -1.0, uv.y * 0.15));
    float fr = glassFresnel(max(dot(n, -rd), 0.0), 1.33);
    vec3 r = reflect(rd, n);
    vec3 sky = glassEnv(r, lk);
    vec3 key = glassKeyDir(lk);
    float glit = pow(max(dot(r, normalize(key + vec3(0.0, 2.5, 0.0))), 0.0), 900.0);
    glit *= (0.3 + 2.5 * ear.hat) * step(0.8, glassHash(floor(p * 60.0) + floor(t * 8.0)));
    vec3 col = floorCol * (1.0 - fr) + sky * fr * 2.0 + sun * glit * 3.0;

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
