// C ABI over which wave and envelope controls apply (viz/ModControls). See geode_api.h.
#include <cstddef>
#include <string_view>

#include "api/geode_api.h"
#include "viz/ModControls.hpp"
#include "viz/ModDocument.hpp"

namespace viz = geode::viz;

static_assert(viz::modcontrols::kWave == GEODE_MOD_SHOWS_WAVE);
static_assert(viz::modcontrols::kRate == GEODE_MOD_SHOWS_RATE);
static_assert(viz::modcontrols::kKnob == GEODE_MOD_SHOWS_KNOB);

namespace {

std::string_view view(const char* s) { return s ? std::string_view(s) : std::string_view(); }

}  // namespace

extern "C" {

unsigned geode_mod_lfo_shows(const char* lfos, int slot) {
    if (slot < 0 || slot >= viz::LfoEngine::kSlots) return 0u;
    viz::ModDocument doc;
    doc.readLfos(view(lfos));
    return viz::modcontrols::ofWave(doc.lfos[static_cast<std::size_t>(slot)]);
}

unsigned geode_mod_adsr_shows(const char* adsrs, int slot) {
    if (slot < 0 || slot >= viz::AdsrEngine::kCount) return 0u;
    viz::ModDocument doc;
    doc.readAdsrs(view(adsrs));
    return viz::modcontrols::ofEnvelope(doc.adsrs[static_cast<std::size_t>(slot)]);
}

}  // extern "C"
