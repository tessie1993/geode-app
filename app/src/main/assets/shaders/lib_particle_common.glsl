// Shared particle geometry and colour helpers. NOT a standalone shader: it
// carries no `#version` and is pulled in with `//#include lib_particle_common`
// by the shaders that need it. BOTH stages of BOTH particle families take this
// chunk - the CPU styles in ParticleSceneBase and the GPU lifecycle layer the
// fluid styles run - so there is one set of shapes and one set of constants in
// the app rather than two look-alike copies drifting apart.
//
// It carries its own `precision highp float;` because a library's function
// bodies are compiled where they land, which is above the includer's own
// precision statement.
//
// Everything here is stage-neutral: no derivatives, no gl_FragCoord. The
// shading itself needs fwidth() and so lives in lib_particle_shade.glsl, which
// is included by fragment stages only.
precision highp float;

// Shape radius in quad space (the quad edge is 1.0); the rest of the quad is
// the margin the aura falls off across. SHAPE_R * GLOW_EXTENT is ~1, which is
// what keeps the body on the sprite radius the size attribute asks for - move
// one without the other and every shape grows or shrinks.
const float PT_SHAPE_R = 0.30;
const float PT_GLOW_EXTENT = 2.6;
// Sub-pixel sprites alias and strobe. The starfield fix is to hold the radius
// at roughly a pixel and dim by the area given up, so shrinking particles fade
// out smoothly instead of flickering.
const float PT_MIN_RADIUS_PX = 0.85;

vec3 ptHsv2rgb(vec3 c) {
    vec4 k = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + k.xyz) * 6.0 - k.www);
    return c.z * mix(k.xxx, clamp(p - k.xxx, 0.0, 1.0), c.y);
}

// --- 2D SDF primitives (Inigo Quilez, iquilezles.org/articles/distfunctions2d)
float ptSdCircle(vec2 p, float r) {
    return length(p) - r;
}

float ptSdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float ptSdHexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

float ptSdStar(vec2 p, float r, float n, float m) {
    float an = 3.141593 / n;
    float en = 3.141593 / m;
    vec2 acs = vec2(cos(an), sin(an));
    vec2 ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

float ptSdCross(vec2 p, vec2 b) {
    p = abs(p);
    p = (p.y > p.x) ? p.yx : p.xy;
    vec2 q = p - b;
    float k = max(q.y, q.x);
    vec2 w = (k > 0.0) ? q : vec2(b.y - p.x, -k);
    return sign(k) * length(max(w, 0.0));
}

/** Signed distance to the selected shape's surface (index = SceneParams.particleShape). */
float ptShapeField(vec2 p, float shape) {
    if (shape < 0.5) {
        return ptSdCircle(p, PT_SHAPE_R);
    } else if (shape < 1.5) {
        // Ring: an annulus around the circle instead of an exp() band.
        return abs(ptSdCircle(p, PT_SHAPE_R * 0.84)) - PT_SHAPE_R * 0.17;
    } else if (shape < 2.5) {
        return ptSdStar(p, PT_SHAPE_R * 1.06, 5.0, 2.6);
    } else if (shape < 3.5) {
        return ptSdRoundedBox(p, vec2(PT_SHAPE_R * 0.80), PT_SHAPE_R * 0.26);
    } else if (shape < 4.5) {
        // Spark: a rounded cross with a bead at the crossing.
        float arms = ptSdCross(p, vec2(PT_SHAPE_R * 1.15, PT_SHAPE_R * 0.11)) - PT_SHAPE_R * 0.05;
        return min(arms, ptSdCircle(p, PT_SHAPE_R * 0.20));
    } else if (shape < 5.5) {
        return ptSdHexagon(p, PT_SHAPE_R * 0.90);
    } else {
        // Bubble: a thin shell (the fill and highlight are added separately).
        return abs(ptSdCircle(p, PT_SHAPE_R * 0.92)) - PT_SHAPE_R * 0.07;
    }
}

/** Four-armed diffraction streak, the lens-flare cue that reads as "bright". */
float ptSpikes(vec2 p) {
    vec2 a = abs(p);
    return exp(-a.y * 46.0 - a.x * 3.4) + exp(-a.x * 46.0 - a.y * 3.4);
}

/** ACES filmic curve, Krzysztof Narkowicz's fit of the RRT+ODT. */
vec3 ptAces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/** Dave Hoskins' sine-free hash (shadertoy.com/view/4djSRW) - the sin()-based
 *  one banded visibly on mobile GPUs at reduced precision. */
float ptHash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/**
 * Sub-pixel handling for a sprite of the given radius: returns the radius to
 * actually draw at (x) and the brightness left after giving up area (y).
 */
vec2 ptRadiusFade(float radiusPx) {
    float held = max(radiusPx, PT_MIN_RADIUS_PX);
    float fade = radiusPx > 0.0 ? min(1.0, (radiusPx * radiusPx) / (held * held)) : 0.0;
    return vec2(held, fade);
}

/**
 * Billboard corner offset in px for a quad of half-extent [quadPx], oriented
 * along the screen-space velocity [velPx] and stretched by its speed - the
 * stretched-billboard motion trick (NVIDIA, Stupid OpenGL Shader Tricks, GDC
 * 2003). Under ~1 px/s there is no meaningful direction, so the quad stays
 * axis-aligned and the sprite reads round. [stretchSeconds] is how much travel
 * folds into the streak and [maxStretch] the ceiling - a style whose whole
 * subject is falling (rain) wants a far longer streak than one whose particles
 * merely happen to move. [stretch] returns the factor for energy conservation.
 */
vec2 ptBillboard(vec2 corner, vec2 velPx, float quadPx, float stretchSeconds, float maxStretch, out float stretch) {
    float speedPx = length(velPx);
    stretch = 1.0 + clamp(speedPx * stretchSeconds, 0.0, max(maxStretch - 1.0, 0.0));
    vec2 dir = speedPx > 1.0 ? velPx / speedPx : vec2(1.0, 0.0);
    return dir * (corner.x * quadPx * stretch) + vec2(-dir.y, dir.x) * (corner.y * quadPx);
}

