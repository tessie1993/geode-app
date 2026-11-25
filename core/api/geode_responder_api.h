#ifndef GEODE_RESPONDER_API_H
#define GEODE_RESPONDER_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Event Responder (core/viz/modulation/Responder): Customize › Music's Song event rows, each owning an ordered
   list of responses. The shell draws the rows and the cards and calls these; every default, range, gate and the
   whole of what plays is native. Pure calls take the saved reactions text (api/geode_modulation_api.h's: the
   responses ride in the same text as the routes) and hand the new text back; the three geode_viz_* calls need a
   renderer. A text out: `out` gets as much as `capacity` holds, NUL-terminated, and the return is the length the
   whole text needs. A value list out: at most `capacity` values, and the return is how many were written.

   A row is a Moment ordinal (core/viz Reactions.hpp, in the order of geode_resp_row_name). Every row starts OFF
   on a new style (geode_resp_default_text), and a row that is off also holds back the style's own routes on
   its events, which are listed on the row as "style default" responses and edited with the route calls. */

#define GEODE_RESP_ROWS 15
#define GEODE_RESP_KINDS 16 /* a response's kind: 0 moves a parameter, 1..4 are reserved for Things, Fronts, Fields and Clock control */

/* A response's fields, in geode_resp_values' order and as geode_resp_edit's `field`. Enums travel as ordinals:
   strength is the curve (0 linear, 1 exponential, 2 logarithmic, 3 smooth), unit 0 seconds / 1 beats, drive
   0 once / 1 while / 2 follow / 3 long. Target is the modulation target's ordinal (geode_mod_catalog_text). */
#define GEODE_RESP_ENABLED 0
#define GEODE_RESP_ROW 1
#define GEODE_RESP_KIND 2
#define GEODE_RESP_TARGET 3
#define GEODE_RESP_AMOUNT 4
#define GEODE_RESP_STRENGTH 5
#define GEODE_RESP_FADE_IN 6
#define GEODE_RESP_HOLD 7
#define GEODE_RESP_FADE_OUT 8
#define GEODE_RESP_UNIT 9
#define GEODE_RESP_DRIVE 10
#define GEODE_RESP_EVERY_NTH 11
#define GEODE_RESP_CHANCE 12
#define GEODE_RESP_COOLDOWN 13
#define GEODE_RESP_P0 14 /* P0..P5: the kind's own parameters */
#define GEODE_RESP_FIELDS 20
/* A listed response: the fields, then its origin (0 the user's, 1 a style default) and, for a style default, the
   index of its route in geode_mod_routes_playing (edit it with GEODE_MOD_EDIT_ROUTE_SET), else -1. */
#define GEODE_RESP_ORIGIN_USER 0
#define GEODE_RESP_ORIGIN_STYLE 1
#define GEODE_RESP_LIST_VALUES 22

/* geode_resp_rows, per row: on, the user's responses, the style's default responses, and whether the row
   holds back the style's routes while it is off. */
#define GEODE_RESP_ROW_VALUES 4

/* geode_resp_edit's `op`. */
#define GEODE_RESP_EDIT_ROW_ON 0 /* row, value 0 or 1 */
#define GEODE_RESP_EDIT_ADD 1    /* row, value = the target ordinal a new parameter response moves */
#define GEODE_RESP_EDIT_REMOVE 2 /* row, index = the nth user response of the row */
#define GEODE_RESP_EDIT_SET 3    /* row, index, field (GEODE_RESP_*), value */
#define GEODE_RESP_EDIT_CLEAR 4  /* every response removed and every row off again */

/* geode_viz_responder_status: the rows' event lights (0..1, easing out after the event), then these. */
#define GEODE_RESP_STATUS_COST GEODE_RESP_ROWS /* this frame's work, in route evaluations */
#define GEODE_RESP_STATUS_BUDGET (GEODE_RESP_ROWS + 1)
#define GEODE_RESP_STATUS_LEVEL (GEODE_RESP_ROWS + 2) /* 0 light, 1 heavy, 2 over the budget */
#define GEODE_RESP_STATUS_ACTIVE (GEODE_RESP_ROWS + 3)
#define GEODE_RESP_STATUS_DROPPED (GEODE_RESP_ROWS + 4) /* responses the matrix had no free slot for */
#define GEODE_RESP_STATUS_ROUTES (GEODE_RESP_ROWS + 5)  /* routes of the matrix that play */
#define GEODE_RESP_STATUS_VALUES (GEODE_RESP_ROWS + 6)

/* The saved text of a new style: every row off, the rows holding back the style's own routes. */
GEODE_API int geode_resp_default_text(char* out, int capacity);
/* A row's saved name (KICK, SNARE, … KEY_CHANGE), static; NULL for a row there is none of. */
GEODE_API const char* geode_resp_row_name(int row);

/* Every row's GEODE_RESP_ROW_VALUES, in row order, for the text (NULL: a new style's), the style and the hand mask. */
GEODE_API int geode_resp_rows(const char* reactions, const char* style_id, int seed, unsigned mask, float* out, int capacity);
/* One row's responses, GEODE_RESP_LIST_VALUES each: the user's in order, then the style's own routes on its events. */
GEODE_API int geode_resp_values(const char* reactions, const char* style_id, int seed, unsigned mask, int row, float* out,
                                int capacity);
/* One edit (GEODE_RESP_EDIT_*). The new reactions text, routes and all; an edit that names nothing hands back what
   came in. NULL reactions edits a new style's. */
GEODE_API int geode_resp_edit(const char* reactions, int op, int row, int index, int field, float value, char* out, int capacity);

/* Plays a response once now on the renderer, as its event would (the picker's live preview): `values` are its
   GEODE_RESP_FIELDS fields. Any thread; latched for the next frame. */
GEODE_API void geode_viz_response_preview(geode_viz*, const float* values, int count);
/* The rows' event lights and the cost budget as the last frame left them: GEODE_RESP_STATUS_VALUES values. */
GEODE_API int geode_viz_responder_status(geode_viz*, float* out, int capacity);

#ifdef __cplusplus
}
#endif

#endif
