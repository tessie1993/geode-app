#include <cstring>
#include <string>

#include "api/geode_reaction_text_api.h"
#include "viz/ReactionText.hpp"
#include "viz/Reactions.hpp"

using geode::viz::ReactionEngine;

static_assert(ReactionEngine::kRoutes == GEODE_REACT_ROUTES);
static_assert(ReactionEngine::kConfigFloats == GEODE_REACT_ROUTE_FLOATS);

extern "C" {

size_t geode_reactions_text_encode(const float* routes, int route_floats, char* out, size_t capacity) {
    const int count = routes && route_floats > 0 ? route_floats / GEODE_REACT_ROUTE_FLOATS : 0;
    const std::string text = geode::viz::reactiontext::encode(routes, count);
    if (out && capacity > text.size()) std::memcpy(out, text.c_str(), text.size() + 1);
    return text.size();
}

int geode_reactions_text_decode(const char* text, float* out, int capacity) {
    if (!text) return -1;
    float routes[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int read = geode::viz::reactiontext::decode(text, routes);
    if (read < 0) return -1;
    const int room = out && capacity > 0 ? capacity / GEODE_REACT_ROUTE_FLOATS : 0;
    const int n = read < room ? read : room;
    if (n > 0) std::memcpy(out, routes, static_cast<size_t>(n) * GEODE_REACT_ROUTE_FLOATS * sizeof(float));
    return n;
}

}  // extern "C"
