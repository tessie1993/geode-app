#include "api/geode_api.h"
#include "viz/StageRun.hpp"

// The header's numbers are the rule's; a drift here would make the shell pass one bit and mean another.
static_assert(geode::viz::stage::kCasting == GEODE_STAGE_CASTING);
static_assert(geode::viz::stage::kCastLeadsAuto == GEODE_STAGE_CAST_LEADS_AUTO);
static_assert(geode::viz::stage::kDisplayOn == GEODE_STAGE_DISPLAY_ON);
static_assert(geode::viz::stage::kDisplayFailureShift == GEODE_STAGE_DISPLAY_FAILURE_SHIFT);
static_assert(geode::viz::stage::kPresentFound == GEODE_STAGE_PRESENT_FOUND);
static_assert(geode::viz::stage::kHostCasting == GEODE_STAGE_HOST_CASTING);
static_assert(geode::viz::stage::kHostOpaque == GEODE_STAGE_HOST_OPAQUE && geode::viz::stage::kHostFaint == GEODE_STAGE_HOST_FAINT);

extern "C" {

unsigned geode_stage_run(unsigned flags) { return geode::viz::stage::run(flags); }

int geode_stage_host(unsigned flags, const int* bounds, int window_w, int window_h, int* rect_out) {
    geode::viz::frame::tee::Rect b;
    if (bounds) b = {bounds[0], bounds[1], bounds[2], bounds[3]};
    const geode::viz::stage::Host h = geode::viz::stage::host(flags, b, window_w, window_h);
    if (rect_out) {
        rect_out[0] = h.rect.x0;
        rect_out[1] = h.rect.y0;
        rect_out[2] = h.rect.x1;
        rect_out[3] = h.rect.y1;
    }
    return h.alpha;
}

int geode_stage_display_eligible(unsigned flags) { return geode::viz::stage::displayEligible(flags) ? 1 : 0; }

int geode_stage_pick_display(const int* ids, const unsigned* flags, int count, int preferred_id) {
    if (count <= 0) return -1;
    static_assert(sizeof(int) == sizeof(int32_t) && sizeof(unsigned) == sizeof(uint32_t));
    return geode::viz::stage::pickDisplay(
        reinterpret_cast<const int32_t*>(ids), reinterpret_cast<const uint32_t*>(flags), static_cast<size_t>(count), preferred_id);
}

int geode_stage_presents(unsigned flags) { return geode::viz::stage::presents(flags) ? 1 : 0; }

}  // extern "C"
