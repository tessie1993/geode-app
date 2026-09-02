#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>

#include "viz/Params.hpp"
#include "viz/Transition.hpp"

namespace geode::viz {

// Port of CompositePass.kt: the frame's one opaque fullscreen pass over composite_frag.glsl.
class CompositePass {
public:
    static constexpr int kStyleLayer = 6;
    static constexpr float kDitherAmount = 1.0f / 255.0f;

    struct Inputs {
        GLuint texA = 0;
        GLuint texB = 0;
        GLuint flowTex = 0;
        float flowStrength = 0.0f;
        GLuint rippleTex = 0;
        float rippleTexelW = 0.0f;
        float rippleTexelH = 0.0f;
        float rippleStrength = 0.0f;
        float rippleSpecular = 0.0f;
        float progress = 1.0f;
        float layerMix = 0.5f;
        int blendOrdinal = 0;
        bool hasLayer = false;
        bool hasOutgoing = false;
        TransitionStyle transitionStyle = TransitionStyle::Fade;
        std::string transitionId = "fade";
        float ratio = 1.0f;
        float timeSeconds = 0.0f;
        float hitImpulse = 0.0f;
        float flash = 0.0f;
        float strobeHz = 0.0f;
        float postRotationAngle = 0.0f;
        float postCyclePhase = 0.0f;
        float postBeatPulse = 0.0f;
        GLuint quadVao = 0;
        SceneParams fx;
        std::array<float, 4> gateA{};
        std::array<float, 4> gateB{};
    };

    CompositePass(const ShaderSource& assets, ProgramBinaryCache* cache) : transitions_(assets, cache), assets_(assets) {}
    ~CompositePass() { release(); }

    bool create(const std::string& fadeVert, std::string* error);
    void release();
    void releaseStaleTextures();
    GLuint zeroTex() const { return zeroTex_; }
    void warmTransition(const std::string& id) { transitions_.warm(id); }
    TransitionCatalog& catalog() { return transitions_.catalog(); }
    void draw(const Inputs& inputs);

private:
    GLuint createZeroTexture();
    GLuint createNoiseTexture();
    void bindTextures(UniformCache& p, const Inputs& inputs);
    void uploadFrameUniforms(UniformCache& p, const Inputs& inputs, const TransitionCatalog::Def* definition);
    void uploadGradeUniforms(UniformCache& p, const Inputs& inputs);

    TransitionPrograms transitions_;
    const ShaderSource& assets_;
    GLuint noiseTex_ = 0;
    GLuint zeroTex_ = 0;
};

}  // namespace geode::viz
