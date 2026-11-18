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

#ifdef __cplusplus
}
#endif

#endif
