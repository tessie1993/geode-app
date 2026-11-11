#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>

namespace geode::viz::frame {

// Record's picture (Visuals › Record): the stage's finished composite, copied into a hardware encoder's
// input surface. The renderer calls it between the composite and the UI blur, so the file holds the visual
// alone: none of the app's glass, sheet or chrome, which are drawn after it or by Compose.
//
// The copy is one scaled blit with the encoder's surface as the draw surface and the stage's as the read
// surface, on the stage's own context, so no frame is read back and no second context is made. The swap can
// wait while the encoder's queue is full; core/output/Recorder keeps frames out of here while the encoder is
// behind, so the stage never stalls on it. Which frames are copied and their PTS are that clock's.
//
// GL thread only, apart from the destructor's guard: the surface is destroyed with or without a context.
class RecordTap {
public:
    RecordTap() = default;
    ~RecordTap() { stop(); }
    RecordTap(const RecordTap&) = delete;
    RecordTap& operator=(const RecordTap&) = delete;

    // Opens an EGL surface on the encoder's window (an ANativeWindow) for a width x height frame. False when
    // no context is current or the driver will not draw the stage's context into it.
    bool start(void* nativeWindow, int width, int height);
    void stop();
    bool running() const { return surface_ != EGL_NO_SURFACE; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Makes the encoder's surface the draw surface and copies sourceFbo (0 = the stage's surface), scaled.
    // The caller may then draw over the frame (the watermark) before present(). False when the driver
    // refused; the stage's surfaces are current again then.
    bool begin(GLuint sourceFbo, int sourceWidth, int sourceHeight);

    // Stamps the frame and hands it to the encoder, then puts the stage's surfaces and sourceFbo back.
    bool present(int64_t ptsNs, GLuint sourceFbo);

private:
    void restore(GLuint sourceFbo);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLSurface stageDraw_ = EGL_NO_SURFACE;
    EGLSurface stageRead_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    int width_ = 0;
    int height_ = 0;
    GLint viewport_[4] = {0, 0, 0, 0};
    GLboolean scissor_ = GL_FALSE;
};

}  // namespace geode::viz::frame
