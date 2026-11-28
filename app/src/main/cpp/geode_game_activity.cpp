#include <game-activity/GameActivity.h>

// The Java host owns the SurfaceView and the retained StageThread owns rendering. This callback only
// satisfies GameActivity's native entry point; it deliberately starts no second native loop.
extern "C" __attribute__((visibility("default"))) void GameActivity_onCreate(GameActivity*, void*, size_t) {}
