#include "api/geode_api.h"
#include "viz/Catalogue.hpp"
#include "viz/CataloguePresets.hpp"

extern "C" {

const char* geode_catalogue_text(void) { return geode::viz::catalogue::text().c_str(); }

const char* geode_catalogue_presets_text(void) { return geode::viz::catalogue::presetsText().c_str(); }

}
