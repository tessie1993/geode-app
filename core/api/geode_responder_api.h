#ifndef GEODE_RESPONDER_API_H
#define GEODE_RESPONDER_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Event Responder (core/viz/modulation/Responder): Customize › Music's Song event rows, each owning an ordered
   list of responses. The shell draws the rows and the cards and calls these; every default, range and the
   whole of what plays is native. Pure calls take the saved reactions text (api/geode_modulation_api.h's: the
   responses ride in the same text as the routes) and hand the new text back; the three geode_viz_* calls need a
   renderer. A text out: `out` gets as much as `capacity` holds, NUL-terminated, and the return is the length the
   whole text needs. A value list out: at most `capacity` values, and the return is how many were written.

   A row is a Moment ordinal (core/viz Reactions.hpp, in the order of geode_resp_row_name). Every row starts OFF
   on a new style (geode_resp_default_text), and a row that is off also holds back the style's own routes on
   its events, which are listed on the row as "style default" responses and edited with the route calls. */

#define GEODE_RESP_ROWS 16
#define GEODE_RESP_KINDS 16 /* a response's kind: 0 moves a parameter, 1 Things, 2 Fronts, 3 Fields, 4 Clock control (their kind-params are P0..P5, see viz/things/ThingResponse.hpp and its siblings) */

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
#define GEODE_RESP_LIST_VALUES 24

/* A listed response: the fields, then origin, route index, and (style defaults only, else -1) the route's source
   and target ordinals, which are what GEODE_RESP_EDIT_STYLE_MUTE takes. A style default's ENABLED is its own
   switch: 0 once the user has muted that route. */
/* geode_resp_rows, per row: on, the user's responses, the style's default responses, and how many of those
   are muted. A row's switch is for its responses alone: the style's own routes play whatever the rows say. */
#define GEODE_RESP_ROW_VALUES 4

/* geode_resp_edit's `op`. */
#define GEODE_RESP_EDIT_ROW_ON 0 /* row, value 0 or 1 */
#define GEODE_RESP_EDIT_ADD 1    /* row, value = the target ordinal a new parameter response moves */
#define GEODE_RESP_EDIT_REMOVE 2 /* row, index = the nth user response of the row */
#define GEODE_RESP_EDIT_SET 3    /* row, index, field (GEODE_RESP_*), value */
#define GEODE_RESP_EDIT_CLEAR 4  /* every response removed, every row off and every style route back on */
#define GEODE_RESP_EDIT_STYLE_MUTE 5 /* row = the route's source ordinal, index = its target ordinal, value 1 mutes, 0 unmutes */

/* geode_viz_responder_status: the rows' event lights (0..1, easing out after the event), then these. */
#define GEODE_RESP_STATUS_COST GEODE_RESP_ROWS /* this frame's work, in route evaluations */
#define GEODE_RESP_STATUS_BUDGET (GEODE_RESP_ROWS + 1)
#define GEODE_RESP_STATUS_LEVEL (GEODE_RESP_ROWS + 2) /* 0 light, 1 heavy, 2 over the budget */
#define GEODE_RESP_STATUS_ACTIVE (GEODE_RESP_ROWS + 3)
#define GEODE_RESP_STATUS_DROPPED (GEODE_RESP_ROWS + 4) /* responses the matrix had no free slot for */
#define GEODE_RESP_STATUS_ROUTES (GEODE_RESP_ROWS + 5)  /* routes of the matrix that play */
#define GEODE_RESP_STATUS_VALUES (GEODE_RESP_ROWS + 6)

/* Downgrading: a build from before the Event Responder reads a list that carries `routes|auto` (Auto routes with
   responses) as a list of zero routes of the user's, so the style's own routes stop playing there. There is no
   migration; the line is what keeps this build from reading such a list as the user's. */

/* The saved text of a new style: every row off, nothing muted; that is no text at all, so the style's own routes play. */
GEODE_API int geode_resp_default_text(char* out, int capacity);
/* A row's saved name (KICK, SNARE, … KEY_CHANGE, COLLIDE), static; NULL for a row there is none of. */
GEODE_API const char* geode_resp_row_name(int row);

/* Every row's GEODE_RESP_ROW_VALUES, in row order, for the text (NULL: a new style's), the style and the hand mask. */
GEODE_API int geode_resp_rows(const char* reactions, const char* style_id, int seed, unsigned mask, float* out, int capacity);
/* One row's responses, GEODE_RESP_LIST_VALUES each: the user's in order, then the style's own routes on its events. */
GEODE_API int geode_resp_values(const char* reactions, const char* style_id, int seed, unsigned mask, int row, float* out,
                                int capacity);
/* One edit (GEODE_RESP_EDIT_*). The new reactions text, routes and all; an edit that names nothing hands back what
   came in. NULL reactions edits a new style's. */
GEODE_API int geode_resp_edit(const char* reactions, int op, int row, int index, int field, float value, char* out, int capacity);

/* The Song events page's catalogue (core/viz/modulation/ResponseCatalog): the families and tiles of "+ Add response",
   each kind's own controls with their words, units and ranges, the envelope presets, the strength curves, the drive
   modes and the rows' names, as the lines ResponseCatalog.hpp lists. Pure, any thread. */
GEODE_API int geode_resp_catalog_text(char* out, int capacity);

/* geode_resp_catalog_edit's `op`. */
#define GEODE_RESP_CATALOG_ADD 0    /* row, value = the tile's ordinal (an `entry` line); the row comes on */
#define GEODE_RESP_CATALOG_PRESET 1 /* row, index = the nth user response of the row, value = the preset's ordinal */
#define GEODE_RESP_CATALOG_RESET_ROW 2 /* row: its user responses removed and the row off again and its style chips unmuted (the page offers Undo with the text it had) */
/* One of those edits, as geode_resp_edit: the new reactions text, routes and all (NULL reactions: a new style's). */
GEODE_API int geode_resp_catalog_edit(const char* reactions, int op, int row, int index, float value, char* out, int capacity);

/* A response's field as words (`values` are its GEODE_RESP_FIELDS fields, `field` a GEODE_RESP_*). A parameter response's
   amount reads as a percentage of the style's own value on `look` (the params wire; NULL: the defaults). */
GEODE_API int geode_resp_field_text(const float* values, int count, int field, const float* look, char* out, int capacity);
/* What a response's chip says: the parameter it moves, or the kind and what it draws or does. */
GEODE_API int geode_resp_title(const float* values, int count, char* out, int capacity);
/* The envelope preset (an ordinal of the catalogue's `preset` lines) a response's fade in, hold and fade out make; -1 for none. */
GEODE_API int geode_resp_preset_of(const float* values, int count);

/* Plays a response once now on the renderer, as its event would (the picker's live preview): `values` are its
   GEODE_RESP_FIELDS fields. Any thread; latched for the next frame. */
GEODE_API void geode_viz_response_preview(geode_viz*, const float* values, int count);
/* The rows' event lights and the cost budget as the last frame left them: GEODE_RESP_STATUS_VALUES values. */
GEODE_API int geode_viz_responder_status(geode_viz*, float* out, int capacity);

#ifdef __cplusplus
}
#endif

#endif
