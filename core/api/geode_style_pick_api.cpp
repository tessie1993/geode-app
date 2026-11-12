#include "api/geode_api.h"
#include "viz/AutoDraw.hpp"
#include "viz/StylePick.hpp"

namespace style_pick = geode::viz::style_pick;
namespace auto_draw = geode::viz::auto_draw;

extern "C" {

int geode_styles_random(const int* family, const unsigned char* open, int count, int on_screen, int family_only) {
    const auto candidates = style_pick::pool(family, open, count, on_screen, family_only != 0);
    // Auto's process-wide draw, so the page and Auto share one seeded source.
    const int k = auto_draw::pick(static_cast<int>(candidates.size()), -1);
    return k < 0 ? -1 : candidates[static_cast<size_t>(k)];
}

void geode_styles_lead(int count, int lead, int* out) { style_pick::lead(count, lead, out); }

}  // extern "C"
