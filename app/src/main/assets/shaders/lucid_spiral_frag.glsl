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

// Lucid Spiral: an endless dive into a spiral staircase of ridged folds.
// The plane is taken to log-polar coordinates, where scaling becomes sliding,
// then the angle is sheared by the log radius - the Droste twist - and
// mirrored into six arms so the spiral closes on itself with no seam. A
// ridged nest three levels deep is laid on the sheared strip, so the arms are
// lined with sharp branching creases that pour toward the centre forever.
//
// Coloured as an acid duotone: two palette ends split by the ridge value,
// with alternate arms swapping their channel order, so neighbouring arms
// never share a colour.
//
// ---- audio ------------------------------------------------------------------
//
// The kick (uHits.x) pulses ZOOM DEPTH: each kick pushes the dive a short way
// further down the spiral (log-radius offset), on top of the steady descent
// carried by uFlowPhase. It is a geometric push, never a brightness change.
// uTrebleSmooth tightens the ridges' crease width. Brightness follows uSwell
// gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the descent. Complexity: uMorph past 0.5
// adds a nest level and an octave. Chaos: uTurbulence/uWarp raise warp
// strength and jitter the strip; uTwist (in lucidView) winds the arms
// tighter. Kaleido:
// when uKaleido is on, uSymmetry (rounded to even) sets the arm count. Color:
// both duotone ends are pal(). Energy: uIntensity scales the kick push. Space:
// uZoom/uRotation through lucidView.

/** Even, so arm parity survives the angle's wrap. */
#define SP_ARMS 6.0

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float kick = clamp(uHits.x, 0.0, 1.0) * uIntensity;
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(4, 2);

    vec2 lp = lucidLogPolar(uv);
    float r = exp(lp.x);
    float dive = flow * 0.30 + t * 0.06 + 0.22 * kick;
    float u = lp.x - dive;
    // Shear the angle by the log radius, so every arm winds inward as a
    // logarithmic spiral, then mirror it into arms. The angle's 2*pi jump is a
    // whole number of arms, and the arm count is even so the parity used for
    // the channel swap survives it too: no seam anywhere.
    float arms = (uKaleido > 0.5 && uSymmetry >= 2.0) ? 2.0 * floor(uSymmetry * 0.5 + 0.5) : SP_ARMS;
    float seg = LUCID_TAU / arms;
    float a = lp.y + u * 0.9;
    float arm = floor(a / seg);
    float v = abs(mod(a, seg) - 0.5 * seg) / seg;

    vec2 q = vec2(u * 2.4, v * 2.8);
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.25 * lucidChaos(), t * 0.2, LUCID_RIDGED, oct);

    float ridge = 0.5 + 0.5 * n.z;
    float crease = smoothstep(0.55 - 0.1 * clamp(treb, 0.0, 1.0), 0.98, ridge);
    vec3 col = mix(pal(0.08) * 0.15, pal(0.62), smoothstep(0.2, 0.8, ridge));
    col = mix(col, pal(0.95) * 1.2, crease);
    col = lucidChannelTurn(col, mod(arm, 2.0) + 0.4 * n.w);
    col = lucidCubic(col, 0.85);

    // The centre is infinitely far down the staircase: fade it out.
    col *= smoothstep(0.0, 0.18, r);
    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
