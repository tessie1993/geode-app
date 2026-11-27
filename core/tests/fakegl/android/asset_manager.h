/* Host-test stand-in for <android/asset_manager.h>: the APK's asset store, the OS boundary that
 * core/viz/glsl/ShaderSource.cpp reads shaders through. Declarations only; each fakegl test that reads shaders
 * (core/tests/transition_programs_test.cpp, core/tests/style_reads_test.cpp) defines AAssetManager, AAsset and these functions.
 * Same declarations as tools/hostlink's stub. */
#pragma once
#include <sys/types.h>

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif
struct AAssetManager;
struct AAsset;
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
#define AASSET_MODE_BUFFER 3
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);
off_t AAsset_getLength(AAsset* asset);
int AAsset_read(AAsset* asset, void* buf, size_t count);
void AAsset_close(AAsset* asset);
#ifdef __cplusplus
}
#endif
