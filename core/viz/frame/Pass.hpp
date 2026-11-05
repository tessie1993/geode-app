#pragma once

#include "sound/SoundFrame.h"

// The frame graph's unit of work (ADR 0006, core/viz/frame/README.md): one step of the frame every surface
// runs, after the scene and before the swap. Trail, Liquid, Composite, NoteBloom, UiBlur and the export
// post-passes are to implement it; EdgeGlowPass (the wallpaper's edge look, #439) is the first.
//
// Why this split of calls: prepare() is CPU only, so a pass's maths (envelopes, what it reads off the sound
// frame, the geometry it draws) runs and is tested on the host without a GL context (core/tests); draw()
// only turns what prepare() decided into GL calls on the GL thread. wants() lets the graph skip a pass that
// has nothing to do this frame, on this surface, without touching either. release() exists because a new
// EGL context reuses names: a pass must forget the old context's programs and textures before anything in
// the new one is generated, or it would later delete or draw with an object that is no longer its own.
namespace geode::viz::frame {

// Which kind of surface the frame is for. A pass that belongs to one (the wallpaper's edge glow, the
// export's watermark) says so in wants().
enum class Surface : int { Live, Wallpaper, Dream, Export };

// What a pass may read about this frame. `sound` is the frame's sound engine output: never null while the
// renderer runs, but a pass treats null as silence so a host test can drive it without one.
struct FrameCtx {
    int width, height;
    double seconds;
    float dt;
    Surface surface;
    const GeodeSoundFrame* sound;
};

// The frame's textures a pass may sample. 0 = not available to this pass (the edge glow reads neither: it
// blends over the target's finished pixels).
struct PassInputs {
    unsigned sceneTex = 0, compositeTex = 0;
};

// Where a pass draws: a framebuffer (0 = the window surface) of width × height pixels.
struct PassTarget {
    unsigned fbo = 0;
    int width = 0, height = 0;
};

class Pass {
public:
    virtual ~Pass() = default;
    virtual bool wants(const FrameCtx&) const = 0;                // false: skipped
    virtual void prepare(const FrameCtx&) = 0;                    // CPU only, no GL
    virtual void draw(const PassInputs&, const PassTarget&) = 0;  // GL thread
    virtual void release() = 0;                                   // drop GL names before a new context gens
};

}  // namespace geode::viz::frame
