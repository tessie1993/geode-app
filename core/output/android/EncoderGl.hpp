#pragma once

// An ES 3 context on a recordable RGBA8888 config, current on an encoder's input window, for the offline
// renders (output/android/LoopRenderHost, VideoExportHost). It stamps each frame's presentation time and swaps
// it into the encoder. Android only: kept out of every host-test compile line.

#include <EGL/egl.h>

#include <cstdint>

namespace geode::output {

class EncoderGl {
public:
    EncoderGl() = default;
    ~EncoderGl() { close(); }
    EncoderGl(const EncoderGl&) = delete;
    EncoderGl& operator=(const EncoderGl&) = delete;

    // nullptr once current on `window`, else why not. close() undoes a half-made one.
    const char* open(void* window);
    // The frame's presentation time, then the swap; false when the swap failed.
    bool present(int64_t ptsNs);
    // Idempotent. The display stays initialized: the process's other surfaces share it.
    void close();

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
};

}  // namespace geode::output
