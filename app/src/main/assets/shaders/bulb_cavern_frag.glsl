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

// Bulb Cavern: the power-12 bulb turned inside out, and the eye flying inside
// it. Space is inverted in a sphere (q = K p / |p|^2) before the fractal sees
// it, so the bulb's OUTSIDE - everything around it, out to infinity - is
// folded into a closed room around the origin, and its surface becomes the
// room's walls. Every bump on the bulb's crust is a stalactite pointing in,
// every crease a passage leading out. The eye rides a closed loop through the
// room (the rig's travel moves it, the rig's look turns it), lit only by a
// lantern it carries; the walls are dark wet stone veined with light where
// the orbit trap runs thin, and the far end sinks into teal fog.
//
// THE ESTIMATE. Inversion scales lengths by K / |p|^2, so a distance measured
// in q is converted back by |p|^2 / K. That is exact only locally, so the
// march takes 0.6 of it. THE BOUND is the room itself: the bulb contains a
// ball of radius ~0.7 round its centre, which inverts to "everything past
// K / 0.7", so every ray from inside hits a wall within BV_FAR.
//
// ---- audio ------------------------------------------------------------------
//
// The DROP twists the cave. On a drop (uEvents.y) the whole room wrings about
// the vertical axis - the twist grows with height, so floor and ceiling turn
// opposite ways - on a bump envelope that rises and settles over the drop's
// 2.5 s, and the march shortens its steps by the twist's stretch while it
// lasts. The veins breathe gently with uSwell. Nothing on a hit changes
// brightness.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the vein drift. Complexity (uMorph) adds up to two
// fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) bends
// the bulb before it is inverted (bulbChaos in q, estimate divided by
// bulbChaosLip). Energy (uIntensity) scales the drop twist. Colour is pal();
// Space is the rig (travel and look drive the eye; its lateral wander offsets
// the loop).

#define BV_MAX_STEPS 128
#define BV_K 3.2
#define BV_FAR 7.0
#define BV_LOOP_R 1.35
const float BV_LOOP_RATE = 8.0 * CAM_SPIN_UNIT;

float gTwist;
float gTwistLip;
int gIters;
vec4 gTrap;

float map(vec3 p) {
    p.xz = rot2(gTwist * p.y) * p.xz;
    float r2 = max(dot(p, p), 1e-4);
    vec3 q = p * (BV_K / r2);
    vec4 tr;
    float d = bulbDE(bulbChaos(q), 12.0, 12.0, vec2(0.0), gIters, tr) / bulbChaosLip();
    gTrap = tr;
    return d * (r2 / BV_K) * 0.6 / gTwistLip;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    gTwist = 0.32 * dropBump * bulbEnergy();
    // Twisting by k*y stretches by at most sqrt(1 + (k*rho)^2); rho stays under ~4 in the room.
    gTwistLip = 1.0 + abs(gTwist) * 4.0;
    gIters = bulbIters(6);

    // The eye's loop through the room, and a frame that looks along it. The
    // rig's basis rides on top, so its look, banking and shake still apply.
    float a = uCamTravel * BV_LOOP_RATE;
    vec3 path = vec3(BV_LOOP_R * cos(a), 0.4 * sin(2.0 * a), BV_LOOP_R * sin(a));
    vec3 tang = normalize(vec3(-BV_LOOP_R * sin(a), 0.8 * cos(2.0 * a), BV_LOOP_R * cos(a)));
    mat3 frame = bulbFrame(tang, vec3(0.0, 1.0, 0.0));
    vec3 ro = path + frame * vec3(clamp(uCamPos.xy, -2.0, 2.0) * 0.12, 0.0);
    vec3 rd = normalize(frame * (uCamBasis * vec3(uv, uCamFocal)));

    float hue = 0.48 + 0.03 * sin(bulbTime() * 0.019);
    vec3 fog = pal(hue) * 0.05 + vec3(0.004, 0.012, 0.014);

    float t = 0.02;
    float hitT = -1.0;
    float used = 0.0;
    vec4 trap = vec4(1.0);
    for (int i = 0; i < BV_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > BV_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = bulbEps(t);
        if (d < eps) {
            hitT = t;
            trap = gTrap;
            break;
        }
        t += max(d, eps * 0.5);
    }

    vec3 col = fog;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT) * 1.5);
        // The lantern: at the eye, falling off with distance.
        float lamp = 1.0 / (1.0 + 0.35 * hitT * hitT);
        float dif = max(dot(n, -rd), 0.0);
        vec3 h = normalize(-rd + vec3(0.0, 0.35, 0.0));
        float wet = pow(max(dot(n, h), 0.0), 60.0);
        float ao = bulbStepAO(used);
        vec3 stone = mix(vec3(0.05, 0.05, 0.055), pal(hue + 0.55) * 0.18, 0.4);
        col = stone * (0.25 + 0.9 * dif) * lamp * ao;
        col += vec3(0.9, 0.95, 1.0) * wet * lamp * 0.5 * ao;
        // Veins: where the orbit hugged the equatorial plane, the rock glows.
        float vein = smoothstep(0.05, 0.0, trap.y) * (0.6 + 0.4 * sin(sqrt(trap.x) * 9.0 + bulbTime() * 0.3));
        col += pal(hue + 0.12 + 0.2 * sqrt(trap.x)) * vein * (0.35 + 0.15 * swell);
        col = mix(col, fog, 1.0 - exp(-hitT * 0.32));
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
