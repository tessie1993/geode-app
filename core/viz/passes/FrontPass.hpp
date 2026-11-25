#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>

#include "viz/fronts/FrontField.hpp"
#include "viz/gl/Framebuffer.hpp"
#include "viz/gl/Program.hpp"
#include "viz/glsl/ShaderSource.hpp"

namespace geode::viz {

// Fronts (viz/fronts/FrontField.hpp) drawn over any style's finished picture: one full-screen triangle that
// walks the packed front list. It runs right after the style and before the trails, Liquid and dive passes,
// so what a front does feeds back with the picture. The frame skips it when no front is alive.
class FrontPass {
public:
    FrontPass() = default;
    ~FrontPass() { release(); }
    FrontPass(const FrontPass&) = delete;
    FrontPass& operator=(const FrontPass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error);
    void release();
    // The context is gone: drops the program and target without a GL call.
    void forget();
    void releaseTarget() { target_.release(); }

    // Draws `sceneTex` with the fronts applied into the pass's own target, and returns false when it cannot.
    // `colours` is one RGB per front (the renderer's palette at each front's palettePos). `otherTex` is what
    // a Reveal shows behind the front (0: none, the picture is revealed from dark). On true, the caller
    // trades its scene target for target() (Framebuffer::swap), so everything after reads the result.
    bool run(GLuint sceneTex, GLuint otherTex, int width, int height, float aspect, const fronts::Packed& packed,
             const std::array<float, 3 * fronts::kMaxFronts>& colours, GLuint quadVao);
    Framebuffer& target() { return target_; }

private:
    OwnedProgram uniforms_;
    Framebuffer target_{"fronts"};
};

}  // namespace geode::viz
