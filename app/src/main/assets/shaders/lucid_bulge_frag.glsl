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
//#include lib_lucid

// Lucid Bulge: a soap bubble swelling out of a flat film. The centre of the
// frame is mapped as a sphere seen from the front - the field is magnified at
// its heart and squeezed into the rim by asin(), exactly as a pattern painted
// on a ball is - while the plane around it carries on flat, so the bubble
// reads as a bulge rising out of the same skin. Inside, a simplex nest four
// levels deep slides around the sphere as it turns; outside, a cheaper
// two-level nest keeps the film alive.
//
// Coloured as thin-film interference, the colours on a soap bubble: the
// nest sets the film's thickness, and the sphere's own normal adds the
// angle-of-view shift, so the rim runs through more colours than the face.
// A soft specular window and a dark limb make it round.
//
// ---- audio ------------------------------------------------------------------
//
// PITCH walks the hue. The key detector's eased hue (uKeyHue; uKeyStrength
// blends between the walked colour and the resting one) and each pitch change (uEvents.w, through lucidBump so the
// nudge swells in rather than jumps) rotate the film's colour: the walk turns
// the interference phase and the channel order together, both periodic in a
// whole turn, so the eased key hue wrapping from 0.99 to 0.01 is seamless.
// uBreath gently inflates the bubble. Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock (sphere spin and film drift). Complexity:
// uMorph thickens the film (more interference orders) and, past 0.5, adds a
// nest level and an octave. Chaos: uTurbulence/uWarp raise warp strength and
// jitter the film; uTwist twists the plane. Color: the film is half pal(),
// half the physical interference ramp. Energy: uIntensity scales the pitch
// nudge and the breathing. Space: uZoom/uRotation through lucidView. The
// user's kaleidoscope folds the plane (through lucidView).

/** The film colour at a hue walk; periodic in `walk` with period 1 (film phase and channel order both wrap). */
vec3 walkFilm(float thick, float walk) {
    vec3 film = mix(lucidFilm(thick + walk), pal(thick * 0.5), 0.5);
    return lucidChannelTurn(film, walk * 3.0);
}

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    float chaos = lucidChaos();

    // The walk is never scaled (a scaled circular hue would jump where it
    // wraps); key confidence instead blends the walked film with the resting one.
    float walk = uKeyHue + 0.2 * lucidBump(uEvents.w) * uIntensity;
    float keyed = clamp(uKeyStrength, 0.0, 1.0);
    float R = 0.72 * (1.0 + (clamp(uBreath, 0.9, 1.1) - 1.0) * uIntensity);
    float r = length(uv);
    float orders = 1.0 + 0.8 * clamp(uMorph, 0.0, 1.0);

    vec3 col;
    if (r < R) {
        int depth = lucidDetail(4, 3);
        int oct = lucidDetail(3, 2);
        vec2 s = uv / R;
        float rs = length(s);
        // Sphere map: arc length from the pole, so the rim compresses.
        vec2 q = s * (asin(min(rs, 0.9999)) / max(rs, 1e-4)) * 2.2;
        q += vec2(t * 0.12, t * 0.03);
        q += lucidJitter(q, t);
        vec4 n = lucidNest(q, depth, 1.35 * chaos, t * 0.2, LUCID_SIMPLEX, oct);
        vec3 nrm = vec3(s, sqrt(max(0.0, 1.0 - rs * rs)));
        float thick = (0.9 + 0.5 * n.z + 0.7 * (1.0 - nrm.z)) * orders;
        vec3 film = mix(walkFilm(thick, walk), walkFilm(thick, 0.0), 1.0 - keyed);
        float limb = 0.35 + 0.65 * pow(nrm.z, 0.6);
        float spec = pow(max(dot(nrm, normalize(vec3(-0.45, 0.55, 0.7))), 0.0), 40.0);
        col = film * limb * (0.55 + 0.45 * smoothstep(-0.6, 0.8, n.z));
        col += vec3(spec) * 0.55 + pal(0.5) * smoothstep(0.85, 1.0, rs) * 0.25;
    } else {
        // The flat film: same colouring, two levels, dimmer.
        vec2 q = uv * 1.6 + vec2(-flow * 0.05, t * 0.02);
        q += lucidJitter(q, t);
        vec4 n = lucidNest(q, 2, 1.1 * chaos, t * 0.15, LUCID_SIMPLEX, 2);
        float thick = (0.6 + 0.45 * n.z) * orders;
        vec3 film = mix(walkFilm(thick, walk), walkFilm(thick, 0.0), 1.0 - keyed);
        col = film * 0.32 * smoothstep(-0.7, 0.9, n.z);
        // Contact shadow where the bubble meets the film.
        col *= smoothstep(0.0, 0.12, r - R);
    }
    col = lucidCubic(col, 0.45);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
