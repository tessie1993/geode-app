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
//#include lib_lucid

// Lucid Petal: a flower that never stops opening. The plane is measured in
// petal-shaped rings: the radius is divided by a rose curve, so every
// contour is a five-petalled outline, and those contours are banded into
// layers that bloom outward from the centre along the flow. Each layer gets
// its own turn of a value nest (three levels), so the petals are veined and
// mottled differently ring by ring; layers alternate their channel order.
//
// Coloured as fire opal: a warm palette body, shaded darker toward each
// petal's edge, with play-of-colour flecks - small cells of saturated colour
// that light up inside the body, fixed to the petal, as in a real opal.
//
// ---- audio ------------------------------------------------------------------
//
// SUB-BASS (sndSpectrum(0.10), the lowest band region) swells the petals: it deepens
// the rose curve, so on a sub drop the outline goes from a near-circle to
// deep, separate petals and the rings flare. It moves geometry only. Busy
// mids (sndShaped(SND_MIDDLE)) brighten the opal flecks' colour, not the frame.
// Brightness follows SND_LEVEL gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the blooming. Complexity: uMorph adds
// petals (5 -> 8, whole petals only, stepped at thirds) and, past 0.5, a nest
// level and an octave. Chaos: uTurbulence/uWarp raise warp strength and
// jitter the veins; uTwist twists the flower. Kaleido: when uKaleido is on,
// uSymmetry sets the petal count. Color: body and flecks are pal(). Energy:
// uIntensity scales the sub swell and the fleck response. Space: uZoom/
// uRotation through lucidView.

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float sub = clamp(sndSpectrum(0.10), 0.0, 1.0) * uIntensity;
    float mid = clamp(sndShaped(SND_MIDDLE), 0.0, 1.5) * uIntensity;
    float swell = clamp(SND_LEVEL, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    float petals = (uKaleido > 0.5 && uSymmetry >= 2.0) ? floor(uSymmetry + 0.5) : 5.0 + floor(clamp(uMorph, 0.0, 1.0) * 3.0);
    float r = length(uv);
    float a = atan(uv.y, uv.x) + t * 0.03;
    float shape = pow(abs(cos(petals * a * 0.5)), 0.6);
    float amp = 0.2 + 0.5 * clamp(sub, 0.0, 1.5);
    float u = r / (0.5 + amp * shape);

    float L = u * 2.2 - flow * 0.35 - t * 0.05;
    float idx = floor(L);
    float ring = fract(L);

    vec2 q = lucidRot(idx * 0.77) * uv * 2.2 + vec2(idx * 3.1, idx * 1.7);
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.2 * lucidChaos(), t * 0.18, LUCID_VALUE, oct);

    // Opal body: warm, shaded darker at the petal's leading edge.
    float body = 0.5 + 0.5 * n.z;
    vec3 col = pal(0.02 + 0.14 * body + 0.06 * idx) * (0.3 + 0.75 * body);
    float edge = smoothstep(0.0, 0.18, ring) * smoothstep(1.0, 0.72, ring);
    col *= 0.25 + 0.75 * edge;

    // Play of colour: small saturated cells fixed to the petal.
    vec3 c = lucidCells(q * 3.0 + n.xy, 0.9);
    float fleck = smoothstep(0.35, 0.05, c.x) * step(0.55, c.z);
    col += pal(c.z * 2.0 + n.w) * fleck * (0.3 + 0.35 * clamp(mid, 0.0, 1.0)) * edge;

    col = lucidChannelTurn(col, mod(idx, 2.0) * 0.5);
    col = lucidCubic(col, 0.6);
    // A dark eye at the heart of the flower.
    col *= smoothstep(0.0, 0.15, r);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
