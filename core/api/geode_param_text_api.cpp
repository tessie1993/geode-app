// C ABI over the Customize value text (viz/ParamText). See geode_api.h.
#include "api/geode_api.h"
#include "api/geode_text_out.hpp"
#include "viz/ParamText.hpp"

using geode::viz::ParamUnit;

int geode_param_text(const char* key, int unit, float value, float lo, float hi, float def, char* out, int capacity) {
    const bool named = unit >= 0 && unit <= static_cast<int>(ParamUnit::Plain);
    const ParamUnit shown = named ? static_cast<ParamUnit>(unit) : geode::viz::paramUnitFor(key ? key : "", lo, hi);
    return geode::api::textOut(geode::viz::paramText(value, shown, def, lo < 0.0f), out, capacity);
}
