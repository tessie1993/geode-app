// MilkDrop's per-frame UV transform for the feedback pass: uvFor() and fold() of
// core/viz/liquid/FeedbackMotion.hpp, term for term (MilkDrop3 milkdropfs.cpp:1699-1833). Change both
// together. The C++ side sanitizes the values before LiquidPass uploads them, so nothing here checks
// for non-finite input.

struct FrameMotion {
    float zoom;
    float zoomExp;
    float rot;
    float cx;
    float cy;
    float dx;
    float dy;
    float sx;
    float sy;
    float warp;
    float warpScale;
    float warpSpeed;
};

uniform float uZoom;
uniform float uZoomExp;
uniform float uRot;
uniform float uCx;
uniform float uCy;
uniform float uDx;
uniform float uDy;
uniform float uSx;
uniform float uSy;
uniform float uWarp;
uniform float uWarpScale;
uniform float uWarpSpeed;

FrameMotion frameMotion() {
    return FrameMotion(uZoom, uZoomExp, uRot, uCx, uCy, uDx, uDy, uSx, uSy, uWarp, uWarpScale, uWarpSpeed);
}

// The UV of the last frame a destination pixel samples; uv is GL's (v up), aspect width / height.
vec2 uvFor(FrameMotion f, vec2 uv, float aspect, float time) {
    float aspX = aspect < 1.0 ? aspect : 1.0;
    float aspY = aspect > 1.0 ? 1.0 / aspect : 1.0;
    float x = uv.x * 2.0 - 1.0;
    float y = uv.y * 2.0 - 1.0;

    float rad = sqrt(x * x * aspX * aspX + y * y * aspY * aspY);
    float zoom2 = pow(f.zoom, pow(f.zoomExp, rad * 2.0 - 1.0));
    float zoomInv = 1.0 / zoom2;
    float u = x * aspX * 0.5 * zoomInv + 0.5;
    float v = -y * aspY * 0.5 * zoomInv + 0.5;

    u = (u - f.cx) / f.sx + f.cx;
    v = (v - f.cy) / f.sy + f.cy;

    float wt = time * f.warpSpeed;
    float si = 1.0 / f.warpScale;
    float f0 = 11.68 + 4.0 * cos(wt * 1.413 + 10.0);
    float f1 = 8.77 + 3.0 * cos(wt * 1.113 + 7.0);
    float f2 = 10.54 + 3.0 * cos(wt * 1.233 + 3.0);
    float f3 = 11.49 + 4.0 * cos(wt * 0.933 + 5.0);
    u += f.warp * sin(wt * 0.333 + si * (x * f0 - y * f3));
    v += f.warp * cos(wt * 0.375 - si * (x * f2 + y * f1));
    u += f.warp * cos(wt * 0.753 - si * (x * f1 - y * f2));
    v += f.warp * sin(wt * 0.825 + si * (x * f0 + y * f3));

    float u2 = u - f.cx;
    float v2 = v - f.cy;
    float c = cos(f.rot);
    float s = sin(f.rot);
    u = u2 * c - v2 * s + f.cx;
    v = u2 * s + v2 * c + f.cy;

    u -= f.dx;
    v -= f.dy;

    u = (u - 0.5) / aspX + 0.5;
    v = (v - 0.5) / aspY + 0.5;
    return vec2(u, 1.0 - v);
}

// Mirror fold into 0..1: an edge pulled inward reflects the picture, never smears it into stripes.
float fold(float p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }
vec2 fold(vec2 p) { return vec2(fold(p.x), fold(p.y)); }
