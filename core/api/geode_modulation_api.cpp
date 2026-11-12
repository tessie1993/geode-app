#include "api/geode_modulation_api.h"

#include <string>
#include <string_view>

#include "api/geode_text_out.hpp"
#include "viz/ModCatalog.hpp"
#include "viz/ModDocument.hpp"
#include "viz/ModTargets.hpp"

// The handle-free half of the modulation ABI: the calls that need no style traits. The ones that deal a
// style's own routes sit beside the renderer (geode_viz_api.cpp), the wave roll beside the other rolls
// (geode_params_api.cpp).

using geode::api::textOut;
using geode::viz::ModDocument;

namespace {

namespace viz = geode::viz;

static_assert(viz::kLfoValues == GEODE_MOD_LFO_VALUES);
static_assert(static_cast<int>(viz::LfoField::Moment) == GEODE_MOD_LFO_MOMENT);
static_assert(viz::kAdsrValues == GEODE_MOD_ADSR_VALUES);
static_assert(static_cast<int>(viz::AdsrField::Target) == GEODE_MOD_ADSR_TARGET);
static_assert(viz::kRouteValues == GEODE_MOD_ROUTE_VALUES);
static_assert(static_cast<int>(viz::RouteField::ReleaseSeconds) == GEODE_MOD_ROUTE_RELEASE_SECONDS);
static_assert(static_cast<int>(viz::ReactionEdit::RouteSet) == GEODE_MOD_EDIT_ROUTE_SET);
static_assert(static_cast<int>(viz::ReactionEdit::RecipeToggle) == GEODE_MOD_EDIT_RECIPE_TOGGLE);
static_assert(static_cast<int>(viz::ReactionEdit::MusicToggle) == GEODE_MOD_EDIT_MUSIC_TOGGLE);
static_assert(static_cast<int>(viz::ReactionEdit::Clear) == GEODE_MOD_EDIT_CLEAR);
static_assert(viz::kRecipeOn == GEODE_MOD_RECIPE_ON && viz::kRecipeFits == GEODE_MOD_RECIPE_FITS);
static_assert(viz::kRecipeOffered == GEODE_MOD_RECIPE_OFFERED);
static_assert(viz::kMusicValues == GEODE_MOD_MUSIC_VALUES);
static_assert(viz::modtargets::kPickerReaction == GEODE_MOD_PICKER_REACTION);

std::string_view view(const char* s) { return s ? std::string_view(s) : std::string_view(); }

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

ModDocument withLfos(const char* lfos) {
    ModDocument doc;
    doc.readLfos(view(lfos));
    return doc;
}

ModDocument withAdsrs(const char* adsrs) {
    ModDocument doc;
    doc.readAdsrs(view(adsrs));
    return doc;
}

}  // namespace

extern "C" {

const char* geode_mod_catalog_text(void) { return viz::modcatalog::text().c_str(); }

int geode_mod_lfos_read(const char* lfos, char* out, int capacity) {
    return textOut(withLfos(lfos).lfosText(), out, capacity);
}

int geode_mod_adsrs_read(const char* adsrs, char* out, int capacity) {
    return textOut(withAdsrs(adsrs).adsrsText(), out, capacity);
}

int geode_mod_reactions_read(const char* reactions, char* out, int capacity) {
    ModDocument doc;
    doc.readReactions(view(reactions), GEODE_HAND_ALL);
    return textOut(doc.reactionsText(), out, capacity);
}

int geode_mod_hand_load(const char* reactions, int has_mask, int saved_mask) {
    if (!reactions) return 0;
    ModDocument doc;
    doc.readReactions(view(reactions), GEODE_HAND_ALL);
    if (!doc.hand.user) return -1;
    return has_mask ? static_cast<int>(static_cast<unsigned>(saved_mask) & GEODE_HAND_ALL) : static_cast<int>(GEODE_HAND_ALL);
}

int geode_mod_lfo_values(const char* lfos, int slot, const char* style_id, float* out, int capacity) {
    return withLfos(lfos).lfoValues(slot, text(style_id), out, capacity);
}

int geode_mod_lfo_edit(const char* lfos, int slot, int field, float value, char* out, int capacity) {
    ModDocument doc = withLfos(lfos);
    doc.setLfo(slot, field, value);
    return textOut(doc.lfosText(), out, capacity);
}

int geode_mod_adsr_values(const char* adsrs, int slot, float* out, int capacity) {
    return withAdsrs(adsrs).adsrValues(slot, out, capacity);
}

int geode_mod_adsr_edit(const char* adsrs, int slot, int field, float value, char* out, int capacity) {
    ModDocument doc = withAdsrs(adsrs);
    doc.setAdsr(slot, field, value);
    return textOut(doc.adsrsText(), out, capacity);
}

int geode_mod_targets_offered(const char* style_id, int picker, int* out, int capacity) {
    return ModDocument::targetsOffered(text(style_id), picker, out, capacity);
}

int geode_mod_adsr_targets_offered(const char* adsrs, int slot, const char* style_id, int* out, int capacity) {
    return withAdsrs(adsrs).adsrTargetsOffered(slot, text(style_id), out, capacity);
}

}  // extern "C"
