// The widget glow's C ABI (viz/WidgetGlow.hpp). Pure, any thread; marshalled by geode_viz_jni.cpp.
#include "api/geode_api.h"
#include "viz/WidgetGlow.hpp"

extern "C" {

int geode_widget_glow(float art_hue, float art_span, int has_style, int style_hash, int tick, float* out) {
    if (!out) return 0;
    geode::viz::WidgetLight lights[geode::viz::kWidgetLights];
    geode::viz::widgetGlow({art_hue, art_span, has_style != 0, style_hash, tick}, lights);
    for (int i = 0; i < geode::viz::kWidgetLights; ++i) {
        float* o = out + i * 6;
        o[0] = lights[i].x;
        o[1] = lights[i].y;
        o[2] = lights[i].radius;
        o[3] = lights[i].hueDegrees;
        o[4] = lights[i].saturation;
        o[5] = lights[i].value;
    }
    return geode::viz::kWidgetLights;
}

}  // extern "C"
