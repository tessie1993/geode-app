#version 300 es
precision highp float;

// One axis of the UI blur (core/viz/UiBlurPass): the visual under the app's glass sheet, blurred at
// quarter resolution so Compose's translucent sheet can show it through frosted glass. A 13-tap Gaussian
// (sigma about 3 texels) read in 7 fetches by sampling between texel pairs with linear filtering; the
// pass runs horizontally, then vertically, twice, all scissored to the sheet's rect plus a margin.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSrc;
// One texel along this pass's axis, in uv: (1 / width, 0) or (0, 1 / height).
uniform vec2 uStep;

const float kOffset[4] = float[4](0.0, 1.4117647, 3.2941176, 5.1764706);
const float kWeight[4] = float[4](0.19648255, 0.29690696, 0.09447040, 0.01038136);

void main() {
    vec3 c = texture(uSrc, vUv).rgb * kWeight[0];
    for (int i = 1; i < 4; ++i) {
        vec2 o = uStep * kOffset[i];
        c += (texture(uSrc, vUv + o).rgb + texture(uSrc, vUv - o).rgb) * kWeight[i];
    }
    fragColor = vec4(c, 1.0);
}
