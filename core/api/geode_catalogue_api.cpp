#include <limits>
#include <string>
#include <utility>

#include "api/geode_api.h"
#include "viz/Catalogue.hpp"
#include "viz/CataloguePresets.hpp"

namespace {

namespace catalogue = geode::viz::catalogue;

// A string answer that is built per call lives until the caller's next call on the same thread.
const char* hold(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.c_str();
}

}  // namespace

extern "C" {

const char* geode_catalogue_style_ids(int trait) {
    if (trait < 0 || trait >= catalogue::kTraitCount) return "";
    try {
        return catalogue::idsText(static_cast<catalogue::Trait>(trait)).c_str();
    } catch (...) {
        return "";
    }
}

const char* geode_catalogue_picker_names(void) {
    try {
        return catalogue::pickerNamesText().c_str();
    } catch (...) {
        return "";
    }
}

const char* geode_catalogue_picker_ids(const char* family) {
    thread_local std::string ids;
    try {
        return hold(ids, catalogue::pickerIdsText(family ? family : ""));
    } catch (...) {
        return "";
    }
}

const char* geode_catalogue_resolve(const char* style_id) {
    thread_local std::string resolved;
    try {
        return hold(resolved, catalogue::resolveId(style_id ? style_id : ""));
    } catch (...) {
        return "";
    }
}

const char* geode_catalogue_preset_style(const char* style_id) {
    thread_local std::string shelf;
    try {
        return hold(shelf, catalogue::presetStyle(style_id ? style_id : ""));
    } catch (...) {
        return "";
    }
}

const char* geode_catalogue_scope_of(const char* param_key) {
    try {
        return catalogue::name(catalogue::scopeOf(param_key ? param_key : ""));
    } catch (...) {
        return catalogue::name(catalogue::Scope::Universal);
    }
}

int geode_catalogue_reaches(const char* scope_name, const char* style_id) {
    try {
        const auto scope = catalogue::scopeNamed(scope_name ? scope_name : "");
        // A name the catalogue does not have hides nothing, like a control no scope names.
        return catalogue::reaches(scope.value_or(catalogue::Scope::Universal), style_id ? style_id : "") ? 1 : 0;
    } catch (...) {
        return 1;
    }
}

float geode_catalogue_override(const char* style_id, const char* param_key) {
    try {
        const auto held = catalogue::overrideOf(style_id ? style_id : "", param_key ? param_key : "");
        return held.value_or(std::numeric_limits<float>::quiet_NaN());
    } catch (...) {
        return std::numeric_limits<float>::quiet_NaN();
    }
}

const char* geode_catalogue_presets_text(void) { return geode::viz::catalogue::presetsText().c_str(); }

}
