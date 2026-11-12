#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/EndlessZoom.hpp"
#include "viz/Framebuffer.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// Endless zoom's stage (EndlessZoom.hpp has the law and the reasons): one fullscreen pass over the
// finished picture of any style, between the scene target (or the feedback pass) and the composite,
// which reads its texture in the picture's place. It keeps nothing from frame to frame.
class DivePass {
public:
    DivePass() = default;
    ~DivePass() { release(); }
    DivePass(const DivePass&) = delete;
    DivePass& operator=(const DivePass&) = delete;

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error);
    void release();
    void releaseTarget() { target_.release(); }
    // Draws `sceneTex` (srcWidth x srcHeight) nested at `phase` into a width x height target; `mix` 0..1
    // fades the dive in over the plain picture. Returns the texture to show, 0 when it cannot draw.
    GLuint run(GLuint sceneTex, int srcWidth, int srcHeight, int width, int height, float phase, float mix,
               const endless::tunnel::Look& look, GLuint quadVao);

private:
    GLuint program_ = 0;
    UniformCache uniforms_;
    Framebuffer target_{"dive"};
};

}  // namespace geode::viz
