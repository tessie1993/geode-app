#include "viz/FlashLimiter.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz::flash_limit {

float quantise(float v) { return std::floor(std::clamp(v, 0.0f, 1.0f) * kQuantSteps + 0.5f) / kQuantSteps; }

float quantiseDown(float v) { return std::floor(std::clamp(v, 0.0f, 1.0f) * kQuantSteps) / kQuantSteps; }

float srgbToLinear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float luminance(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// Mirrored line for line by flashStep() in lib_flash_limit.glsl. See VisualSafety.hpp for the
// budget's arithmetic; the invariant everything rests on is |anchor - shown| <= kFlashBand.
TileState step(const TileState& previous, float raw, float dt) {
    using namespace safety;
    const float measured = quantise(raw);
    TileState next;
    next.primed = true;
    if (!previous.primed) {
        next.shown = measured;
        next.anchor = measured;
        next.budget = kFlashBudgetCapacity;
        return next;
    }
    const float h = kFlashBand;
    const float stepDt = std::clamp(dt, 0.0f, kFlashMaxDt);
    const float shown = previous.shown;
    const float anchor = previous.anchor;
    const float budget = std::min(kFlashBudgetCapacity, previous.budget + kFlashBudgetRefill * stepDt);
    const float freeMove = kFlashFreeRate * stepDt;
    // How far the shown value may go each way: what is left of the band on that side (free), the
    // free rate, and the whole bucket. Moving by `up` drags the anchor by at most freeMove + budget.
    const float up = std::max(anchor + h - shown, 0.0f) + freeMove + budget;
    const float down = std::max(shown - anchor + h, 0.0f) + freeMove + budget;
    const float moved = std::clamp(shown + std::clamp(measured - shown, -down, up), 0.0f, 1.0f);
    const float dragged = std::clamp(anchor, moved - h, moved + h);
    const float spent = std::max(std::fabs(dragged - anchor) - freeMove, 0.0f);
    next.shown = quantise(moved);
    next.anchor = quantise(dragged);
    next.budget = quantiseDown(std::max(budget - spent, 0.0f) / kFlashBudgetCapacity) * kFlashBudgetCapacity;
    return next;
}

TileGain gainFor(float shown, float raw) {
    TileGain g;
    if (shown < raw) {
        g.gain = shown / raw;
    } else if (shown > raw) {
        // shown <= 1, so raw < 1 here.
        g.lift = (shown - raw) / (1.0f - raw);
    }
    return g;
}

}  // namespace geode::viz::flash_limit
