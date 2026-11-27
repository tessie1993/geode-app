#pragma once
#include <cstdint>

// What a stage draws with. The stage owns the thread, the EGL context and the vsync,
// and calls these on its own thread only, in the order core/stage/StageLifecycle asks for them; the renderer (a
// geode_viz) is bound to it by the code that creates the stage. Pure, so the lifecycle's host test can name it.
namespace geode::stage {

// A bit of frame()'s answer that is the stage's own, never the shell's: the frame drew nothing because it would have
// repeated the one on screen, so the stage skips the swap. Mirrors viz::frame::kEventNothingNew.
inline constexpr uint32_t kFrameNothingNew = 1u << 7;

class StageClient {
public:
    virtual ~StageClient() = default;
    // A new context is current: build GL objects. Once per context; a lost context is followed by another.
    virtual void surfaceCreated() = 0;
    // The window's size in pixels, after every new surface and every size change, context current.
    virtual void surfaceChanged(int width, int height) = 0;
    // One frame, context current: paced is the rate frames are paced at (1 / the frame's length, 0 not known), cap
    // the surface's own, a hint only. Returns event bits the shell polls (geode_stage_take_events).
    virtual uint32_t frame(float pacedFps, float capFps) = 0;
    // Called just before each frame: when the display is expected to show it (CLOCK_MONOTONIC ns, from the pacer,
    // core/stage/SwapPace), 0 for a frame no pacer asked for. The picture steps by these times, and the sound is read
    // where the viewer sees the frame.
    virtual void framePresent(int64_t presentNs) { static_cast<void>(presentNs); }
    // The next frame steps by 0: the time since the last one did not pass on screen (the pacing starts again, or a
    // frame no pacer asked for follows). Called on the stage thread, context current or not.
    virtual void restartClock() {}
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
