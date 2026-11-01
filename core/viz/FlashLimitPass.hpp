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
// Per frame, all on the GPU (no readback, no stall):
//   1. frame -> cells: (16 x 8) x (9 x 8) cells (portrait 9 x 16 tiles), each the exact mean
//      relative luminance of its pixels (every pixel texelFetched and sRGB-decoded once).
//   2. cells -> tiles: 16 x 9 tiles, 64 cells each.
//   3. tiles + last state -> this state (ping-pong): flash_limit::step per tile (shaders/
//      lib_flash_limit.glsl mirrors core/viz/FlashLimiter.cpp, which core/tests/flash_limit_test.cpp
//      proves against the WCAG count), plus the gain each tile needs.
//   4. frame -> target: each pixel times its bilinearly blended tile gain (a copy where no nearby
//      tile is limited).
// Cost: passes 2 and 3 are a few hundred fragments, nothing. Pass 1 reads the frame once (one
// texelFetch and an sRGB decode per pixel, over 128 x 72 fragments); pass 4 is one full-screen copy.
// Against drawing straight to the target that is one extra full-frame write and two extra reads,
// about what one simple post effect costs; at 1080 x 2400 on a mid-range GPU, well under a
// millisecond. A thermal tier does not drop it: it is a safety pass, not an effect.
//
// The tile grid is 16 x 9 of the frame. A WCAG "general flash" has to cover a quarter of a 10-degree
// field of view, several tiles on a phone at reading distance, so a tile-sized flash or larger is
// caught; a flicker confined well inside one tile, or a pattern that inverts while its tile's mean
// stays put (a checkerboard swapping black and white), is not measured as a flash.
//
// State is RGBA8 everywhere, 16-bit fixed point in two channels per value (three MRT attachments per
// state set, ES 3.0 guarantees four). Half-float targets would need EXT_color_buffer_half_float, and
// a half's 11-bit mantissa cannot hold the per-frame refill (0.13 / 60 = 0.002 on top of ~0.36)
// without drift; 16-bit fixed point is exact, the same on every GPU, and the host test runs on the
// very same quantised values. Nothing here depends on an extension.
//
// The state is created cleared ("not primed"), so the first frame after create, a surface change or
// a turn between portrait and landscape is measured and shown as it is (no fade-in from black). A
// resize that keeps the orientation keeps the state. Scene cuts keep it too: a cut is exactly what
// has to be limited.
class FlashLimitPass {
public:
    static constexpr int kCellsPerTile = 8;

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
    int tileColumns() const { return cols_; }
    int tileRows() const { return rows_; }

private:
    struct StateSet {
        GLuint fbo = 0;
        GLuint tex[3] = {0, 0, 0};
    };

    bool createStates();
    void releaseStates();
    static void draw(GLuint quadVao);

    GLuint lumProgram_ = 0;
    GLuint tileProgram_ = 0;
    GLuint stateProgram_ = 0;
    GLuint applyProgram_ = 0;
    UniformCache lumUniforms_;
    UniformCache tileUniforms_;
    UniformCache stateUniforms_;
    UniformCache applyUniforms_;
    Framebuffer frame_{"flashFrame"};
    Framebuffer cells_{"flashCells"};
    Framebuffer tiles_{"flashTiles"};
    StateSet states_[2];
    // The set holding the latest state; apply() steps it into the other one.
    int current_ = 0;
    bool stepped_ = false;
    int cols_ = 0;
    int rows_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
