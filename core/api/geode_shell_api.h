#ifndef GEODE_SHELL_API_H
#define GEODE_SHELL_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The shell's rules (core/shell): what the screens and stores ask, answered by native. Pure: every call is
   host-tested (core/tests/shell_*_test.cpp) and safe on any thread. */

/* The frame rates an export offers, ascending (shell/ExportFps), written into out up to capacity; returns how
   many there are. */
GEODE_API int geode_export_fps_options(int* out, int capacity);
/* A stored frame rate kept when it is offered, else the fallback. */
GEODE_API int geode_export_fps_read(int stored, int fallback);

/* The media notification's extra buttons and the start screen (shell/Behaviour). A button is its ordinal
   (0 shuffle, 1 repeat, 2 favourite) and a set of them the mask of 1 << ordinal; a screen is its ordinal
   (0 last used, 1 player, 2 library, 3 visuals, 4 studio). */
/* The chosen buttons in the order the session offers them, written into out up to capacity; returns how many. */
GEODE_API int      geode_notification_buttons_ordered(unsigned chosen, int* out, int capacity);
/* The chosen set with the button switched on or off; switching one on past the room drops the last other one. */
GEODE_API unsigned geode_notification_buttons_toggled(unsigned chosen, int button, int on);
/* The last screen to keep: "last used" as a last screen reads as the player. */
GEODE_API int      geode_behaviour_last_screen(int last);
/* The screen to open on now: the chosen one, or the last screen for "last used". */
GEODE_API int      geode_behaviour_opens_on(int start, int last);

/* The Styles page's own shelf (shell/StyleShelf): favourites in the order they were starred, recents newest
   first. A list comes in as count ids (a NULL id reads as empty) and goes out as text-out: the caller's buffer
   gets as much as fits, NUL-terminated, and the return is the length the whole text needs; each id is followed
   by a newline. */
/* The favourites with id starred if it was not, unstarred if it was. */
GEODE_API int geode_style_shelf_toggle_favourite(const char* const* favourites, int count, const char* id, char* out,
                                                 int capacity);
/* The recents with id in front, no repeats, at most the recent limit. */
GEODE_API int geode_style_shelf_push_recent(const char* const* recents, int count, const char* id, char* out, int capacity);
/* The recents without id (its first place only). */
GEODE_API int geode_style_shelf_remove_recent(const char* const* recents, int count, const char* id, char* out, int capacity);
/* Your styles: on_screen (NULL for none), then the favourites in order's order, then the recents, each of order's
   ids once and nothing order lacks. */
GEODE_API int geode_style_shelf_yours(const char* const* favourites, int favourite_count, const char* const* recents,
                                      int recent_count, const char* const* order, int order_count, const char* on_screen,
                                      char* out, int capacity);

/* The onboarding tour's rules (shell/Tour). Chapters, spots, tips and places cross as their ordinals (the order of
   shell/Tour.hpp); the controls laid out now (shown) as the mask of 1 << spot; what covers the app (cover) as the
   mask of the GEODE_TOUR_COVER_* bits. A run crosses as ints: at, seen, replay (0 or 1), then its chapters.
   Progress crosses as ints: off (0 or 1), the resume spot (-1 for none), how many chapters are done, those
   chapters in the order they were added, then its gesture tips in the order they were said. A call that writes a
   run or progress writes into out up to capacity and returns the ints it needs. */
#define GEODE_TOUR_COVER_FIRST_RUN 1
#define GEODE_TOUR_COVER_PAYWALL 2
#define GEODE_TOUR_COVER_PIP 4
#define GEODE_TOUR_COVER_PERMISSION_RATIONALE 8
#define GEODE_TOUR_COVER_AWAY 16
#define GEODE_TOUR_RUN_HEAD 3      /* ints before a run's chapters */
#define GEODE_TOUR_PROGRESS_HEAD 3 /* ints before a progress's chapters */
/* Which of a progress's three prefs strings: */
#define GEODE_TOUR_TEXT_DONE 0
#define GEODE_TOUR_TEXT_RESUME 1
#define GEODE_TOUR_TEXT_TIPS 2
/* Whether the user has already done what the spot asks (a pointer never is), 0 or 1. */
GEODE_API int geode_tour_reached(int spot, unsigned shown, int place, int playing);
/* The step the run is on, as spot, index, count into out3; 0 when the chapter has nothing left to show. */
GEODE_API int geode_tour_step(const int* run, int run_len, unsigned shown, int place, int playing, int* out3);
/* The run with the spot behind it. */
GEODE_API int geode_tour_pass(const int* run, int run_len, int spot, int* out, int capacity);
/* The run on its next chapter; -1 when that was the last. */
GEODE_API int geode_tour_next_chapter(const int* run, int run_len, int* out, int capacity);
/* Whether anything over the app holds the tour back. */
GEODE_API int geode_tour_held(unsigned cover);
/* Whether the run (NULL, run_len 0: none) may show a step now. */
GEODE_API int geode_tour_shows(const int* run, int run_len, const int* progress, int progress_len, unsigned cover);
/* Whether the tip may be said now. */
GEODE_API int geode_tour_may_tip(int tip, const int* progress, int progress_len, unsigned cover, const int* run, int run_len);
/* The chapter that should start by itself, or -1. */
GEODE_API int geode_tour_due(const int* progress, int progress_len, unsigned shown, int place, int playing);
/* A run of the chapter picking up where the progress left it. */
GEODE_API int geode_tour_resume(int chapter, const int* progress, int progress_len, int* out, int capacity);
/* A replay of the chapters in the mask, in tour order; -1 for none. */
GEODE_API int geode_tour_replay(unsigned chapters, int* out, int capacity);
/* The progress once the run's chapter has ended. */
GEODE_API int geode_tour_ended(const int* progress, int progress_len, const int* run, int run_len, int skipped, int* out,
                               int capacity);
/* Whether the one-time Pro offer follows this ending. */
GEODE_API int geode_tour_offers_pro(const int* run, int run_len, int skipped);
/* The progress the prefs' three strings spell (NULL: none stored; legacy_seen the old "tour ended" flag). */
GEODE_API int geode_tour_decode(const char* done, const char* resume, const char* tips, int legacy_seen, int off, int* out,
                                int capacity);
/* One of the progress's prefs strings (GEODE_TOUR_TEXT_*) as text-out; -1 for no resume. */
GEODE_API int geode_tour_encode(const int* progress, int progress_len, int which, char* out, int capacity);
/* Where the step's card goes: hole is left, top, right, bottom of the lit control (NULL for none), safe the same of
   the safe area; writes the card's left and top into out2. */
GEODE_API void geode_tour_place_card(const int* hole, int card_width, int card_height, const int* safe, int gap, int* out2);

/* The Presets list (shell/PresetList). A filter chip is its kind: */
#define GEODE_PRESET_FILTER_ALL 0
#define GEODE_PRESET_FILTER_THIS_STYLE 1
#define GEODE_PRESET_FILTER_MINE 2
#define GEODE_PRESET_FILTER_BUILT_IN 3
#define GEODE_PRESET_FILTER_FOLDER 4
/* A row's marks, or-ed into its flags: */
#define GEODE_PRESET_BUILT_IN 1
#define GEODE_PRESET_FOR_THIS_STYLE 2
#define GEODE_PRESET_ON_STAGE 4
/* The chips in order, the four shelves and then the user's folders (blank ones dropped, each once), as kind and
   folder pairs into out (room for capacity pairs, 2 * capacity ints): the folder is the index into folders of
   the chip's name, -1 for a shelf. Returns how many chips there are. */
GEODE_API int geode_preset_filters(const char* const* folders, int count, int* out, int capacity);
/* The rows the filter (kind, and filter_folder's name for a folder chip) leaves, as indices into the count rows
   (each a name, a folder and its flags), into out (room for count); returns how many: the user's own first,
   then the built-ins, those made for the style on stage ahead of the rest; a name shows once. */
GEODE_API int geode_preset_apply(const char* const* names, const char* const* folders, const int* flags, int count,
                                 int filter_kind, const char* filter_folder, int* out);
/* The one row that gets the check, or -1: the last tapped (NULL for none) while it is on stage, else the first
   row on stage. */
GEODE_API int geode_preset_applied(const char* const* names, const int* flags, int count, const char* last_tapped);

/* An audio file another app opens in this one, "Open with" (shell/OpenedAudio). Every text is a value the intent
   or the provider handed over, UTF-8 (a lone surrogate may come as its own three bytes); NULL is absent. */
#define GEODE_OPENED_NOT_OURS 0 /* not an opened audio file: left for the other handlers */
#define GEODE_OPENED_STALE 1    /* delivered again from Recents, after its grant may have ended: dropped */
#define GEODE_OPENED_OPEN 2     /* an opened audio file to play */
/* Routes a delivered intent (GEODE_OPENED_*) from its action, its data uri's scheme and the
   type the sender declared. */
GEODE_API int geode_opened_audio_route(const char* action, const char* scheme, const char* type, int from_history);
/* 1 when the app must refuse to play the uri: provider_package owns the uri's authority (NULL when no provider
   answers to it), read_granted says this app holds an explicit read grant on it. */
GEODE_API int geode_opened_audio_refuses(const char* scheme, const char* authority, const char* own_package,
                                         const char* provider_package, int read_granted);
/* The provider's display name made fit to show, as text-out (the caller's buffer gets as much as fits,
   NUL-terminated; the return is the length the whole text needs), or -1 when nothing is left. */
GEODE_API int geode_opened_audio_display_name(const char* raw, char* out, int capacity);
/* A queue id for an opened file from 16 random bytes the caller drew, as text-out. */
GEODE_API int geode_opened_media_id(const unsigned char* random16, char* out, int capacity);
/* 1 when a queue id marks an opened file (NULL: no id). */
GEODE_API int geode_opened_audio_is_opened(const char* media_id);

/* Auto's remembered shape as the prefs keep it (shell/AutoVisuals). The visual playlist crosses as
   GEODE_AUTO_ENTRY_FIELDS texts per entry: scene id, preset name, label; a NULL preset name
   is none. The scalar settings cross as GEODE_AUTO_SAVED_FIELDS ints in this order: */
#define GEODE_AUTO_ENTRY_FIELDS 3
#define GEODE_AUTO_SAVED_RANDOM_ON 0       /* the timer (never stored: it always reads as off) */
#define GEODE_AUTO_SAVED_RANDOM_INTERVAL 1 /* s */
#define GEODE_AUTO_SAVED_RANDOM_STYLES 2
#define GEODE_AUTO_SAVED_RANDOM_PRESETS 3
#define GEODE_AUTO_SAVED_PLAYLIST_ON 4
#define GEODE_AUTO_SAVED_PLAYLIST_INTERVAL 5 /* s */
#define GEODE_AUTO_SAVED_FIELDS 6
typedef struct geode_auto_playlist geode_auto_playlist;
/* The entries a saved playlist text holds, read as org.json reads it, each scene id put through resolve (the
   catalogue's, geode_catalogue_resolve; NULL keeps it). NULL json is no entries. Never NULL. */
GEODE_API geode_auto_playlist* geode_auto_playlist_read(const char* json, const char* (*resolve)(const char*));
GEODE_API int                  geode_auto_playlist_count(const geode_auto_playlist*);
/* Field (0 .. GEODE_AUTO_ENTRY_FIELDS - 1) of entry i; NULL for a preset name that is none, or out of
   range. Valid until the playlist is destroyed. */
GEODE_API const char*          geode_auto_playlist_field(const geode_auto_playlist*, int i, int field);
GEODE_API void                 geode_auto_playlist_destroy(geode_auto_playlist*);
/* The playlist as the prefs keep it, a JSON array as org.json wrote it, from count entries' fields
   (GEODE_AUTO_ENTRY_FIELDS * count texts), as text-out. */
GEODE_API int geode_auto_playlist_to_json(const char* const* fields, int count, char* out, int capacity);
/* The saved settings read over the state: state the GEODE_AUTO_SAVED_FIELDS now, stored what the prefs hold where
   has is 1, playlist_count how many entries the playlist has once read. Writes the fields read into out: the
   timer off, an absent value the state's, the playlist on only when it is stored on (or was on) and has entries,
   both intervals held to native's range. */
GEODE_API void geode_auto_visuals_apply(const int* state, const int* stored, const unsigned char* has, int playlist_count,
                                        int* out);

#ifdef __cplusplus
}
#endif

#endif
