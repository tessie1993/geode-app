#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid {

// The liquid itself (LiquidMath.hpp has the why): a pair of liquid targets carried frame to frame and
// swapped after each run, each with a second attachment for what the composite shows. One fullscreen
// pass a frame over liquid_frag.glsl; the composite then reads display() in place of the scene target.
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
        DiveFrame dive;
        float inject = 0.5f;
        float edge = 0.0f;
        float sharpen = 0.0f;
        float bornRadius = 0.15f;
        float bornGain = 0.0f;
        int bornMode = 0;  // 0 a frame from bornTex, 3 ink forms
        int age = 0;
        int surface = 0;
        bool feedback = true;
        float shimmer = 0.0f;
        float warp = 0.0f;  // 0..1, a slow sine bend of the carried frame (MilkDrop's warp); 0 off
        float light = 0.0f;
        float time = 0.0f;
        GLuint quadVao = 0;
    };

    ~LiquidPass() { release(); }
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, bool halfFloat, std::string* error);
    void release();
    // Sizes the targets; a new size starts the liquid over (black, filled by the next pours).
    bool ensure(int width, int height);
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
        GLuint display = 0;
    };
    bool makeSide(Side& s);
    void releaseSide(Side& s);

    GLuint program_ = 0;
    UniformCache uniforms_;
    Side sides_[2];
    int read_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool halfFloat_ = false;
    bool fresh_ = true;
};

}  // namespace geode::viz::liquid
