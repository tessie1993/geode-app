#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Framebuffer.hpp"
#include "viz/Program.hpp"
#include "viz/styles/shader/ShaderSource.hpp"

namespace geode::viz {

// The rect of the live visual under the app's glass sheet, as the shell hands it over
// (geode_viz_set_ui_blur_region). Normalised to the viewport with y DOWN, as Android lays out a view:
// a view's left / top / right / bottom in pixels divided by the visual's width and height. strength
// is 0..1 (0 = off); cornerPx is the sheet's corner radius in surface pixels.
struct UiBlurRegion {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float strength = 0.0f;
    float cornerPx = 0.0f;

    bool active() const { return strength > 0.0f && right > left && bottom > top; }
};

// The UI blur: frosted glass for the Compose sheet that floats over the visual.
//
// Why native: the sheet is Compose, drawn over a separate GLSurfaceView, so a RenderEffect blur in the
// sheet's own window never sees the visual's pixels. The visual blurs the region itself instead, and
// the sheet draws its translucent matte glass over that.
//
// Drawn by Renderer::render after the composite, the frame's last draw, into the
// target framebuffer. Live surfaces only: an offscreen (export) renderer never draws it, so a sheet
// that happened to be open never ends up in a video.
//
// Cost: nothing at all at strength 0 (no pass runs, no texture is touched). Otherwise, for the rect plus
// a margin only: a linear blit of the finished frame to half resolution and another to quarter, two
// rounds of a separable 13-tap Gaussian at quarter resolution (7 fetches a pass), and one scissored
// draw that lays the blur back inside the rounded rect. On a 1080 × 2400 phone with a half-screen sheet
// that is about 80 k quarter-resolution pixels a pass, well under half a millisecond on a mid GPU.
class UiBlurPass {
public:
    // Rounds of horizontal then vertical Gaussian: about 17 surface pixels of sigma with two.
    static constexpr int kIterations = 2;
    // Surface pixels read past the rect on every side, so its edge blurs the visual beside it, not
    // stale texels: the kernel's reach (6 quarter texels) × kIterations × 4, rounded up.
    static constexpr int kMarginPx = 64;

    UiBlurPass() = default;
    ~UiBlurPass() { release(); }
    UiBlurPass(const UiBlurPass&) = delete;
    UiBlurPass& operator=(const UiBlurPass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    void release();
    // The context is gone: drops the programs and targets without a GL call.
    void forget();
    void releaseTargets();
    // Once a frame, on the wall step: frees the targets once `idleSeconds` pass without a draw.
    void releaseIdle(float wallDt, float idleSeconds);
    // After the composite, with `targetFbo` holding the finished frame (or the frame_ beginFrame handed out) of width × height pixels.
    // Before the composite: the framebuffer the frame should be drawn into so the blur never reads the window
    // (a window surface read back after its draw costs a tile flush and reload every frame), or 0 when no blur
    // will run and the frame goes straight to the window. A non-zero answer must be followed by draw().
    GLuint beginFrame(const UiBlurRegion& region, int width, int height);
    void draw(const UiBlurRegion& region, GLuint targetFbo, int width, int height, GLuint quadVao);

private:
    bool drawBlur(const UiBlurRegion& region, GLuint srcFbo, GLuint targetFbo, int width, int height, GLuint quadVao);
    static void copyFrame(GLuint srcFbo, GLuint dstFbo, int width, int height);

    OwnedProgram blurUniforms_;  // the blur program and its locations
    OwnedProgram maskUniforms_;  // the mask program and its locations
    Framebuffer half_{"uiBlurHalf"};
    Framebuffer quarterA_{"uiBlurQuarterA"};
    Framebuffer quarterB_{"uiBlurQuarterB"};
    Framebuffer frame_{"uiBlurFrame"};  // the finished frame while a blur runs, copied to the window by draw
    bool framePending_ = false;
    // Wall seconds since the last draw (releaseIdle).
    float idleSeconds_ = 0.0f;
};

}  // namespace geode::viz
