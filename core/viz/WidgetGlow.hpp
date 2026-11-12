#pragma once

// The widget's generated picture when its style has no thumbnail: three soft lights over black, in the cover's hue
// or a hue of the style's own. This is the part that decides how the picture looks (where each light sits at a tick,
// its radius, hue, saturation and value); the shell only paints the gradients. Ported from the Kotlin
// `WidgetVisual.glow` it replaces, in the same arithmetic, so the old widget is unchanged. Pure, any thread.
namespace geode::viz {

inline constexpr int kWidgetLights = 3;

// The publisher's tick moves the lights a twelfth of a turn.
inline constexpr int kWidgetTicksPerTurn = 12;

struct WidgetLight {
    float x = 0.0f;           // centre as a share of the bitmap's width
    float y = 0.0f;           // centre as a share of its height
    float radius = 0.0f;      // as a share of its width
    float hueDegrees = 0.0f;  // 0..360
    float saturation = 0.0f;
    float value = 0.0f;
};

struct WidgetGlowIn {
    float artHue = -1.0f;   // the cover's base hue, or below zero for none
    float artSpan = 0.0f;   // the cover's hue span
    bool hasStyle = false;  // a style is on screen
    int styleHash = 0;      // Java's String.hashCode of the style id: each style keeps its colour
    int tick = 0;
};

void widgetGlow(const WidgetGlowIn& in, WidgetLight out[kWidgetLights]);

}  // namespace geode::viz
