#pragma once
#include <cstdint>

// What a stage draws with (docs/agents/native-stage.md). The stage owns the thread, the EGL context and the vsync,
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
    // One frame, context current: paced is the rate the display presents at (read from the vsync's own interval, 0 not known), cap the surface's own, a hint only.
    // Returns event bits the shell polls (geode_stage_take_events).
    virtual uint32_t frame(float pacedFps, float capFps) = 0;
    // Called just before each frame: the vsync's frame time (CLOCK_MONOTONIC ns) the frame is for, 0 when no vsync
    // asked for it. The frame's own time, so a thread hop between the vsync and the draw adds no jitter to its step.
    virtual void frameTime(int64_t vsyncNs) { static_cast<void>(vsyncNs); }
    // Called just before each frame, after frameTime: when the display is expected to show it (CLOCK_MONOTONIC ns),
    // 0 when no time was given. The sound is read where the viewer sees the frame.
    virtual void framePresent(int64_t presentNs) { static_cast<void>(presentNs); }
    // After each frame: the cap "Adapt to frame time" (viz/frame/FrameGovernor) wants the stage paced to instead of
    // its own, 0 for none. The stage hands it to the pacer and the window's hint the way it hands its own cap.
    virtual float governedCapFps() { return 0.0f; }
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
