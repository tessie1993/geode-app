#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>

#include "viz/gl/Framebuffer.hpp"
#include "viz/gl/Program.hpp"
#include "viz/glsl/ShaderSource.hpp"
#include "viz/fields/FieldSet.hpp"

namespace geode::viz {

// Fields (viz/fields/FieldSet.hpp) bending the whole picture: one full-screen triangle that walks the packed list
// and reads the picture from where each Field moves it from, on any style. It runs after the Fronts and Things and
// before the trails, so the trails smear what it bends. The frame skips it when no Field is alive.
class FieldPass {
public:
    FieldPass() = default;
    ~FieldPass() { release(); }
    FieldPass(const FieldPass&) = delete;
    FieldPass& operator=(const FieldPass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error);
    void release();
    // The context is gone: drops the program and target without a GL call.
    void forget();
    void releaseTarget() { target_.release(); }
    // Once a frame with no Field alive, on the wall step: frees the target once `idleSeconds` pass unused.
    void releaseIdle(float wallDt, float idleSeconds);

    // Draws `sceneTex` bent by the Fields into the pass's own target, and returns false when it cannot. On true,
    // target().tex() is the bent picture, for everything after to read; `sceneTex` itself is left as it was.
    bool run(GLuint sceneTex, int width, int height, float aspect, const fields::Packed& packed, GLuint quadVao);
    Framebuffer& target() { return target_; }

private:
    OwnedProgram uniforms_;
    Framebuffer target_{"fields"};
    // Wall seconds since the pass last drew (releaseIdle).
    float idleSeconds_ = 0.0f;
};

}  // namespace geode::viz
