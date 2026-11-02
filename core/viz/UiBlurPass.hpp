#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Framebuffer.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

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
// Drawn by Renderer::render after the composite and the note blooms, the frame's last draw, into the
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
    // After the composite, with `targetFbo` holding the finished frame of width × height pixels.
    void draw(const UiBlurRegion& region, GLuint targetFbo, int width, int height, GLuint quadVao);

private:
    GLuint blurProgram_ = 0;
    GLuint maskProgram_ = 0;
    UniformCache blurUniforms_;
    UniformCache maskUniforms_;
    Framebuffer half_{"uiBlurHalf"};
    Framebuffer quarterA_{"uiBlurQuarterA"};
    Framebuffer quarterB_{"uiBlurQuarterB"};
};

}  // namespace geode::viz
