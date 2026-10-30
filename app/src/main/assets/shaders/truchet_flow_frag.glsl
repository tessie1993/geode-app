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

// Truchet Flow: traffic on the paths. The quarter-arc tiling is laid down as
// faint guide rails, and rounded dashes stream along every path. The dashes
// ride TruchetArc.s, the position along the arc in the path's own direction;
// that direction is consistent from tile to tile (lib_truchet: it follows the
// cell's checkerboard parity), and every arc holds a whole number of dashes,
// so the stream runs unbroken through the whole maze - each closed loop of the
// tiling becomes a little circuit of moving lights. Each dash is shaded from
// a bright head to a dim tail.
//
// ---- audio ------------------------------------------------------------------
//
// The TEMPO sets the speed: the dashes advance exactly one dash spacing per
// beat, on the analyser's phase-locked beat phase (uBeatPhase), plus a slow
// drift so they never stop. The mids (uMidSmooth) set the density - longer
// dashes that close into a near-solid stream on a busy middle, short dots on
// a sparse one. Onsets swing a scattered quarter of the tiles, so the traffic
// is re-routed on the hit. The bass breathes the scale. Brightness follows
// uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the drift and the hue. Complexity: uMorph puts more
// dashes on each arc. Chaos: uWarp/uTurbulence bend the plane before tiling;
// uTwist in view(). Energy: uIntensity scales the mid density and the bass
// breathing. Colour: dashes, heads and rails are pal(). Space: view().

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float mids = clamp(uMidSmooth * e, 0.0, 1.0);
    float morph = truchetMorph();

    float scale = 2.6 * truchetBreath(uBassSmooth);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    TruchetArc a = truchetSquare(p, floor(uHitCount), uHits.w, 4.0);

    // Dashes per quarter arc: a whole number, so the stream joins up.
    float per = 3.0 + floor(morph * 2.0 + 0.5);
    float arcLen = TRUCHET_PI * 0.25;
    float seg = arcLen / per;
    float phase = a.s * per - fract(uBeatPhase) - t * 0.12;
    float k = fract(phase);
    float duty = 0.22 + 0.55 * mids;
    // Rounded dash: a capsule along the arc, measured in cell units.
    float halfLen = 0.5 * duty * seg;
    float along = (k - 0.5 * duty) * seg;
    // Wrap so the capsule's far half reaches back across the fract seam.
    along = along - seg * floor(along / seg + 0.5);
    float hw = 0.055;
    float dd = length(vec2(max(abs(along) - max(halfLen - hw, 0.0), 0.0), a.d));
    float dash = truchetLine(dd, hw, px);
    // Head bright, tail dim. The stream moves toward rising s, so the front
    // of a dash is its far end along the arc (wrapped, so a tail's round cap
    // that lies across the fract seam still reads as tail).
    float headness = clamp(along / max(2.0 * halfLen, 1e-4) + 0.5, 0.0, 1.0);

    float hue = 0.05 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = 0.15 * truchetField(p, t);
    vec3 tail = pal(hue + 0.55 + field);
    vec3 head = mix(pal(hue + 0.15 + field), vec3(1.0), 0.35);

    float tone = truchetTone(a, px);
    vec3 col = mix(pal(hue + 0.7) * 0.025, pal(hue + 0.75) * 0.05, tone);
    col += pal(hue + 0.6) * 0.12 * truchetLine(a.d, 0.012, px);
    col += pal(hue + 0.55 + field) * exp(-abs(a.d) * 18.0) * 0.08;
    col = mix(col, mix(tail * 0.6, head, headness), dash);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
