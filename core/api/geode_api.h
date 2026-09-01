#pragma once
#include <stddef.h>
#include <stdint.h>
#include <android/asset_manager.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GEODE_API __attribute__((visibility("default")))

typedef struct geode_analysis geode_analysis;
typedef struct geode_viz      geode_viz;
typedef struct geode_dsp      geode_dsp;
typedef struct geode_player   geode_player;

GEODE_API const char* geode_version(void);

#ifdef __cplusplus
}
#endif
