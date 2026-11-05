#version 300 es
precision highp float;
precision highp int;

// The scene objects' overlay (core/viz/frame/ObjectOverlayPass, #515): every live SDF shape of the
// style's geode::scene::Scene, raymarched from the scene camera and blended (premultiplied) over the
// world the style already drew. One screen-covering triangle (fade_vert).
//
// The accepted limit (#302): no depth from the world, so a shape never goes behind world geometry and
// never into its fog; the world's fog only tints a far shape.
//
// Per pixel: a ray-sphere cull over every instance (a bounded loop over the uniform block, breaking at
// uObjectCount), keeping at most OBJ_CANDIDATES the ray can meet, then one sphere trace through the
// union of those, bounded by OBJ_STEPS. Lit like Morph World's surface (a light riding above the eye, a
// rim and a specular), coloured by the world's palette position plus each shape's own offset through the
// style's palette (lib_palette, the values the style itself was given), brightened by its glow, and
// scaled by the world's gain envelope so the objects follow the world's rate-limited brightness.
//
// Written for Geode from textbook sphere tracing (Hart 1996); no code was taken from any published shader.

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;

// lib_palette's inputs: the style's palette this frame (object::OverlayPalette).
uniform float uPalBase;
uniform float uPalRange;
uniform float uPal2Base;
uniform float uPal2Range;
uniform float uPaletteMix;
uniform float uColorShift;
uniform float uHueRange;

//#include lib_palette
//#include lib_camera
//#include lib_objects

#define OBJ_MAX 64
#define OBJ_CANDIDATES 8
#define OBJ_STEPS 64

/**
 * object::OverlayFrame's block, std140: shape i is uObjects[5i .. 5i + 4] =
 * posScale (world position, drawn scale), orient (quaternion x, y, z, w), size (x, y, z, round),
 * material (palette offset, glow, drawn opacity, material id), life (type, age, burst, collapse).
 */
layout(std140) uniform SceneObjects {
    vec4 uObjects[OBJ_MAX * 5];
};
uniform int uObjectCount;
/** The world's look (object::OverlayLook): palette position, emission, gain, fog density. */
uniform vec4 uObjLook;
/** The world's view: roll (radians about the view axis), lens factor on uCamFocal. */
uniform vec4 uObjView;

int gCand[OBJ_CANDIDATES];
int gCount = 0;

float objDistance(int i, vec3 p) {
    vec4 ps = uObjects[i * 5];
    vec4 q = uObjects[i * 5 + 1];
    vec4 size = uObjects[i * 5 + 2];
    vec4 life = uObjects[i * 5 + 4];
    float s = max(ps.w, 1e-4);
    vec3 local = objUnrotate(q, p - ps.xyz) / s;
    return objLife(life.x, local, size.xyz, size.w, life.z, life.w) * s;
}

/** The union of the candidates at p; `which` is the nearest one's instance index. */
float objField(vec3 p, out int which) {
    float d = 1e9;
    which = -1;
    for (int k = 0; k < OBJ_CANDIDATES; ++k) {
        if (k >= gCount) break;
        int i = gCand[k];
        float di = objDistance(i, p);
        if (di < d) {
            d = di;
            which = i;
        }
    }
    return d;
}

vec3 objNormal(vec3 p, float t) {
    float e = 0.001 * (1.0 + t);
    vec2 h = vec2(1.0, -1.0);
    int w;
    return normalize(h.xyy * objField(p + h.xyy * e, w) + h.yyx * objField(p + h.yyx * e, w)
                     + h.yxy * objField(p + h.yxy * e, w) + h.xxx * objField(p + h.xxx * e, w));
}

void main() {
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);
    // The world's own roll and lens, so the shapes stay put in it while it banks and breathes.
    float c = cos(uObjView.x);
    float s = sin(uObjView.x);
    vec2 turned = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    vec3 ro = uCamPos;
    vec3 rd = normalize(uCamBasis * vec3(turned, uCamFocal * max(uObjView.y, 0.05)));

    // Cull: the instances whose bounding sphere (grown by their glow's halo) this ray meets.
    float tMin = 1e9;
    float tMax = 0.0;
    for (int i = 0; i < OBJ_MAX; ++i) {
        if (i >= uObjectCount || gCount >= OBJ_CANDIDATES) break;
        vec4 ps = uObjects[i * 5];
        vec4 size = uObjects[i * 5 + 2];
        vec4 mat = uObjects[i * 5 + 3];
        vec4 life = uObjects[i * 5 + 4];
        float r = ps.w * objBound(life.x, size.xyz, size.w) * (1.05 + 0.5 * mat.y);
        vec3 oc = ro - ps.xyz;
        float b = dot(oc, rd);
        float h = b * b - (dot(oc, oc) - r * r);
        if (h < 0.0) continue;
        h = sqrt(h);
        float t1 = -b + h;
        if (t1 <= 0.0) continue;
        gCand[gCount] = i;
        gCount++;
        tMin = min(tMin, max(-b - h, 0.0));
        tMax = max(tMax, t1);
    }
    if (gCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    float t = tMin;
    bool hit = false;
    int which = -1;
    float nearD = 1e9;
    int nearI = -1;
    for (int k = 0; k < OBJ_STEPS; ++k) {
        int w;
        float d = objField(ro + rd * t, w);
        if (d < nearD) {
            nearD = d;
            nearI = w;
        }
        if (d < 0.001 * (1.0 + t)) {
            hit = true;
            which = w;
            break;
        }
        t += d * 0.8;
        if (t > tMax) break;
    }

    vec3 col = vec3(0.0);
    float alpha = 0.0;
    if (hit) {
        vec4 mat = uObjects[which * 5 + 3];
        vec3 p = ro + rd * t;
        vec3 n = objNormal(p, t);
        vec3 base = pal(uObjLook.x + mat.x);
        vec3 l = normalize(uCamBasis[1] * 0.8 - rd * 0.6 + vec3(0.0, 0.3, 0.0));
        float diff = max(dot(n, l), 0.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        float spec = pow(max(dot(reflect(rd, n), l), 0.0), 24.0);
        col = base * (0.12 + diff) + base * rim * 0.45 * uObjLook.y + vec3(spec) * 0.25;
        col += base * mat.y * 0.5 * uObjLook.y;
        // The world's air tints a far shape; it never hides one.
        vec3 fogCol = pal(uObjLook.x + 0.5 + 0.1 * rd.y) * 0.035;
        col = mix(col, fogCol, min(1.0 - exp(-t * uObjLook.w), 0.6));
        alpha = clamp(mat.z, 0.0, 1.0);
    } else if (nearI >= 0) {
        // A miss near a glowing shape: its halo.
        vec4 ps = uObjects[nearI * 5];
        vec4 mat = uObjects[nearI * 5 + 3];
        float halo = mat.y * exp(-max(nearD, 0.0) / max(0.12 * ps.w, 1e-3));
        col = pal(uObjLook.x + mat.x) * uObjLook.y;
        alpha = clamp(halo * 0.6 * mat.z, 0.0, 1.0);
    }
    col *= uObjLook.z;
    // The world's soft shoulder, so a bright shape rolls off as the world's light does.
    col = col / (1.0 + 0.35 * max(col.r, max(col.g, col.b)));
    fragColor = vec4(col * alpha, alpha);
}
