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
//#include lib_truchet

// Truchet Tunnel: the quarter-arc tiling wrapped onto the wall of an endless
// tube. The screen is read in log-polar coordinates - angle across, log of
// the radius along - scaled so a cell is square in both, which is the
// conformal map: the tiles keep their shape and only shrink toward the far
// end at the centre. Sliding along log radius is moving down the tube.
//
// NO SEAM. atan() jumps by a full turn on the negative x axis. The angle is
// scaled to exactly N cells per turn with N a whole, EVEN number, so across
// the jump the cell coordinate moves by exactly N: the fract() inside each
// cell is unchanged, the cell id is wrapped mod N before it is hashed, and an
// even N keeps the checkerboard parity (which sets the two-colouring and the
// flow direction) unchanged too. Every other angular term below multiplies the
// angle by a whole number. The pixel footprint for anti-aliasing comes from
// fwidth of the SCREEN coordinate, scaled analytically, never from fwidth of
// the angle - that would smear a one-pixel line along the jump.
//
// ---- audio ------------------------------------------------------------------
//
// Flight rides uFlowPhase (loudness sets the rate, never the direction): the
// wall streams outward past the viewer. A DROP (uEvents.y) corkscrews the
// tube, winding a twist that grows with depth and easing back out over the
// drop's envelope (a bump, so it starts and ends at rest). Onsets swing a
// scattered third of the tiles. The bass breathes the tube's width. The
// two tones trade along the depth of the tube; brightness follows uSwell.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the flight and the hue. Complexity: uMorph past 0.5
// puts 16 cells round the tube instead of 12. Chaos: uWarp/uTurbulence ripple
// the wall with wrap-safe waves (whole multiples of the angle); uTwist in
// view(). Energy: uIntensity scales the corkscrew and the bass breathing.
// Colour: both tones and the lines are pal(). Space: view().

void main() {
    vec2 uv = view();
    float pxUv = length(fwidth(uv)) * 0.7071;
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);

    float cells = truchetMorph() >= 0.5 ? 16.0 : 12.0;
    float k = cells / TRUCHET_TAU;
    float r = max(length(uv), 1e-3);
    float ang = atan(uv.y, uv.x);
    float depth = log(r / truchetBreath(uBassSmooth));

    float drop = clamp(uEvents.y, 0.0, 1.0);
    float screw = 4.0 * drop * (1.0 - drop) * e;
    float travel = uFlowPhase * uSpeed * 0.6 + t * 0.12;

    vec2 q = vec2(ang * k, depth * k - travel);
    q.x += (0.35 + 1.4 * screw) * depth * k;
    float chaos = truchetChaos();
    if (chaos > 1e-4) {
        q.x += 0.35 * chaos * sin(q.y * 0.4 + t * 0.2);
        q.y += 0.35 * chaos * sin(ang * 3.0 + q.y * 0.25 - t * 0.17);
    }
    float px = k * pxUv / r;

    vec2 id = floor(q);
    id.x = mod(id.x, cells);
    TruchetArc a = truchetSquareAt(id, fract(q) - 0.5, floor(uHitCount), uHits.w, 3.0);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float along = 0.5 + 0.5 * sin(q.y * 0.35);
    vec3 toneA = pal(hue + 0.05 + 0.2 * along) * 0.8;
    vec3 toneB = pal(hue + 0.55 + 0.2 * along) * 0.22;
    float tone = truchetTone(a, px);
    vec3 col = mix(toneB, toneA, tone);
    col *= 0.75 + 0.25 * smoothstep(0.0, 0.25, abs(a.d));
    col = mix(col, pal(hue + 0.3) * 1.1, truchetLine(a.d, 0.035, px));
    // Light falls off toward the far end, which also hides the cells there
    // shrinking below a pixel.
    col *= smoothstep(0.0, 0.45, r) * (0.7 + 0.3 * smoothstep(0.2, 1.4, r));

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
