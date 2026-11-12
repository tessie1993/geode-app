#pragma once
#include <cstdint>

// What a stage draws with (docs/agents/native-stage.md). The stage owns the thread, the EGL context and the vsync,
// and calls these on its own thread only, in the order core/stage/StageLifecycle asks for them; the renderer (a
// geode_viz) is bound to it by the code that creates the stage. Pure, so the lifecycle's host test can name it.
namespace geode::stage {

class StageClient {
public:
    virtual ~StageClient() = default;
    // A new context is current: build GL objects. Once per context; a lost context is followed by another.
    virtual void surfaceCreated() = 0;
    // The window's size in pixels, after every new surface and every size change, context current.
    virtual void surfaceChanged(int width, int height) = 0;
    // One frame, context current: paced is the rate the pacer draws at now (0 every vsync), cap the surface's own.
    // Returns event bits the shell polls (geode_stage_take_events).
    virtual uint32_t frame(float pacedFps, float capFps) = 0;
    // Shed the spare scenes before the surface pauses, context current.
    virtual void trimSpares() = 0;
    // The context is gone with every GL name in it: forget them, delete none. No context is current.
    virtual void contextLost() = 0;
    // Work the client asked the stage thread for (a kStageService command), between frames. ready says a frame
    // could be drawn now: the context is current on a sized window surface.
    virtual void service(bool ready) { static_cast<void>(ready); }
    // The last call. glCurrent says whether a context is current, so GL names may be deleted.
    virtual void release(bool glCurrent) = 0;
};

}  // namespace geode::stage
