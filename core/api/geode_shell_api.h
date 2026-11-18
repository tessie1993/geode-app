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

#ifdef __cplusplus
}
#endif

#endif
