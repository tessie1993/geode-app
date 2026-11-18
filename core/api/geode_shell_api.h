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

#ifdef __cplusplus
}
#endif

#endif
