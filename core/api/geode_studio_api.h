#ifndef GEODE_STUDIO_API_H
#define GEODE_STUDIO_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Studio project's documents (core/studio/StudioEdit): its kind and its edit, as the JSON project.json keeps
   under "kind" and "edit". The shell stores the text native hands back as it is and asks native again for every
   rule. Each call returns a text the caller destroys, NULL when the input is no such document. Pure. */
typedef struct geode_studio_text geode_studio_text;

/* The edit read from json, in canonical form; NULL json is the empty edit. */
GEODE_API geode_studio_text* geode_studio_edit_read(const char* json);
/* The kind read from json, in canonical form; NULL json is the standard kind every new project starts as. */
GEODE_API geode_studio_text* geode_studio_kind_read(const char* json);
/* A new project's edit: song_uri NULL for none (a recording of the mic or another app), song_ms 0 when its
   length is not known yet; scene_id NULL for no look, milk_path NULL unless the look is a MilkDrop preset.
   The look runs over the song or the render (render_ms), whichever is longer. Ids are fresh random UUIDs. */
GEODE_API geode_studio_text* geode_studio_edit_new(const char* song_uri, const char* song_title, long long song_ms,
                                                   const char* scene_id, const char* milk_path, long long render_ms);
GEODE_API const char*        geode_studio_text_get(const geode_studio_text*);
GEODE_API void               geode_studio_text_destroy(geode_studio_text*);

/* The shortest clip native makes: a render range shorter than this has no stretch of song to draw over. */
GEODE_API long long          geode_studio_min_clip_ms(void);

#ifdef __cplusplus
}
#endif

#endif
