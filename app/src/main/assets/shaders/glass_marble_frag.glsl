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

// Glass Marble: a big cat's-eye marble filling the frame, clear glass with a
// twisted four-bladed ribbon of colour sealed inside, the whole marble turning
// slowly on a tilted axis. The ray refracts into the sphere once and crosses
// it along the straight interior chord; sixteen samples along that chord read
// the ribbon - two crossing helicoid vanes, each a plane through the marble's
// axis turned in proportion to height, so the blades spiral from pole to pole -
// and composite it front to back. Each blade is shaded by how squarely it faces
// the key light, so the twist reads as a solid ribbon rather than a smear.
// Whatever the ribbon lets through leaves by the far wall's refraction into the
// backdrop. Outside, Fresnel lays the studio's reflection over the rim.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_PRESENCE, with sndLevel(SND_BASS_ATT)) winds the ribbon tighter: the twist rate
// climbs from about two radians per unit of height toward three and a half, so
// the blades corkscrew on the low end and unwind in the quiet. A section change
// (SND_CUT_COUNT, eased over the section envelope SND_HELD_HARD_CUT) re-picks the two
// blade colours, sliding them round the palette. Brightness follows SND_LEVEL.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the marble's turn. Complexity (uMorph) widens the
// blades. Chaos (uWarp, uTurbulence) wobbles the outer surface (glassWobble),
// so the ribbon swims behind a molten skin. Energy (uIntensity) scales the
// bass winding. Colour: the blades and the room from pal(). Space is the rig
// (camAround, no spin).

#define MB_CAM_R 3.0
#define MB_STEPS 16
#define MB_IOR 1.5

float gTwist;
float gWidth;

/** The two vanes at q (marble frame): density of each, and each blade's facing of `light`. */
vec4 vanes(vec3 q, vec3 light) {
    float a = gTwist * q.y;
    vec2 d = vec2(cos(a), sin(a));
    float inside = (1.0 - smoothstep(0.66, 0.8, length(q)));
    float across = dot(q.xz, d);
    float off = dot(q.xz, vec2(-d.y, d.x));
    float v1 = (1.0 - smoothstep(0.025, 0.1, abs(off))) * (1.0 - smoothstep(gWidth - 0.12, gWidth, abs(across)));
    float v2 = (1.0 - smoothstep(0.025, 0.1, abs(across))) * (1.0 - smoothstep(gWidth - 0.12, gWidth, abs(off)));
    float f1 = abs(dot(vec3(-d.y, 0.0, d.x), light));
    float f2 = abs(dot(vec3(d.x, 0.0, d.y), light));
    return vec4(v1 * inside, v2 * inside, f1, f2);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float bass = clamp(0.6 * SND_BASS_PRESENCE + 0.4 * sndLevel(SND_BASS_ATT), 0.0, 1.2);
    gTwist = 2.1 + 1.3 * bass * glassEnergy();
    gWidth = 0.46 + 0.14 * glassComplexity();
    float seed = glassSeed(SND_CUT_COUNT, SND_HELD_HARD_CUT);
    float hue = 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float bladeHue = hue + 0.6 * seed;

    vec3 ro;
    vec3 rd;
    camAround(uv, MB_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue);

    vec2 hs = glassSphere(ro, rd, vec3(0.0), 1.0);
    if (hs.x > 0.0) {
        mat3 spin = glassEuler(vec3(0.35, t * 0.11, 0.18));
        vec3 light = spin * normalize(vec3(-0.3, 0.8, -0.5));
        vec3 p = ro + rd * hs.x;
        vec3 n = glassWobble(p, p, glassChaos());
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, MB_IOR);
        vec3 d1 = glassRefract(rd, n, 1.0 / MB_IOR);
        float chord = max(-2.0 * dot(p, d1), 0.0);
        float stepLen = chord / float(MB_STEPS);

        vec3 acc = vec3(0.0);
        float trans = 1.0;
        for (int i = 0; i < MB_STEPS; i++) {
            vec3 q = spin * (p + d1 * (float(i) + 0.5) * stepLen);
            vec4 v = vanes(q, light);
            float dens = (v.x + v.y) * stepLen * 9.0;
            if (dens > 1e-3) {
                vec3 c1 = pal(bladeHue + 0.12 * q.y) * (0.35 + 0.65 * v.z);
                vec3 c2 = pal(bladeHue + 0.5 + 0.12 * q.y) * (0.35 + 0.65 * v.w);
                vec3 c = (c1 * v.x + c2 * v.y) / max(v.x + v.y, 1e-3);
                float a = 1.0 - exp(-dens);
                acc += trans * a * c;
                trans *= 1.0 - a;
            }
        }

        vec3 pe = p + d1 * chord;
        vec3 ex = glassRefract(d1, -pe, MB_IOR);
        vec3 inner = acc + trans * glassBackdrop(ex, hue) * 0.9;
        col = mix(inner, glassBackdrop(reflect(rd, n), hue), fr);
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
