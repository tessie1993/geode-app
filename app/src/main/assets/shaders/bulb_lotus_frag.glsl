#version 300 es
#define VIEW_OWNS_SHAPE
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

// Bulb Lotus: the LATITUDE variant of the bulb. Where the classic formula
// multiplies the polar angle (measured from the pole), this one multiplies the
// latitude (measured from the equator) - the other reading of "double the
// angle" - and the body comes out as a flower rather than a seed head: five
// petals round the pole (azimuth power 5) folding up and over each other.
// The latitude power is a real number and moves freely, because latitude has
// no wrap; only the azimuth power has to stay an integer (see lib_bulb).
//
// Tilted toward the eye so the bloom is seen from above, lit from BEHIND by a
// low sun, so the petals glow at their thin edges the way backlit petals do.
// Pastel, foggy, soft; the sun's bloom sits in the sky behind the flower.
//
// ---- audio ------------------------------------------------------------------
//
// PRESENCE opens the flower. The presence band (uBandsHi.y - voice and lead
// instruments) raises the latitude power from 2.2 toward 4.0, so the petals
// unfold and multiply while someone sings and close back into a bud in the
// gaps. Hats (uHits.z) shiver the azimuth phase, a tremble through the petals.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the petal twirl. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the presence bloom and the hat tremble. Colour is pal() throughout; Space is
// the rig (camAround).

#define BL_MAX_STEPS 112
#define BL_BOUND 1.45
#define BL_CAM_R 3.0
#define BL_M 5.0
const float BL_SPIN = -3.0 * CAM_SPIN_UNIT;

float gLat;
float gTwirl;
int gIters;
vec4 gTrap;
mat3 gTilt;

float lotusDE(vec3 p, out vec4 tr) {
    vec3 z = p;
    float dr = 1.0;
    float r = max(length(z), 1e-6);
    float grow = max(gLat, BL_M);
    float esc = float(gIters);
    tr = vec4(dot(p, p), abs(p.y), length(p.xz), 1.0);
    for (int i = 0; i < BULB_MAX_ITERS; i++) {
        if (i >= gIters) break;
        r = max(length(z), 1e-6);
        if (r > 2.0) {
            esc = float(i);
            break;
        }
        float lat = asin(clamp(z.y / r, -1.0, 1.0)) * gLat;
        float lon = atan(z.z, z.x + 1e-12) * BL_M + gTwirl;
        dr = grow * pow(r, BL_M - 1.0) * dr + 1.0;
        float cl = cos(lat);
        z = pow(r, BL_M) * vec3(cl * cos(lon), sin(lat), cl * sin(lon)) + p;
        tr.x = min(tr.x, dot(z, z));
        tr.y = min(tr.y, abs(z.y));
        tr.z = min(tr.z, length(z.xz));
    }
    r = max(length(z), 1e-6);
    tr.w = esc / float(max(gIters, 1));
    return 0.5 * log(r) * r / dr;
}

float map(vec3 p) {
    vec4 tr;
    float d = lotusDE(bulbChaos(gTilt * p), tr) / bulbChaosLip();
    gTrap = tr;
    return d;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float presence = clamp(0.5 * uBandsHi.y + 0.5 * uMidSmooth / 1.5, 0.0, 1.0);
    float hat = clamp(uHits.z, 0.0, 1.0);
    gLat = 2.2 + 1.8 * clamp(presence * bulbEnergy(), 0.0, 1.0);
    gTwirl = 0.05 * hat * bulbEnergy() * sin(bulbTime() * 23.0) + 0.1 * sin(bulbTime() * 0.05);
    gIters = bulbIters(6);
    gTilt = rotX(0.55);

    vec3 ro;
    vec3 rd;
    camAround(uv, BL_CAM_R, BL_SPIN, ro, rd);

    float hue = 0.92 + 0.04 * sin(bulbTime() * 0.013);
    // The sun sits behind the flower relative to the eye, a little high.
    vec3 sun = normalize(-normalize(ro) + vec3(0.0, 0.45, 0.0));

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BL_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BL_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            near = min(near, d);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.8, eps * 0.5);
        }
    }

    // Pastel haze with the sun's bloom behind the subject.
    float sunAmt = max(dot(rd, sun), 0.0);
    vec3 haze = mix(vec3(0.16, 0.13, 0.17), mix(pal(hue + 0.15), vec3(1.0), 0.6) * 0.32, smoothstep(-0.5, 0.6, rd.y));
    vec3 col = haze + vec3(1.0, 0.86, 0.72) * pow(sunAmt, 24.0) * 0.45;
    col += mix(pal(hue), vec3(1.0), 0.5) * exp(-max(near, 0.0) * 12.0) * 0.12;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        float ndv = max(dot(n, -rd), 0.0);
        // Wrap light from the sun, so the side facing away is not black.
        float wrap = max((dot(n, sun) + 0.45) / 1.45, 0.0);
        // Transmission: thin edges seen against the sun light up.
        float trans = pow(sunAmt, 3.0) * pow(1.0 - ndv, 1.5) * (0.5 + 0.5 * smoothstep(0.4, 0.0, trap.y));
        float ao = bulbStepAO(used) * (0.55 + 0.45 * smoothstep(0.08, 0.8, trap.x));
        vec3 petal = mix(pal(hue + 0.35 * trap.y + 0.25 * sqrt(trap.x)), vec3(1.0), 0.45);
        col = petal * (0.30 + 0.55 * wrap) * ao;
        col += vec3(1.0, 0.80, 0.62) * trans * 0.9;
        col += petal * (0.5 + 0.5 * n.y) * 0.12;
        col = mix(col, haze, 1.0 - exp(-max(hitT - 1.8, 0.0) * 0.5));
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
