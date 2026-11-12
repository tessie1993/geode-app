#ifndef GEODE_REACTION_TEXT_API_H
#define GEODE_REACTION_TEXT_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The saved text of the user's routes (core/viz/ReactionText: "reactions/2", a route per line). Encode takes
   the routes layout (GEODE_REACT_ROUTE_FLOATS per route) and returns the text's full length, written
   NUL-terminated when capacity holds it. Decode writes at most capacity / GEODE_REACT_ROUTE_FLOATS routes and
   returns how many, -1 for text that is no list (NULL, another version): the style's DNA plays. Pure. */
GEODE_API size_t geode_reactions_text_encode(const float* routes, int route_floats, char* out, size_t capacity);
GEODE_API int    geode_reactions_text_decode(const char* text, float* out, int capacity);

#ifdef __cplusplus
}
#endif

#endif
