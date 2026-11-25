#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>

#include "viz/gl/Framebuffer.hpp"
#include "viz/gl/Program.hpp"
#include "viz/glsl/ShaderSource.hpp"
#include "viz/things/ThingPool.hpp"

namespace geode::viz {

// Things (viz/things/ThingPool.hpp) drawn over the picture: one full-screen triangle that walks the packed
// list, evaluates each Thing's shape (and its blend toward another) and lays it over the picture tinted from
// the current palette. It runs right after the Fronts and before the trails, so the trails smear the Things.
// The frame skips it when no Thing is alive.
class ThingPass {
public:
    ThingPass() = default;
    ~ThingPass() { release(); }
    ThingPass(const ThingPass&) = delete;
    ThingPass& operator=(const ThingPass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error);
    void release();
    // The context is gone: drops the program and target without a GL call.
    void forget();
    void releaseTarget() { target_.release(); }
    // Once a frame with no Thing alive, on the wall step: frees the target once `idleSeconds` pass unused.
    void releaseIdle(float wallDt, float idleSeconds);

    // Draws `sceneTex` with the Things laid over it into the pass's own target, and returns false when it
    // cannot. `colours` is one RGB per Thing (the renderer's palette at each palettePos). On true, target().tex()
    // is the picture with the Things on it, for everything after to read; `sceneTex` itself is left as it was.
    bool run(GLuint sceneTex, int width, int height, float aspect, const things::Packed& packed,
             const std::array<float, 3 * things::kMaxThings>& colours, GLuint quadVao);
    Framebuffer& target() { return target_; }

private:
    OwnedProgram uniforms_;
    Framebuffer target_{"things"};
    // Wall seconds since the pass last drew (releaseIdle).
    float idleSeconds_ = 0.0f;
};

}  // namespace geode::viz
