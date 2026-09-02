#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Framebuffer.hpp"
#include "viz/Params.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// Port of TrailPass.kt: the fade quad, or the trail-warp resample when zoom/warp echo is on.
class TrailPass {
public:
    ~TrailPass() { release(); }
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    void release();
    void apply(const SceneParams& p, float keep, float timeSeconds, float dt, const Framebuffer& sceneTarget, GLuint quadVao,
               int renderWidth, int renderHeight);
    void drawFadeQuad(float alpha, GLuint quadVao);

    static float fadeAlpha(float keep, float dt);
    static float warpDecay(float retention, float dt);

private:
    void drawTrailWarp(const SceneParams& p, float retention, float timeSeconds, float dt, const Framebuffer& sceneTarget,
                       GLuint quadVao, int renderWidth, int renderHeight);

    Framebuffer trail_{"trail"};
    GLuint fadeProgram_ = 0;
    GLuint trailWarpProgram_ = 0;
    UniformCache fadeUniforms_;
    UniformCache trailUniforms_;
};

}  // namespace geode::viz
