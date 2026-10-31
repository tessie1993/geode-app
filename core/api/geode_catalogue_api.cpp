#include "api/geode_api.h"
#include "viz/Catalogue.hpp"

extern "C" {

const char* geode_catalogue_text(void) { return geode::viz::catalogue::text().c_str(); }

}
