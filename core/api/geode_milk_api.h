#ifndef GEODE_MILK_API_H
#define GEODE_MILK_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The MilkDrop preset on screen, one memory per process (core/viz/MilkMemory). The shell seeds it with the
   path it saved and saves the path again when loaded or forget says so; every rule is native. Any thread.
   A returned string stays valid on the calling thread until its next call here; NULL means none. */
GEODE_API void        geode_milk_memory_seed(const char* saved);
GEODE_API void        geode_milk_memory_request(const char* path);
GEODE_API int         geode_milk_memory_loaded(const char* path);                     /* 1: save path */
GEODE_API int         geode_milk_memory_forget(const char* const* paths, int count);  /* 1: drop the saved path */
GEODE_API const char* geode_milk_memory_last_shown(void);
/* What a new renderer starts on; with nothing shown or asked for, the starter pack goes into milk_dir and
   its first-run preset is the answer. */
GEODE_API const char* geode_milk_memory_restore_path(AAssetManager* assets, const char* milk_dir);

/* Copies the bundled presets into milk_dir once per pack version (core/viz/MilkStarterPack); returns how
   many files it wrote. */
GEODE_API int         geode_milk_starter_install(AAssetManager* assets, const char* milk_dir);

/* MilkDrop textures (core/viz/MilkTextures): which picked files the texture store takes and their stored names,
   the preset that shows one, and which stored texture answers each sampler a preset names. The shell reads
   documents and writes, deletes and links files. Lists come back as entries each followed by '\n'. */
GEODE_API long long   geode_milk_texture_max_bytes(void);
/* 1: worth reading (a texture extension, declared_bytes within the cap; 0 when the provider did not say). */
GEODE_API int         geode_milk_texture_admits(const char* name, long long declared_bytes);
/* The name to store the picked bytes under; NULL when refused. width/height: the shell's bounds decode, -1 none. */
GEODE_API const char* geode_milk_texture_stored_name(const char* name, const unsigned char* bytes, long long length,
                                                     int width, int height);
/* The stored textures in texture_dir, in the store's order. */
GEODE_API const char* geode_milk_texture_list(const char* texture_dir);
/* The generated presets to delete once texture is gone from texture_dir. */
GEODE_API const char* geode_milk_texture_removed_presets(const char* texture, const char* texture_dir);
GEODE_API const char* geode_milk_texture_show_preset_name(const char* texture);
GEODE_API const char* geode_milk_texture_show_preset_text(const char* texture);
/* The preset at preset_path resolved against texture_dir and the person's choices (lowercased expected name ->
   stored name): "1" or "0" (a stand-in or a missing texture), then per sampler five entries: expected name,
   texture ("" when missing), kind (0 matched, 1 renamed, 2 overridden, 3 substituted, 4 missing) and the two
   link file names to make in the preset's link folder (the second "" when it is the first). */
GEODE_API const char* geode_milk_texture_links(const char* preset_path, const char* texture_dir,
                                               const char* const* override_keys, const char* const* override_values,
                                               int override_count);
/* The presets in milk_dir that keep a link folder, and the link folders no preset keeps. */
GEODE_API const char* geode_milk_texture_linked_presets(const char* milk_dir);
GEODE_API const char* geode_milk_texture_stale_link_dirs(const char* milk_dir);
/* A file from an imported folder: 0 neither, 1 a preset, 2 a texture; and the file name it is written as. */
GEODE_API int         geode_milk_pack_entry_kind(const char* name);
GEODE_API const char* geode_milk_pack_target_name(const char* name);
/* 1: a sampler of the preset at preset_path names nothing in texture_dir. */
GEODE_API int         geode_milk_preset_misses_texture(const char* preset_path, const char* texture_dir);
/* The file name a single imported preset is written as in milk_dir; keep_existing: never one already there. */
GEODE_API const char* geode_milk_import_file_name(const char* milk_dir, const char* display_name, int keep_existing);

#ifdef __cplusplus
}
#endif

#endif
