#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Params.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

class ProgramBinaryCache;

// Customize › Particles: pieces of whatever style is on screen thrown off as glowing particles, drawn
// over the composite. Each particle takes its colour from the spot of the picture it was born on, so
// the style itself seems to break up and fly.
//
// Stateless on the GPU: particle i's birth place, drift and life are hashes of its id and its cycle,
// and its age comes from one clock that only moves with the music (the `particles` param, which the
// reaction routes drive). Loud passages fling more of them faster; when the music stops they hang in
// the air; an export draws exactly the same ones. `particleFall` sets gravity: falling, or flying up.
class ParticleLayer {
public:
    static constexpr int kMaxParticles = 3072;
    ~ParticleLayer() { release(); }

    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, std::string* error);
    void release();
    // Pure: steps the music clock and the visible count from the final params.
    void step(const SceneParams& p, float dt);
    // Draws into the bound framebuffer, blending over it; sceneTex is the style's own frame.
    void draw(GLuint sceneTex, int width, int height, float ratio);

    float clock() const { return clock_; }
    int count() const { return count_; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    UniformCache uniforms_;
    float clock_ = 0.0f;
    float level_ = 0.0f;
    int count_ = 0;
    float fall_ = 0.3f;
};

}  // namespace geode::viz
