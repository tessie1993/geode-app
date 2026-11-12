#include <string_view>
#include <vector>

#include "api/geode_api.h"
#include "viz/StageStep.hpp"

namespace stage_step = geode::viz::stage_step;

// The header's numbers are the rule's; a drift here would make the shell pass one fact and mean another.
static_assert(stage_step::kPageStyles == GEODE_STAGE_STEP_PAGE_STYLES);
static_assert(stage_step::kPageCustomize == GEODE_STAGE_STEP_PAGE_CUSTOMIZE);
static_assert(stage_step::kPagePresets == GEODE_STAGE_STEP_PAGE_PRESETS);
static_assert(stage_step::kPageMilkdrop == GEODE_STAGE_STEP_PAGE_MILKDROP);
static_assert(stage_step::kPageTextures == GEODE_STAGE_STEP_PAGE_TEXTURES);
static_assert(stage_step::kPageSettings == GEODE_STAGE_STEP_PAGE_SETTINGS);
static_assert(stage_step::kSheetShown == GEODE_STAGE_STEP_SHEET_SHOWN);
static_assert(stage_step::kOnMilk == GEODE_STAGE_STEP_ON_MILK);
static_assert(stage_step::kOnLook == GEODE_STAGE_STEP_ON_LOOK);
static_assert(stage_step::kMilkOpen == GEODE_STAGE_STEP_MILK_OPEN);
static_assert(stage_step::kStyles == GEODE_STAGE_STEP_STYLES);
static_assert(stage_step::kLooks == GEODE_STAGE_STEP_LOOKS);
static_assert(stage_step::kMilk == GEODE_STAGE_STEP_MILK);

extern "C" {

int geode_stage_step_target(int page, unsigned flags, int milk_count, int look_count) {
    return static_cast<int>(stage_step::target(page, flags, milk_count, look_count));
}

int geode_stage_step_index(const unsigned char* open, int count, int here, int step) {
    return stage_step::pickIndex(open, count, here, step);
}

int geode_stage_step_milk(const char* const* paths, int count, const char* current, int step) {
    if (paths == nullptr || count <= 0) return -1;
    std::vector<std::string_view> list;
    list.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) list.emplace_back(paths[i] != nullptr ? paths[i] : "");
    return stage_step::stepMilk(list, current != nullptr ? std::string_view(current) : std::string_view(), step);
}

void geode_stage_step_milk_reset(void) { stage_step::resetMilk(); }

}  // extern "C"
