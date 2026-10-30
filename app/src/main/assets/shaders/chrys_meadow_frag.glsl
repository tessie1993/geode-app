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
//#include lib_chrys

// Chrysanthemum Meadow: a night field of small chrysanthemums on a ground
// plane receding to the horizon, the viewer gliding forward over it. Every
// grid cell grows one flower facing the sky, with its own petal count,
// colour and jitter from the cell hash; the plane's own perspective
// foreshortens them, so the near ones are wide rosettes and the far ones thin
// ellipses. Far rows fade their petals into an averaged colour before they
// can shimmer, then into a mist at the horizon.
//
// ---- audio ------------------------------------------------------------------
//
// The meadow IS a spectrum analyser. Each flower reads the spectrum
// (aband) at the frequency of where its centre sits across the screen - bass
// at the left edge, air at the right - and opens that far, so a kick blooms
// a drift of flowers down the left side of the field and a hi-hat flickers
// the right. Every row shows the whole spectrum, near rows large, far rows
// small. Forward travel rides uFlowPhase (loudness sets the speed, never the
// direction). The flowers' centres take uKeyHue when the key is confident.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the sideways drift, the flowers' slow turning and the
// star twinkle (travel stays on uFlowPhase). Complexity: uMorph plants the
// field denser (smaller cells, up to twice as many flowers per row). Chaos:
// uTurbulence + uWarp vary petal lengths and fray the edges; uTwist curls
// every flower. Energy: uIntensity scales how far the spectrum opens them.
// Colour: pal() throughout. Space: uZoom via view().

#define MD_HORIZON 0.28
#define MD_EYE 0.55

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float aspect = uResolution.x / uResolution.y;
    float t = uTime * uSpeed;
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float density = 1.0 + clamp(uMorph, 0.0, 1.0);
    float hue = 0.75 + 0.02 * sin(t * 0.02);
    float keyHue = mix(hue + 0.12, uKeyHue, clamp(uKeyStrength, 0.0, 1.0) * 0.8);

    float sy = uv.y - MD_HORIZON;
    vec3 sky = mix(pal(hue + 0.05) * 0.12, pal(hue + 0.6) * 0.02, smoothstep(0.0, 0.9, sy));
    vec3 col;
    if (sy > -0.004) {
        // Sky: a few fixed stars on a coarse grid.
        vec2 sg = uv * 34.0;
        vec2 sc = floor(sg);
        float sh = chrysHash(sc);
        float star = step(0.93, sh) * smoothstep(0.12, 0.0, length(fract(sg) - 0.5 - 0.3 * (vec2(sh, fract(sh * 7.0)) - 0.5)));
        col = sky + vec3(0.8, 0.85, 1.0) * star * 0.35 * (0.6 + 0.4 * sin(t * (1.0 + 3.0 * sh) + sh * 30.0));
    } else {
        float z = MD_EYE / -sy;
        vec2 w = vec2(uv.x * z + 1.7 * sin(t * 0.031), z + uFlowPhase * 0.55) * density;
        vec2 cell = floor(w);
        float h1 = chrysHash(cell);
        float h2 = chrysHash(cell + 19.7);
        vec2 jitter = (vec2(h1, h2) - 0.5) * 0.18;
        vec2 local = w - cell - 0.5 - jitter;
        // World size of one pixel here: detail fades out before it aliases.
        float foot = density * z * (1.0 + z / MD_EYE) * 2.0 / uResolution.y;
        float detail = smoothstep(0.12, 0.035, foot);

        // Which frequency this flower hears: its centre's place across the screen.
        float cx = (cell.x + 0.5 + jitter.x) / density - 1.7 * sin(t * 0.031);
        float freq = clamp(0.5 + 0.5 * (cx / z) / aspect, 0.0, 1.0);
        float open = 0.22 + 0.78 * clamp(aband(freq) * uIntensity, 0.0, 1.15);

        float count = 8.0 + floor(h1 * 9.0);
        float len = 0.3 * open;
        vec2 lp = chrysTurn(h2 * CHRYS_TAU + t * 0.05 * (h1 - 0.5)) * local;
        float pid = floor((atan(lp.y, lp.x) - uTwist * 3.0 * (length(lp) - 0.06)) / (CHRYS_TAU / count) + 0.5);
        float lenJ = len * (1.0 + 0.2 * chaos * chrysWobble(mod(pid, count) + h1 * 50.0, t));
        vec4 pet = chrysPetal(lp, count, 0.06, lenJ, 0.5, uTwist * 3.0, 0.35, 0.45);
        pet.x += chrysFray(lp * 0.5, chaos);
        vec4 pet2 = chrysPetal(chrysTurn(CHRYS_PI / count) * lp, count, 0.04, len * 0.7, 0.55, uTwist * 3.0, 0.6, 0.3);
        float aa = max(foot * 1.5, 1e-3);
        vec3 petCol = pal(hue + 0.35 * h1) * mix(0.55, 1.0, pet.y) * chrysPetalShade(pet, 0.35);
        vec3 petCol2 = pal(hue + 0.35 * h1 + 0.06) * 0.85 * chrysPetalShade(pet2, 0.6);
        float rl = length(local);
        vec3 ground = pal(hue + 0.36) * 0.035 * (0.7 + 0.6 * h2);
        vec3 c = ground;
        c *= 1.0 - 0.5 * smoothstep(0.05, -0.02, pet.x);
        c = mix(c, petCol, smoothstep(aa, -aa, pet.x));
        c = mix(c, petCol2, smoothstep(aa, -aa, pet2.x));
        c = mix(c, pal(keyHue) * 0.9, smoothstep(0.05 + aa, 0.05 - aa, rl));
        // Far away: the average a flower of this openness would show.
        vec3 avg = mix(ground, pal(hue + 0.35 * h1) * 0.6, 0.25 + 0.4 * open);
        c = mix(avg, c, detail);
        // Mist toward the horizon.
        col = mix(sky, c, exp(-z * 0.07));
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
