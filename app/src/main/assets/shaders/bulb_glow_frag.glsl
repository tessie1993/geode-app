#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_bulb

// Bulb Glow: no surface at all. The bulb is a glowing gas - every march step
// emits light by how close it is to the set (exp of the distance estimate)
// and the set's interior is a dense core that also absorbs, so the far side
// fades behind the near one and the body has depth without a single normal.
// The emission is coloured by the orbit trap, so the gas is layered in hue
// like a nebula. Black space around it.
//
// The ray never stops on a hit, so it spends its budget crossing the bound:
// steps are clamped to a band (fine near the set, coarse in the clear) and
// the march ends once the absorbed light makes the rest invisible. Five
// fractal iterations (four at low Detail) - glow hides the finest crust anyway.
//
// ---- audio ------------------------------------------------------------------
//
// SECTIONS choose the power. Each section change (uSectionCount) hashes a new
// integer power from 3 to 12 - a fat three-lobed cloud, a spiny twelve-fold
// star - and the section envelope (uEvents.z) carries the gas from the old
// form to the new: during the 1.5 s glide both estimates are evaluated and
// blended, so the cloud melts from one shape into the next instead of
// snapping. Spectral flux (uBandsHi.w) widens the hue spread of the emission.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the spin of the cloud. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the flux hue spread. Colour is pal() throughout; Space is
// the rig (camAround).

#define BG_MAX_STEPS 96
#define BG_BOUND 1.5
#define BG_CAM_R 3.6
#define BG_GLOW_K 14.0
const float BG_SPIN = 2.0 * CAM_SPIN_UNIT;

float gPowOld;
float gPowNew;
float gBlend;
mat3 gSpin;
int gIters;
vec4 gTrap;

float powerFor(float section) {
    return 3.0 + floor(hash11(section * 0.618 + 0.37) * 9.99);
}

float map(vec3 p) {
    p = bulbChaos(gSpin * p);
    vec4 tr;
    float d = bulbDE(p, gPowNew, gPowNew, vec2(0.0), gIters, tr);
    if (gBlend < 0.999) {
        vec4 tr0;
        float d0 = bulbDE(p, gPowOld, gPowOld, vec2(0.0), gIters, tr0);
        d = mix(d0, d, gBlend);
        tr = mix(tr0, tr, gBlend);
    }
    gTrap = tr;
    return d / bulbChaosLip();
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float sect = clamp(uEvents.z, 0.0, 1.0);
    float flux = clamp(uBandsHi.w, 0.0, 1.0);
    gPowNew = powerFor(uSectionCount);
    gPowOld = powerFor(uSectionCount - 1.0);
    gBlend = 1.0 - smoothstep(0.04, 0.96, sect);
    gSpin = rotY(bulbTime() * 0.05) * rotX(0.35 * sin(bulbTime() * 0.031));
    gIters = bulbIters(5);

    vec3 ro;
    vec3 rd;
    camAround(uv, BG_CAM_R, BG_SPIN, ro, rd);

    float hue = 0.72 + 0.06 * sin(bulbTime() * 0.011);
    float spread = 0.45 + 0.5 * flux * bulbEnergy();

    vec3 acc = vec3(0.0);
    float trans = 1.0;
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BG_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BG_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            float d = map(ro + rd * t);
            float dens = exp(-max(d, 0.0) * BG_GLOW_K);
            float stepLen = clamp(abs(d) * 0.7, 0.012, 0.11);
            vec3 em = pal(hue + spread * (sqrt(gTrap.x) + 0.5 * gTrap.y));
            // The interior burns hotter and whiter than the halo around it.
            float core = step(d, 0.0);
            em = mix(em, mix(em, vec3(1.0), 0.35), core);
            acc += em * dens * stepLen * trans * 2.4;
            trans *= exp(-dens * stepLen * (1.2 + 5.0 * core));
            if (trans < 0.03) break;
            t += stepLen;
        }
    }

    vec3 col = vec3(0.004, 0.003, 0.008) + acc;
    col *= 0.78 + 0.25 * swell;
    fragColor = vec4(grade(col), 1.0);
}
