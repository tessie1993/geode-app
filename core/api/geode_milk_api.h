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

#ifdef __cplusplus
}
#endif

#endif
