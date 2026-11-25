#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/gl/Framebuffer.hpp"
#include "viz/params/Params.hpp"
#include "viz/gl/Program.hpp"
#include "viz/glsl/ShaderSource.hpp"

namespace geode::viz {

// The fade quad, or the trail-warp resample when zoom/warp echo is on.
class TrailPass {
public:
    ~TrailPass() { release(); }
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    void release();
    // The context is gone: drops the programs and the trail without a GL call.
    void forget();
    void releaseTargets() { trail_.release(); }
    // Once a frame, on the wall step: frees the echo's target once `idleSeconds` pass without an echo draw.
    void releaseIdle(float wallDt, float idleSeconds);
    // Leaves the scene target bound. The echo trades `sceneTarget`'s texture for the pass's own, so read its
    // fbo() and tex() after this, never before.
    void apply(const SceneParams& p, float keep, float timeSeconds, float dt, Framebuffer& sceneTarget, GLuint quadVao,
               int renderWidth, int renderHeight);
    void drawFadeQuad(float alpha, GLuint quadVao);

    static float fadeAlpha(float keep, float dt);
    static float warpDecay(float retention, float dt);
    static float warpScale(float trailZoom, float dt);

private:
    void drawTrailWarp(const SceneParams& p, float retention, float timeSeconds, float dt, Framebuffer& sceneTarget,
                       GLuint quadVao, int renderWidth, int renderHeight);

    Framebuffer trail_{"trail"};
    // Wall seconds since the echo last drew (releaseIdle).
    float warpIdleSeconds_ = 0.0f;
    OwnedProgram fadeUniforms_;   // the fade program and its locations
    OwnedProgram trailUniforms_;  // the trail warp program and its locations
};

}  // namespace geode::viz
