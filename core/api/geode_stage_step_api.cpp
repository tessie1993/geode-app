#include "api/geode_api.h"
#include "viz/StageStep.hpp"

namespace stage_step = geode::viz::stage_step;

// The header's numbers are the rule's; a drift here would make the shell pass one fact and mean another.
static_assert(stage_step::kPageStyles == GEODE_STAGE_STEP_PAGE_STYLES);
static_assert(stage_step::kPageCustomize == GEODE_STAGE_STEP_PAGE_CUSTOMIZE);
static_assert(stage_step::kPagePresets == GEODE_STAGE_STEP_PAGE_PRESETS);
static_assert(stage_step::kPageTextures == GEODE_STAGE_STEP_PAGE_TEXTURES);
static_assert(stage_step::kPageSettings == GEODE_STAGE_STEP_PAGE_SETTINGS);
static_assert(stage_step::kSheetShown == GEODE_STAGE_STEP_SHEET_SHOWN);
static_assert(stage_step::kOnLook == GEODE_STAGE_STEP_ON_LOOK);
static_assert(stage_step::kStyles == GEODE_STAGE_STEP_STYLES);
static_assert(stage_step::kLooks == GEODE_STAGE_STEP_LOOKS);

extern "C" {

int geode_stage_step_target(int page, unsigned flags, int look_count) {
    return static_cast<int>(stage_step::target(page, flags, look_count));
}

int geode_stage_step_index(const unsigned char* open, int count, int here, int step) {
    return stage_step::pickIndex(open, count, here, step);
}

}  // extern "C"
