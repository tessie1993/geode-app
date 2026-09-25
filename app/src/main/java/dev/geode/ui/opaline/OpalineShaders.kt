package dev.geode.ui.opaline

/**
 * AGSL for one Opaline body, drawn in the component's own coordinates (pixels).
 *
 * The body is a height field over a rounded rectangle seen from slightly above: a top face with
 * quarter-round shoulders and an optional crown dome, the swept sidewall visible beneath it, and
 * a contact shadow plus transmitted caustic on the receiving plane. Normals come from the
 * analytic gradient of that height field, so a press (the `MotionController` dent, `uContact`)
 * moves the highlights physically instead of scaling a picture.
 *
 * Surface terms follow `src/shaders/opaline.js` and `src/materials.js`: the cloud and vein
 * interior of `SURFACE_COLOR` (`mix(diffuse, diffuse * tint * 1.55, cloud)`), the contact
 * emission of `SURFACE_EMISSION` (accent * excitation * 0.42 at the touch point), the film
 * thickness field of `SURFACE_FILM`, Schlick Fresnel from each family's IOR and a GGX clearcoat
 * lobe at its clearcoat roughness. A raster pass cannot refract the scene behind the body, so
 * transmission is expressed as coverage and as the deepening of colour along longer paths
 * through the shoulders; `OPTICS.md`'s traced caustics are approximated by a noise-modulated
 * pool of accent light inside the shadow.
 */
internal const val OPALINE_BODY_AGSL = """
uniform float2 uSize;
uniform float uRadius;
uniform float uHeight;
uniform float uWall;
uniform float uDome;
uniform float uRecess;
uniform float3 uContact;
uniform float4 uState;
uniform float4 uMat;
uniform float4 uMat2;
uniform float4 uMat3;
uniform float3 uKey;
uniform float3 uFill;
layout(color) uniform half4 uBody;
layout(color) uniform half4 uDeep;
layout(color) uniform half4 uAccent;
layout(color) uniform half4 uSky;
layout(color) uniform half4 uGround;
layout(color) uniform half4 uShade;
layout(color) uniform half4 uKeyColor;
layout(color) uniform half4 uFillColor;
layout(color) uniform half4 uRimColor;
uniform shader uCloud;

float sdBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float2 sdBoxGrad(float2 p, float2 b, float r) {
    float2 s = float2(p.x < 0.0 ? -1.0 : 1.0, p.y < 0.0 ? -1.0 : 1.0);
    float2 q = abs(p) - b + r;
    if (q.x > 0.0 && q.y > 0.0) {
        return s * normalize(q);
    }
    return q.x > q.y ? float2(s.x, 0.0) : float2(0.0, s.y);
}

float ggx(float nh, float roughness) {
    float a = max(roughness * roughness, 0.0025);
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

half4 main(float2 p) {
    float3 body = float3(uBody.rgb);
    float3 deep = float3(uDeep.rgb);
    float3 accent = float3(uAccent.rgb);
    float3 sky = float3(uSky.rgb);
    float3 keyColor = float3(uKeyColor.rgb);
    float3 fillColor = float3(uFillColor.rgb);
    float coverage = uMat.x;
    float cloudAmount = uMat.y;
    float iridescence = uMat.z;
    float coatRoughness = uMat.w;
    float clearcoat = uMat2.x;
    float flow = uMat2.y;
    float f0 = uMat2.z;
    float receiverStrength = uMat2.w;
    float baseRoughness = uMat3.x;
    float texel = uMat3.y;
    float seed = uMat3.z;
    float disabled = uMat3.w;
    float press = uContact.z;
    float excite = uState.x;
    float selected = uState.y;
    float focus = uState.z;
    float time = uState.w;

    float wall = uWall;
    float2 topB = float2(uSize.x * 0.5, max(uSize.y - wall, 2.0) * 0.5);
    float2 topC = topB;
    float rTop = min(uRadius, min(topB.x, topB.y));
    float dTop = sdBox(p - topC, topB, rTop);
    float2 silB = float2(topB.x, topB.y + wall * 0.5);
    float2 silC = float2(topC.x, topC.y + wall * 0.5);
    float dSil = sdBox(p - silC, silB, rTop);

    float4 receiver = float4(0.0);
    if (receiverStrength > 0.0) {
        // A pressed body sits closer to the plane, so its shadow tightens and darkens.
        float lift = 1.0 - 0.4 * clamp(press, 0.0, 1.0);
        float2 off = -uKey.xy * (uHeight * 0.8 + 3.0) * lift;
        float blur = max(uHeight * 1.1 * lift + 4.0, 2.0);
        float dS = sdBox(p - silC - off, silB, rTop);
        float shade = (1.0 - smoothstep(-blur, blur, dS)) * receiverStrength * (0.6 + 0.4 * lift);
        receiver = float4(uShade) * shade;
        float2 cOff = off * 1.35;
        float dC = sdBox(p - silC - cOff, silB * 0.6, rTop * 0.6);
        float pattern = float(uCloud.eval((p + cOff) * texel * 1.7 + float2(seed, 19.0)).g);
        float caustic = (1.0 - smoothstep(-blur, blur * 0.5, dC)) * smoothstep(0.35, 0.8, pattern);
        receiver.rgb += accent * caustic * receiverStrength * (1.0 - coverage * 0.6) * 0.35;
    }

    float4 color = float4(0.0);
    float covSil = clamp(0.5 - dSil, 0.0, 1.0);
    if (covSil > 0.0 && wall > 0.0) {
        // The sidewall below the top face, curving around the rounded ends.
        float t = clamp(dTop / max(wall, 1.0), 0.0, 1.0);
        float ex = abs(p.x - silC.x) - (silB.x - rTop);
        float nx = ex > 0.0 ? clamp(ex / max(rTop, 1.0), 0.0, 1.0) * sign(p.x - silC.x) : 0.0;
        float3 wn = normalize(float3(nx, 0.75, sqrt(max(1.0 - nx * nx, 0.0))));
        float wl = clamp(dot(wn, uKey) * 0.5 + 0.5, 0.0, 1.0);
        float wf = clamp(dot(wn, uFill), 0.0, 1.0);
        // Seen through its thickness the wall is the deep, saturated body colour; transmitted
        // light pools toward its foot and a bounce line marks where it meets the plane.
        float3 wallRgb = mix(deep, body, 0.18 + 0.2 * wl) * (0.62 + 0.3 * wl);
        wallRgb += fillColor * body * wf * 0.3;
        wallRgb += mix(body, sky, 0.3) * 0.3 * t * t * (1.2 - coverage * 0.5);
        wallRgb += sky * smoothstep(-2.5, -0.5, dSil) * 0.22;
        float wa = mix(coverage, 1.0, 0.5) * covSil * (1.0 - disabled * 0.35);
        color = float4(clamp(wallRgb, 0.0, 1.0) * wa, wa);
    }

    float covTop = clamp(0.5 - dTop, 0.0, 1.0);
    if (covTop > 0.0) {
        float e = max(-dTop, 0.0);
        float shoulder = max(min(uHeight * 1.6, min(topB.x, topB.y)), 1.0);
        float w = 1.0 - clamp(e / shoulder, 0.0, 1.0);
        float slope = w / max(sqrt(max(1.0 - w * w, 0.0)), 0.08);
        float2 grad = -sdBoxGrad(p - topC, topB, rTop) * slope * (uHeight / shoulder);
        float2 q = (p - topC) / topB;
        grad += -2.0 * uDome * float2(q.x / topB.x, q.y / topB.y);
        float sigma = max(min(topB.x, topB.y) * 0.85, 10.0);
        float2 dc = p - uContact.xy;
        float g = exp(-dot(dc, dc) / (2.0 * sigma * sigma));
        grad += press * uHeight * 0.6 * g * dc / (sigma * sigma);
        grad *= 1.0 - 2.0 * uRecess;
        float3 n = normalize(float3(-grad, 1.0));
        float ndv = clamp(n.z, 0.0, 1.0);
        float ndl = dot(n, uKey);

        float2 tc = p * texel + float2(seed, seed * 0.37);
        float drift = time * flow;
        float c1 = float(uCloud.eval(tc + float2(drift * 9.0, drift * 4.0)).r);
        float c2 = float(uCloud.eval(tc * 0.5 + float2(c1 * 40.0, -c1 * 25.0)).r);
        float cloud = mix(c1, c2, 0.55);
        float vein = smoothstep(0.42, 0.68, cloud);
        float3 tint = mix(deep, accent, vein);
        float3 base = mix(body, body * tint * 1.55, cloudAmount);

        // Dense gel scatters light through its thick centre (milky), while the long paths through
        // the shoulders absorb toward the deep colour; recessed wells are shaded as a cavity.
        float wrap = clamp((ndl + 0.55) / 1.55, 0.0, 1.0);
        float3 lit = base * mix(float3(1.0), keyColor, 0.4) * (0.64 + 0.44 * wrap);
        lit += base * fillColor * clamp(dot(n, uFill), 0.0, 1.0) * 0.18;
        lit += body * vein * cloudAmount * 0.45;
        float edge = 1.0 - smoothstep(0.0, shoulder * 1.4, e);
        float path = clamp(edge * 0.55, 0.0, 0.6) * (1.0 - uRecess);
        lit = mix(lit, deep * (0.62 + 0.5 * wrap), path);
        float cavity = uRecess * (1.0 - smoothstep(0.0, shoulder * 1.3, e)) * clamp(0.6 - ndl, 0.0, 1.0);
        lit *= 1.0 - cavity * 0.55;

        // Key light entering the upper side leaves through the opposite shoulders as a glow.
        float2 away = -normalize(uKey.xy);
        float2 tilt = n.xy / max(length(n.xy), 0.0001);
        float back = clamp(dot(tilt, away), 0.0, 1.0) * (1.0 - ndv) * (1.0 - uRecess);
        lit += mix(body, sky, 0.45) * back * (1.2 - coverage * 0.5) * 0.55;
        lit += float3(uRimColor.rgb) * pow(1.0 - ndv, 3.0) * 0.28 * (1.0 - uRecess * 0.7);

        // Thin-film interference over the cloud's thickness field, as pastel colour.
        float filmT = clamp(cloud * 0.7 + (1.0 - clamp(p.y / uSize.y, 0.0, 1.0)) * 0.3, 0.0, 1.0);
        float3 film = 0.5 + 0.5 * cos(6.2831853 * (mix(0.9, 1.7, filmT) + (1.0 - ndv) * 1.2 + float3(0.0, 0.33, 0.67)));
        float3 pastel = mix(float3(1.0), film, 0.38) * 1.08;
        float irid = clamp(iridescence * 2.2 * (0.4 + 0.6 * vein + 0.6 * (1.0 - ndv)), 0.0, 0.5);
        lit = mix(lit, lit * pastel, irid);

        float fres = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);
        float3 r = reflect(float3(0.0, 0.0, -1.0), n);
        float3 env = mix(float3(uGround.rgb), sky, smoothstep(0.3, 0.9, clamp(-r.y * 0.5 + 0.5, 0.0, 1.0)));
        lit = mix(lit, env, clamp(fres * clearcoat, 0.0, 1.0));
        lit += sky * (1.0 - smoothstep(0.6, 2.2, e)) * 0.3 * (1.0 - uRecess * 0.6);

        float3 h = normalize(uKey + float3(0.0, 0.0, 1.0));
        float nh = clamp(dot(n, h), 0.0, 1.0);
        float nl = clamp(ndl, 0.0, 1.0);
        float fh = f0 + (1.0 - f0) * pow(1.0 - h.z, 5.0);
        float spec = (ggx(nh, max(coatRoughness, 0.16)) * clearcoat + ggx(nh, max(baseRoughness, 0.32)) * 0.8)
            * fh * nl / max(4.0 * ndv, 0.4);
        lit += keyColor * spec / (1.0 + spec);

        float contact = exp(-dot(dc, dc) / max(sigma * sigma * 0.5, 16.0));
        lit += accent * excite * contact * 0.42;
        lit += accent * selected * (0.10 + 0.22 * edge);

        float grey = dot(lit, float3(0.2126, 0.7152, 0.0722));
        lit = mix(lit, float3(grey) * 0.8, disabled * 0.6);
        float ta = mix(coverage, 1.0, clamp(edge * 0.5 + fres * 0.8, 0.0, 1.0)) * covTop * (1.0 - disabled * 0.35);
        float4 top = float4(clamp(lit, 0.0, 1.0) * ta, ta);
        color = top + color * (1.0 - top.a);
    }

    color = color + receiver * (1.0 - color.a);
    if (focus > 0.0) {
        float ring = (1.0 - smoothstep(0.0, 1.5, abs(dSil - 3.0))) * focus;
        color = float4(accent * ring, ring) + color * (1.0 - ring);
    }
    return half4(color);
}
"""
