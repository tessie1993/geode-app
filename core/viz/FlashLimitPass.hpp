#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Framebuffer.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// The output flash limiter: what makes "no more than three flashes a second" true of everything the
// renderer shows, whatever drew it (built-in styles, MilkDrop presets through projectM, user GLSL,
// post FX, transitions, the underlay). Renderer::composite draws the finished frame into this pass's
// own frame target instead of the real one; apply() then measures it and draws it on to the real
// target (the window, the wallpaper's surface, or the export's encoder surface: all three share
// Renderer::composite), so live, wallpaper, dream and export are limited alike. The W00 overlay
// (cover art, text) is drawn after, outside the limiter.
//
// The rule (VisualSafety.hpp, FlashLimiter.hpp): WCAG 2.3.1 transitions of luminance and of
// saturated red are counted per region (3 x 3 overlapping quarter-screen windows) on what is shown;
// frames pass untouched until a sixth transition in a second would show, and only then is the
// region held at last frame's luminance (and its red guarded away) until the window allows again.
//
// Per frame, all on the GPU (no readback, no stall):
//   1. frame -> cells: (16 x 8) x (9 x 8) cells, each the exact mean over its pixels of luminance,
//      saturated-red value and guarded luminance (every pixel texelFetched and sRGB-decoded once).
//   2. cells -> tiles: 16 x 9 tiles, pixel-weighted.
//   3. control: flash_limit::step (shaders/flash_control_frag.glsl mirrors core/viz/FlashLimiter.cpp)
//      on a 16 x 16 ping-pong state: tile transforms plus each region's two detectors.
//   4. frame -> target: each pixel through its tile's transform; a plain copy where it is identity.
//
// Why a frame target, and not the composite pass applying last frame's gains itself: a transition
// is only known once the frame that makes it has been measured. Gains computed from the previous
// frame would let exactly the forbidden transition (the sixth, the one that completes a fourth
// flash) reach the screen for a frame before the hold, and one frame is enough to be a transition.
// Nor can the composite predict it: what decides is the mean of the pixels it is about to write,
// which no single fragment knows. So the frame is drawn off screen, measured, and then shown.
// The cost of that, at 1440 x 3200 (RGBA8): an 18.4 MB frame target (the cells, tiles and state are
// under 100 KB together), and per frame one extra full-frame write (the composite into the target)
// and two full-frame reads (pass 1 and pass 4), on top of the target write the renderer already
// did; 55 MB a frame of extra traffic, about 3.3 GB/s at 60 fps, roughly what one simple full-screen
// post effect costs. Passes 2 and 3 are a few hundred fragments. A thermal tier does not drop the
// pass: it is a safety pass, not an effect.
//
// State is RGBA8 everywhere, 16-bit fixed point in two channels per value (four MRT attachments for
// the control state, two for cells and tiles; ES 3.0 guarantees four). Nothing here depends on an
// extension, and the device holds bit for bit what the host test runs on.
//
// The state is created cleared ("not primed"), so the first frame after create, a surface change or
// a turn between portrait and landscape is measured and shown as it is. A resize that keeps the
// orientation keeps the state. Scene cuts keep it too: a cut is exactly what has to be counted.
class FlashLimitPass {
public:
    static constexpr int kCellsPerTile = 8;
    static constexpr int kControlSize = 16;  // 16 x 16 texels: 144 tiles, 18 detectors

    FlashLimitPass() = default;
    ~FlashLimitPass() { release(); }
    FlashLimitPass(const FlashLimitPass&) = delete;
    FlashLimitPass& operator=(const FlashLimitPass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    void release();

    // Sizes the targets for a width x height output; false when the pass cannot run this frame (its
    // programs or a target failed), in which case the caller draws straight to its target.
    bool ensure(int width, int height);
    // Binds the frame target (whose old contents are discarded) for the caller to draw the frame into.
    void bindFrame();
    // Passes 1-4. Leaves `targetFbo` bound with a width x height viewport, blending off.
    void apply(GLuint targetFbo, float dt, GLuint quadVao);
    // Whether apply() has run since the state was (re)created. Until it has, the next frame applied
    // primes the state, so callers drawing a placeholder (the black of a scene still being staged)
    // draw it straight to their target instead, and the first real frame is shown as it is.
    bool hasHistory() const { return stepped_; }

private:
    // An FBO with `count` RGBA8 attachments, all cleared to zero.
    struct Target {
        GLuint fbo = 0;
        GLuint tex[4] = {0, 0, 0, 0};
        int count = 0;
        int width = 0;
        int height = 0;
        bool create(int w, int h, int attachments);
        void release();
        void discard() const;
    };

    bool createGrid(int cols, int rows);
    void releaseGrid();
    void uploadConstants(UniformCache& u);
    static void draw(GLuint quadVao);

    GLuint cellsProgram_ = 0;
    GLuint tilesProgram_ = 0;
    GLuint controlProgram_ = 0;
    GLuint applyProgram_ = 0;
    UniformCache cellsUniforms_;
    UniformCache tilesUniforms_;
    UniformCache controlUniforms_;
    UniformCache applyUniforms_;
    Framebuffer frame_{"flashFrame"};
    Target cells_;
    Target tiles_;
    Target control_[2];
    // The control set holding the latest state; apply() steps it into the other one.
    int current_ = 0;
    bool stepped_ = false;
    int cols_ = 0;
    int rows_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
