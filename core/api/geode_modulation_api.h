#ifndef GEODE_MODULATION_API_H
#define GEODE_MODULATION_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The user's modulation (core/viz/ModDocument): the three waves, the two envelopes and the reaction routes set
   by hand. The shell holds each as the text it saves and nothing else: it reads a value to draw through the
   *_values calls, makes an edit through the *_edit calls (which hand the new text back) and gives a renderer
   the texts as they are. Every default, range, clamp and "what may this move" is decided here.

   A text out: `out` gets as much as `capacity` holds, NUL-terminated, and the return is the length the whole
   text needs. A value list out: at most `capacity` values, and the return is how many were written.
   Every call but the three geode_viz_set_* is pure: no handle, the same arguments give the same result. */

/* A wave's fields, in geode_mod_lfo_values' order and as geode_mod_lfo_edit's `field`. Enums travel as their
   ordinal in the catalog (geode_mod_catalog_text); beats 0 is free timing, knob 0 no knob, moment -1 none. */
#define GEODE_MOD_LFO_ENABLED 0
#define GEODE_MOD_LFO_SOURCE 1
#define GEODE_MOD_LFO_TARGET 2
#define GEODE_MOD_LFO_WAVE 3
#define GEODE_MOD_LFO_RATE_SECONDS 4
#define GEODE_MOD_LFO_DEPTH 5
#define GEODE_MOD_LFO_POLARITY 6
#define GEODE_MOD_LFO_CURVE 7
#define GEODE_MOD_LFO_BEATS 8
#define GEODE_MOD_LFO_KNOB 9
#define GEODE_MOD_LFO_MOMENT 10
/* The fields, then 1 while the target moves something on the style asked about. */
#define GEODE_MOD_LFO_VALUES 12

/* An envelope's fields, likewise. TARGET is edit-only: its value is a target ordinal, added when the envelope
   does not move it yet and taken away when it does. */
#define GEODE_MOD_ADSR_ENABLED 0
#define GEODE_MOD_ADSR_ATTACK 1
#define GEODE_MOD_ADSR_DECAY 2
#define GEODE_MOD_ADSR_SUSTAIN 3
#define GEODE_MOD_ADSR_RELEASE 4
#define GEODE_MOD_ADSR_AMOUNT 5
#define GEODE_MOD_ADSR_BAND 6
#define GEODE_MOD_ADSR_GATE_THRESHOLD 7
#define GEODE_MOD_ADSR_SUSTAIN_TRACK 8
#define GEODE_MOD_ADSR_RETRIGGER 9
#define GEODE_MOD_ADSR_KNOB 10
#define GEODE_MOD_ADSR_MOMENT 11
#define GEODE_MOD_ADSR_TARGET 12
/* The fields before TARGET; the ordinals of the targets the envelope moves follow them. */
#define GEODE_MOD_ADSR_VALUES 12

/* A route's fields, in geode_mod_routes_playing's order and as GEODE_MOD_EDIT_ROUTE_SET's `b`. */
#define GEODE_MOD_ROUTE_ENABLED 0
#define GEODE_MOD_ROUTE_SOURCE 1
#define GEODE_MOD_ROUTE_TARGET 2
#define GEODE_MOD_ROUTE_MODE 3
#define GEODE_MOD_ROUTE_AMOUNT 4
#define GEODE_MOD_ROUTE_CURVE 5
#define GEODE_MOD_ROUTE_ATTACK_SECONDS 6
#define GEODE_MOD_ROUTE_RELEASE_SECONDS 7
/* The fields, then 1 while the target moves something on the style asked about. */
#define GEODE_MOD_ROUTE_VALUES 9

/* geode_mod_reactions_edit's `op`. The route and recipe edits act on what plays and make that whole list the
   user's; the music edits change one moment and leave every other with whoever had it. */
#define GEODE_MOD_EDIT_ROUTE_SET 0      /* a = route, b = GEODE_MOD_ROUTE_*, value */
#define GEODE_MOD_EDIT_ROUTE_ADD 1      /* one more route, while the matrix has room */
#define GEODE_MOD_EDIT_ROUTE_REMOVE 2   /* a = route */
#define GEODE_MOD_EDIT_RECIPE_TOGGLE 3  /* a = recipe ordinal */
#define GEODE_MOD_EDIT_MUSIC_TOGGLE 4   /* a = moment, b = music action */
#define GEODE_MOD_EDIT_MUSIC_STRENGTH 5 /* a = moment, value */
#define GEODE_MOD_EDIT_MUSIC_RESET 6    /* a = moment, back to Auto */
#define GEODE_MOD_EDIT_CLEAR 7          /* every route back to Auto */

/* geode_mod_recipe_state's bits. */
#define GEODE_MOD_RECIPE_ON 1u      /* every route it would add to the style plays */
#define GEODE_MOD_RECIPE_FITS 2u    /* turning it on stays within the matrix */
#define GEODE_MOD_RECIPE_OFFERED 4u /* it has a route the style can show */

/* geode_mod_music_state per moment: mine, route count, strength, on-action bits, fits-action bits. */
#define GEODE_MOD_MUSIC_VALUES 5

/* geode_mod_targets_offered's `picker`: a wave slot (0 .. GEODE_LFO_SLOTS - 1) or the route editor. */
#define GEODE_MOD_PICKER_REACTION (-2)

/* What the editors show (core/viz/ModCatalog): every wave shape, source, direction, curve, band, reaction
   source and mode, target and recipe with its saved name and its words, the slider ranges and the slot counts,
   as the line table ModCatalog.hpp documents. Static: the pointer stays valid for the process. */
GEODE_API const char* geode_mod_catalog_text(void);

/* A saved text as the document reads it, written back in full: every slot, every field, each inside its
   range. NULL or text that is no list gives the defaults. */
GEODE_API int geode_mod_lfos_read(const char* text, char* out, int capacity);
GEODE_API int geode_mod_adsrs_read(const char* text, char* out, int capacity);
/* The user's routes likewise; 0 (and an empty out) for NULL or text that is no route list: Auto. */
GEODE_API int geode_mod_reactions_read(const char* text, char* out, int capacity);

/* One wave slot's GEODE_MOD_LFO_VALUES. */
GEODE_API int geode_mod_lfo_values(const char* lfos, int slot, const char* style_id, float* out, int capacity);
/* The waves with one field of one slot set. An unknown slot or field, or a value the field cannot hold,
   changes nothing; a number lands inside its range; a new source also takes the direction it is useful in. */
GEODE_API int geode_mod_lfo_edit(const char* lfos, int slot, int field, float value, char* out, int capacity);
/* One envelope's GEODE_MOD_ADSR_VALUES and then its targets. */
GEODE_API int geode_mod_adsr_values(const char* adsrs, int slot, float* out, int capacity);
GEODE_API int geode_mod_adsr_edit(const char* adsrs, int slot, int field, float value, char* out, int capacity);

/* The target ordinals a picker lists on a style, in catalog order: a wave slot's start with 0 (nothing), the
   route editor's never hold it. */
GEODE_API int geode_mod_targets_offered(const char* style_id, int picker, int* out, int capacity);
/* The envelope's: every target the style shows, and the ones it already moves so they can be taken away. */
GEODE_API int geode_mod_adsr_targets_offered(const char* adsrs, int slot, const char* style_id, int* out, int capacity);

/* The reactions. `reactions` is the user's saved routes, NULL (or text that is no list) while Auto; `mask` the
   hand mask saved beside them (GEODE_HAND_*), which counts as 0 while Auto. style_id and seed (the params'
   reactSeed) deal the style's own routes. */
/* What plays (the user's routes on the hand's sources, the style's on the rest): GEODE_MOD_ROUTE_VALUES each. */
GEODE_API int geode_mod_routes_playing(const char* style_id, int seed, const char* reactions, unsigned mask, float* out,
                                       int capacity);
/* One edit (GEODE_MOD_EDIT_*). Writes the user's new routes and, into out_mask, the mask to save beside them;
   returns 0 with an empty out and mask 0 when every route is Auto again. An edit that names nothing (an
   unknown op, route, field, moment or recipe) hands back what came in. */
GEODE_API int geode_mod_reactions_edit(const char* style_id, int seed, const char* reactions, unsigned mask, int op, int a,
                                       int b, float value, char* out, int capacity, unsigned* out_mask);
/* Every moment's row, GEODE_MOD_MUSIC_VALUES each, in moment order. */
GEODE_API int geode_mod_music_state(const char* style_id, int seed, const char* reactions, unsigned mask, float* out,
                                    int capacity);
/* GEODE_MOD_RECIPE_* for one recipe over what plays; 0 for an unknown recipe or a NULL style. */
GEODE_API unsigned geode_mod_recipe_state(int recipe, const char* style_id, int seed, const char* reactions, unsigned mask);

/* Randomize for the waves (core/viz/WaveRandomizer): `lfos` rolled, on the look in `params` (the
   geode_viz_set_params wire). running_only 0 is the wave editor's button: every slot switched on and rolled.
   1 is a roll of everything: only the steady waves already on, and none when `locked` (newline-joined keys)
   keeps "@waves" or "#EXPERT". Returns 0 with an empty out when nothing changed. */
GEODE_API int geode_mod_waves_randomize(const char* style_id, const char* locked, uint64_t seed, int running_only,
                                        const float* params, const char* lfos, char* out, int capacity);

/* A renderer's modulation, as the saved texts. May come from any thread; latched for the next frame.
   geode_viz_set_reactions takes NULL for Auto. What the matrix plays is resolved natively
   (core/viz/ReactionResolve) from the scene playing, the params' reactSeed and these routes under the hand
   mask the renderer reads itself (geode_hand_set, or the one an export pinned), and worked out again before
   a render only when one of them changed: the shell sends the routes only when they change, and a new hand
   mask reaches every renderer without the shell. */
GEODE_API void geode_viz_set_lfos(geode_viz*, const char* lfos);
GEODE_API void geode_viz_set_adsrs(geode_viz*, const char* adsrs);
GEODE_API void geode_viz_set_reactions(geode_viz*, const char* reactions);

/* The hand mask to push when the saved routes load. reactions is the saved route text (NULL: none saved);
   has_mask says a mask was saved beside it, saved_mask is that mask. Routes saved before the mask existed
   (has_mask 0) were a whole list of the user's: GEODE_HAND_ALL. No routes: 0. Returns -1 when the text is
   saved but is no route list any more: the shell drops the saved routes and mask, and the mask is 0. */
GEODE_API int geode_mod_hand_load(const char* reactions, int has_mask, int saved_mask);

#ifdef __cplusplus
}
#endif

#endif
