#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_chrys

// Chrysanthemum Glass: a rose window. The flower is cut into panes of
// coloured glass held in black lead: six rings of petals (6, 6, 12, 12, 24,
// 24, the counts a glazier would pick) over a field of polar lozenges, a
// round boss in the middle and a stone border pierced with small round foils.
// Each pane takes its own hue from a hash of (ring, petal), streaked like
// hand-blown glass, and the light through it is strongest in the middle, as
// if the sun stood right behind the window. Outside the window the dark
// stone wall catches a coloured spill and faint shafts.
//
// The lead is tracked the way a glazier would see it: a pane laid on top
// hides the lead of the panes beneath it, so the lines are the edges of the
// panes you can actually see, not every petal outline at once.
//
// ---- audio ------------------------------------------------------------------
//
// Pitch moves the colour. uKeyHue (the detected key, eased round the hue
// circle over 3 s) WALKS every pane's hue together as the harmony moves, gated
// by uKeyStrength so a noisy passage leaves the window alone. On top of that,
// each pitch change (uEvents.w) sends a hue ripple out from the boss to the
// border across the rings as its envelope decays, so a melody leaves a trail
// of rings turning colour. Spectral presence (uBandsHi.y) shifts the angle of
// the light shafts outside. Brightness follows uSwell only: hits never light
// the glass.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow turn and the shaft drift. Complexity: uMorph
// adds up to two more petal rings, packed between the others. Chaos:
// uTurbulence + uWarp warp the lead into hand-cut, uneven panes; uTwist
// curls the petal rings into a pinwheel. Energy: uIntensity scales the
// pitch ripple. Colour: pal() for every pane, the spill and the shafts (the
// lead and stone are fixed). Space: uZoom via view(). Symmetry: uSymmetry
// (default 6) is the glazier's base count, so the rings run n, n, 2n, 2n, 4n...

#define RW_WINDOW 0.9
#define RW_LEAD 0.0065

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float t = uTime * uSpeed;
    float pitchEnv = clamp(uEvents.w, 0.0, 1.0);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float rings = 6.0 + floor(2.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float baseCount = clamp(floor(uSymmetry + 0.5), 3.0, 12.0);
    float aa = 2.5 / uResolution.y;
    vec2 p = chrysTurn(t * 0.004) * uv;
    float r = length(p);
    float ang = atan(p.y, p.x);
    float walk = uKeyHue * clamp(uKeyStrength, 0.0, 1.0);
    float baseHue = 0.6 + walk;
    float light = 0.8 + 0.2 * swell;

    vec3 col;
    if (r > RW_WINDOW) {
        // Stone wall: coursed blocks, a coloured spill and soft shafts.
        vec2 bp = uv * vec2(4.0, 8.0);
        bp.x += 0.5 * mod(floor(bp.y), 2.0);
        vec2 bf = abs(fract(bp) - 0.5);
        float joint = smoothstep(0.47, 0.5, max(bf.x * 1.0, bf.y));
        float stone = 0.03 + 0.012 * chrysHash(floor(bp));
        col = vec3(stone * 1.1, stone, stone * 0.9) * (1.0 - 0.6 * joint);
        float fall = exp(-(r - RW_WINDOW) * 2.4);
        col += pal(baseHue + 0.15) * 0.07 * fall * light;
        float shaftAng = ang + 0.4 * clamp(uBandsHi.y, 0.0, 1.0);
        float shafts = pow(motionNoise(vec2(shaftAng * 7.0, t * 0.05)), 3.0);
        col += pal(baseHue + 0.3) * 0.08 * shafts * fall * light;
    } else {
        // The default pane: polar lozenges behind the petals.
        float sector = CHRYS_TAU / 48.0;
        float band = 0.09;
        float la = (ang / sector - floor(ang / sector) - 0.5) * sector * r;
        float lr = (fract(r / band) - 0.5) * band;
        vec2 pane = vec2(0.0, floor(ang / sector) + 48.0 * floor(r / band));
        float paneT = r / RW_WINDOW;
        float lead = min(abs(abs(la) - 0.5 * sector * r), abs(abs(lr) - 0.5 * band));

        vec2 pc = p + chaos * 0.025 * (vec2(chrysNoise(p * 6.0), chrysNoise(p * 6.0 + 9.0)) - 0.5);
        for (int i = 7; i >= 0; i--) {
            float k = float(i);
            if (k >= rings) continue;
            float f = k * 5.0 / (rings - 1.0);
            float count = baseCount * pow(2.0, floor(f * 0.5));
            float tipR = 0.2 + 0.124 * f;
            float len = 0.16 + 0.05 * f;
            vec2 q = chrysTurn(chrysRingOffset(k, count)) * pc;
            vec4 pet = chrysPetal(q, count, tipR - len, len, 0.62, uTwist, 0.45, 0.7);
            if (pet.x < 0.0) {
                pane = vec2(k + 1.0, mod(pet.w, count));
                paneT = k / (rings - 1.0);
                lead = abs(pet.x);
            } else {
                lead = min(lead, pet.x);
            }
        }
        // The boss.
        if (r < 0.09) {
            pane = vec2(9.0, floor(mod(ang / (CHRYS_TAU / 6.0) + 6.0, 6.0)));
            paneT = 0.0;
            lead = min(abs(r - 0.09), r < 0.035 ? 0.035 - r : 1.0);
        } else {
            lead = min(lead, abs(r - 0.09));
        }

        // Hue: the pane's own hash, the key walk, and the pitch ripple
        // travelling outward as the envelope decays.
        float front = 1.0 - pitchEnv;
        float ripple = exp(-pow((paneT - front) * 4.0, 2.0)) * sqrt(chrysBump(pitchEnv));
        float h = baseHue + 0.38 * chrysHash(pane * vec2(1.7, 3.1)) + 0.22 * ripple * uIntensity;
        vec3 glass = pal(h);
        glass = glass * glass * 1.25 + 0.04;
        float seed = chrysHash(pane + 11.0);
        vec2 sp = chrysTurn(seed * CHRYS_TAU) * p;
        float streak = 0.82 + 0.18 * motionNoise(sp * vec2(46.0, 5.0));
        float bubbles = step(0.985, chrysHash(floor(sp * 140.0)));
        float sun = 0.5 + 0.5 * exp(-r * 1.8);
        col = glass * streak * sun * light + vec3(0.06) * bubbles * sun;

        // Lead came: dark, with a dull highlight down its middle.
        float lw = RW_LEAD;
        float lm = smoothstep(lw + aa, lw - aa, lead);
        vec3 leadCol = vec3(0.02, 0.02, 0.025) + vec3(0.05) * smoothstep(lw, 0.0, lead) * 0.6;
        col = mix(col, leadCol, lm);

        // Stone border with round foils.
        if (r > 0.84) {
            float fs = CHRYS_TAU / 30.0;
            float fa = (fract(ang / fs) - 0.5) * fs;
            vec2 fq = vec2(r - 0.87, fa * 0.87);
            float foil = length(fq) - 0.018;
            float fid = floor(ang / fs);
            vec3 fglass = pal(baseHue + 0.5 + 0.2 * chrysHash1(fid));
            fglass = fglass * fglass * 1.2 * light * 0.8;
            float stoneShade = 0.05 + 0.03 * smoothstep(0.84, 0.87, r) * smoothstep(0.9, 0.87, r);
            vec3 stoneCol = vec3(stoneShade * 1.1, stoneShade, stoneShade * 0.85);
            vec3 border = mix(stoneCol, fglass, smoothstep(aa, -aa, foil));
            border = mix(border, leadCol, smoothstep(lw + aa, lw - aa, abs(foil)));
            col = mix(col, border, smoothstep(0.84 - aa, 0.84 + aa, r));
        }
        col *= 1.0 - 0.7 * smoothstep(RW_WINDOW - 0.01, RW_WINDOW, r);
    }

    fragColor = vec4(grade(col), 1.0);
}
