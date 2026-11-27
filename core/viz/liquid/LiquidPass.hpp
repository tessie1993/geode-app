#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/gl/Program.hpp"
#include "viz/glsl/ShaderSource.hpp"
#include "viz/liquid/FeedbackMotion.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid {

// The liquid itself (LiquidMath.hpp has the why): a pair of liquid targets carried frame to frame and
// swapped after each run. In Liquid mode both have one shared second attachment for what the composite
// shows, the liquid with its Surface lit (the pass reads only the liquid, and the frame reads the display
// only after the run that wrote it). One fullscreen pass a frame over liquid_frag.glsl; the composite then
// reads the display in place of the scene target.
//
// A shader style's Trails have no Surface: what they show is the liquid itself, which stays in 0..1 (it
// only ever mixes the RGBA8 scene into itself), so they keep no display. Their sides have the one
// attachment and run() returns the liquid just written, with no RGBA8 copy of it per frame.
//
// The liquid is half float where the GPU renders it (FluidBuffers' probe), so the slow pour-back of
// deep Liquidity does not band; RGBA8 otherwise. Both sides filter linearly: the back-trace lands
// between texels every frame.
class LiquidPass {
public:
    struct Frame {
        GLuint styleTex = 0;
        GLuint bornTex = 0;  // the style itself for Born = Live
        GLuint velocityTex = 0;
        float velScaleX = 0.0f;  // uv per (velocity * second)
        float velScaleY = 0.0f;
        int styleWidth = 1;
        int styleHeight = 1;
        float dt = 0.0f;
        motion::FrameTerms terms;  // the transform of the last frame, derived once by the renderer (lib_feedback)
        float vpX = 0.0f;            // the born disc's centre, centred units
        float vpY = 0.0f;
        float inject = 0.5f;
        float edge = 0.0f;
        float sharpen = 0.0f;
        float bornRadius = 0.15f;
        float bornGain = 0.0f;
        int bornMode = 0;  // 0 a frame from bornTex, 3 ink forms
        int age = 0;
        int surface = 0;
        bool flowing = false;  // velocityTex is the flow field's (else a zero texture, and nothing flows)
        bool feedback = true;
        float shimmer = 0.0f;
        float light = 0.0f;
        float time = 0.0f;
        GLuint quadVao = 0;
    };

    LiquidPass() = default;
    ~LiquidPass() { release(); }
    // Owns its program and targets: neither copied nor moved (viz/gl/GlHandle.hpp).
    LiquidPass(const LiquidPass&) = delete;
    LiquidPass& operator=(const LiquidPass&) = delete;
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, bool halfFloat, std::string* error);
    void release();
    // Sizes the targets; a new size starts the liquid over (black, filled by the next pours). `display`
    // keeps the lit display beside the liquid (Liquid mode); without it run() returns the liquid itself
    // (Trails). Adding or dropping the display at the same size keeps the liquid.
    bool ensure(int width, int height, bool display);
    // Starts the liquid over from the next frame's style: switching Liquid on must not show an old pour.
    void reset() { fresh_ = true; }
    // Frees both sides and keeps the program: ensure() builds them again, starting from the style.
    void releaseTargets();
    // Runs one frame; returns the texture to show, 0 when the targets are not there.
    GLuint run(const Frame& f);
    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Side {
        GLuint fbo = 0;
        GLuint liquid = 0;
    };
    bool makeSide(Side& s);
    void releaseSide(Side& s);
    // Both sides and the shared display.
    void releaseSides();
    // Attaches a new display to both sides, or detaches and frees it; false when a side will not complete.
    bool setDisplay(bool display);
    // What the composite shows after the run that wrote side `side`.
    GLuint shown(int side) const { return display_ != 0 ? display_ : sides_[side].liquid; }

    GLuint program_ = 0;
    UniformCache uniforms_;
    Side sides_[2];
    // RGBA8, what run() returns in Liquid mode: attachment 1 of both sides, written by every run. 0 for Trails.
    GLuint display_ = 0;
    int read_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool halfFloat_ = false;
    bool fresh_ = true;
};

}  // namespace geode::viz::liquid
