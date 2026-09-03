#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_touch

// Spiral Eye: a turbulent cloud wound into a logarithmic spiral, streaked
// outward as if the whole frame were being pulled through the middle, with a
// small iridescent mandala burning at the centre of it. The reference is a
// galaxy that is also an eye.
//
// This is the style that is most literally "fluid": the cloud is dye advected
// through the curl field in lib_scene_motion, sampled several times along the
// radial streak so the turbulence smears outward the way motion blur does,
// with the particle layer riding the same field on top.
//
// uv -> log-polar -> curl-advected dye, sampled along the streak -> core.
//
// Audio: the spiral's pitch and the cloud's density breathe on uBassSmooth and
// uSwell; treble lights the filaments. A spike does not brighten anything -
// it re-aims the streak (uMoveDir), re-seeds the cloud (uSpawnSeed) and moves
// the arm count to a new plateau (uFormPhase). Nothing steps in one frame.

#define SE_TAU 6.2831853
#define SE_STREAK 6

// Dye density at a point in log-polar space. Two advections through the curl
// field, so the cloud folds into itself rather than reading as one noise
// layer laid over another.
float dye(vec2 lp, float detail) {
    vec2 q = fluidWarp(lp, 1.5, 0.30);
    q = fluidWarp(q + vec2(0.0, uFlowPhase * 0.25), 3.1, 0.16);
    float d = motionFbm(q * detail);
    d += 0.45 * motionFbm(q * detail * 2.9 + 5.1);
    return d / 1.45;
}

void main() {
    vec2 uv = view();
    float r = max(length(uv), 1e-4);
    float a = atan(uv.y, uv.x);

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    // A spike steps the spiral between 2, 3 and 4 arms and holds it.
    float arms = 2.0 + floor(uFormPhase * 3.0);

    // ---- log-polar --------------------------------------------------------
    //
    // log(r) is what makes the spiral self-similar: a constant step in it is a
    // constant RATIO in radius, so the arms keep their shape all the way in.
    float lr = log(r);
    // The pitch of the spiral. Bass tightens the wind a little; the winding
    // itself advances on uFlowPhase, which only ever moves forward, so a loud
    // passage spins the galaxy faster and can never spin it backwards.
    float pitch = 1.35 + 0.35 * bass;
    float theta = a * arms + lr * pitch * arms + uFlowPhase * 1.6;
    vec2 lp = vec2(theta / SE_TAU, lr * 1.4);

    // ---- the streaked cloud -------------------------------------------------
    //
    // Sampled along the radial direction at shrinking radii: each tap is the
    // same dye field read a little further in, which is exactly what a radial
    // motion blur is, and it costs no buffer.
    float cloud = 0.0;
    float filament = 0.0;
    float wsum = 0.0;
    for (int i = 0; i < SE_STREAK; i++) {
        float t = float(i) / float(SE_STREAK - 1);
        // The streak leans along the current travel direction, so a spike tips
        // the whole galaxy instead of flashing it.
        float pull = t * (0.55 + 0.25 * swell);
        vec2 s = lp + vec2(uMoveDir.x * 0.06 * t, -pull);
        float w = 1.0 - 0.72 * t;
        float d = dye(s, 2.6 + 1.4 * t);
        cloud += d * w;
        // The thin bright edges: where the dye is steep, not where it is high.
        filament += w * smoothstep(0.52, 0.78, d) * (1.0 - smoothstep(0.78, 0.94, d));
        wsum += w;
    }
    cloud /= wsum;
    filament /= wsum;

    // The dark lanes that make it read as a galaxy rather than as fog: the
    // cloud is gated, so there is genuine black between the arms.
    float body = smoothstep(0.34, 0.78, cloud);
    // Density falls off with radius so the frame does not fill edge to edge.
    float falloff = exp(-r * (1.35 - 0.35 * swell));

    vec3 warm = pal(0.10);
    vec3 cool = pal(0.55);
    vec3 col = mix(cool, warm, smoothstep(0.35, 0.85, cloud));
    col *= body * falloff * (0.55 + 0.75 * swell);
    // Filaments take the treble; the term is slew-limited, so a cymbal
    // brightens the edges over a few frames instead of on one.
    col += mix(vec3(1.0), warm, 0.35) * filament * falloff * (0.30 + 0.9 * treb);

    // ---- the mandala core ---------------------------------------------------
    //
    // Small, sharp and iridescent against the soft cloud: concentric rings cut
    // by radial spokes, the ring phase running on the same monotonic clock.
    float cr = r * 9.0;
    float rings = 0.5 + 0.5 * sin(cr * 5.0 - uFlowPhase * 3.0);
    float spokes = 0.5 + 0.5 * cos(a * (8.0 + 4.0 * floor(uFormPhase * 3.0)) + uFlowPhase * 2.0);
    float coreMask = exp(-cr * cr * 0.30);
    // The hue walks with radius, which is what gives the little disc its
    // oil-on-water banding.
    vec3 iris = pal(fract(0.15 + cr * 0.10 + uFormPhase * 0.2));
    col += iris * coreMask * (0.35 + 0.65 * rings * spokes) * 1.5;
    // The white-hot middle, and a halo that swells on a spike.
    col += vec3(1.0, 0.97, 0.9) * exp(-r * r * 900.0) * 2.2;
    col += warm * exp(-r * 14.0) * (0.35 + 0.45 * uSpike);

    // ---- the particle layer -------------------------------------------------
    //
    // Sparks torn off the arms, riding the same curl field the dye does.
    col += mix(vec3(1.0), warm, 0.4) * fluidMotes(uv * 1.2, 6.0, 0.14) * 0.30 * falloff;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.06;
    }
    fragColor = vec4(grade(col), 1.0);
}
