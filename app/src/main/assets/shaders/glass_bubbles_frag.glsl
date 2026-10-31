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

// Glass Bubbles: soap bubbles rising slowly through the studio, swaying as they
// go, each swelling in at the bottom of the frame and shrinking away at the
// top. A bubble is almost nothing - a film a few hundred nanometres thick - so
// it barely bends the light; what it shows is interference. Each bubble is an
// analytic sphere hit at both walls: the near wall reflects the room with
// glassFilm's colour bands over it, the far wall (seen from inside) adds a
// fainter second set, and the light passes through nearly untouched. The film
// is thicker toward the bottom where the water drains and swirls in slow bands
// over the surface, and it thins to black at the crown. Bubbles composite
// additively, so overlapping ones need no sort.
//
// ---- audio ------------------------------------------------------------------
//
// Mids (sndSpectrum(0.57), steadied with SND_MID_PRESENCE) thicken the film: the base
// thickness climbs from about 330 nm toward 630 nm, so the bands multiply and
// their colours roll from gold and magenta through to greens and blues, while
// the bubbles' shapes and paths stay calm. The key (uKeyHue, uKeyStrength)
// drifts the room's palette. Brightness follows SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the rise, the sway and the swirl. Complexity (uMorph)
// adds up to four more bubbles. Chaos (uWarp, uTurbulence) wobbles the
// bubbles' surfaces (glassWobble), as if a draught were catching them. Energy
// (uIntensity) scales the mids' thickening. Colour: the room from pal(); the
// film's colour is physics. Space is the rig (camAround, no spin).

#define BB_MAX 12
#define BB_CAM_R 6.2

/** Film thickness in nm at a point `l` on the unit bubble. */
float filmThickness(vec3 l, float base, float t, float seed) {
    float nm = base * (1.0 - 0.45 * l.y);
    nm += 90.0 * sin(l.x * 5.0 + l.z * 3.0 + t * 0.6 + seed);
    nm += 60.0 * sin(l.y * 9.0 - l.x * 2.0 - t * 0.45 + seed * 2.3);
    nm *= 0.2 + 0.8 * (1.0 - smoothstep(0.55, 1.0, l.y));
    return max(nm, 0.0);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float mids = clamp(0.5 * sndSpectrum(0.57) + 0.5 * SND_MID_PRESENCE, 0.0, 1.3);
    float base = 330.0 + 300.0 * mids * glassEnergy();
    int count = 8 + int(floor(glassComplexity() * 4.0 + 0.5));
    float hue = 0.04 * sin(t * 0.021) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chaos = glassChaos();

    vec3 ro;
    vec3 rd;
    camAround(uv, BB_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue);

    for (int i = 0; i < BB_MAX; i++) {
        if (i >= count) break;
        float fi = float(i);
        vec4 h = vec4(glassHash1(fi * 1.31 + 0.7), glassHash1(fi * 2.17 + 3.1), glassHash1(fi * 3.91 + 5.3), glassHash1(fi * 4.73 + 8.9));
        float phase = fract(t * (0.018 + 0.02 * h.x) + h.y);
        float life = smoothstep(0.0, 0.07, phase) * (1.0 - smoothstep(0.9, 1.0, phase));
        float rad = (0.35 + 0.5 * h.z) * life;
        if (rad < 0.02) continue;
        vec3 c = vec3((h.w - 0.5) * 5.0 + 0.3 * sin(t * 0.4 + fi * 1.9),
                      mix(-3.4, 3.4, phase),
                      (fract(h.x * 7.3 + h.z) - 0.5) * 3.2);
        vec2 hit = glassSphere(ro, rd, c, rad);
        if (hit.x <= 0.0) continue;

        vec3 p1 = ro + rd * hit.x;
        vec3 n1 = glassWobble(normalize(p1 - c), p1, chaos);
        float cos1 = max(dot(n1, -rd), 0.0);
        vec3 film1 = glassFilm(filmThickness((p1 - c) / rad, base, t, fi), cos1, 1.33);
        vec3 env1 = glassBackdrop(reflect(rd, n1), hue);

        vec3 p2 = ro + rd * hit.y;
        vec3 n2 = normalize(c - p2);
        float cos2 = max(dot(n2, -rd), 0.0);
        vec3 film2 = glassFilm(filmThickness((p2 - c) / rad, base, t, fi + 0.5), cos2, 1.33);
        vec3 env2 = glassBackdrop(reflect(rd, n2), hue);

        float w1 = 0.3 + 0.7 * (1.0 - cos1) * (1.0 - cos1);
        float w2 = 0.3 + 0.7 * (1.0 - cos2) * (1.0 - cos2);
        col = col * 0.94 + film1 * env1 * w1 * 0.9 + film2 * env2 * w2 * 0.35;
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
