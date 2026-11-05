#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <cstdint>

#include "viz/FlashLimiter.hpp"

namespace geode::viz {

// Measures the finished frame for the flash limiter (FlashLimiter.hpp), without ever waiting on the GPU.
//
// Each frame, after the composite and before the UI blur (the glass sheet is not
// the visual, and the export never draws it): the target framebuffer is blitted, linearly filtered, to a
// 256 × 256 texture, its mip chain built, and level 4 (16 × 16, each cell a box mean of 16 × 16 of the
// blit) read into a pixel-pack buffer behind a fence. A later frame takes it once the fence has
// signalled, usually the next one. Three buffers in a ring: if all three are still in flight the frame
// goes unmeasured rather than stalling, and the limiter, which counts in time, not frames, carries on.
//
// Cost: one blit of 64 K texels, a mip build of 87 K, a 1 KB read. The default framebuffer is single
// sampled (GLSurfaceView's default config; nothing sets a chooser), which a scaling blit requires.
//
// GL thread only. A lost context loses the names: forget() drops them, release() deletes them.
class OutputProbe {
public:
    static constexpr int kBlitSide = 256;
    static constexpr int kReadLevel = 4;
    static constexpr int kGrid = safety::kProbeGrid;  // kBlitSide >> kReadLevel
    static constexpr int kSlots = 3;
    static constexpr int kBytes = kGrid * kGrid * 4;

    OutputProbe() = default;
    ~OutputProbe() { release(); }
    OutputProbe(const OutputProbe&) = delete;
    OutputProbe& operator=(const OutputProbe&) = delete;

    // Queues a read of `targetFbo` (width × height), drawn with `grade` at `drawnAt`. Leaves
    // GL_FRAMEBUFFER bound to targetFbo with the full viewport.
    void capture(GLuint targetFbo, int width, int height, const safety::Grade& grade, double drawnAt);
    // The oldest landed read, if any, oldest first; false when none has landed yet.
    bool poll(safety::FrameLight& light, safety::Grade& grade, double& drawnAt);

    void release();
    void forget();

private:
    struct Slot {
        GLuint pbo = 0;
        GLsync fence = nullptr;
        safety::Grade grade;
        double drawnAt = 0.0;
    };

    bool ensure();

    GLuint tex_ = 0;
    GLuint blitFbo_ = 0;
    GLuint readFbo_ = 0;
    bool failed_ = false;
    std::array<Slot, kSlots> slots_{};
    int next_ = 0;     // the slot the next capture writes
    int pending_ = 0;  // captures in flight, oldest at next_ - pending_
    std::array<uint8_t, kBytes> scratch_{};
};

}  // namespace geode::viz
