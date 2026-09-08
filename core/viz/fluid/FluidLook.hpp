#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Quad.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::fluid {

// Port of FluidLook.kt: bloom, sunrays and the dithered display pass over the dye.
class Look {
public:
    explicit Look(const ProgramLoader& loader) : loader_(loader) {}
    ~Look() { release(); }
    Look(const Look&) = delete;
    Look& operator=(const Look&) = delete;

    /** How many LOOK branches fluid_display_frag.glsl has; a style's look index is below this. */
    static constexpr int kLookCount = 9;

    float bloomIntensity = 0.8f;
    float bloomThreshold = 0.6f;
    float bloomKnee = 0.7f;
    float sunraysWeight = 1.0f;
    // What the styled looks read: the scene clock, the four band envelopes and the emitters' base hue.
    float timeSeconds = 0.0f;
    std::array<float, 4> audio{};
    float lookHue = 0.0f;

    bool available() const { return available_; }

    /** `look` selects the display branch; it is a compile-time keyword, one Look per scene. */
    void create(const Formats& formats, int look);
    void resize(int w, int h);
    void process(GLuint dyeTex, bool bloomOn, bool sunraysOn);
    void drawDisplay(GLuint dyeTex, bool shadingOn, bool bloomOn, bool sunraysOn, int viewportW, int viewportH);
    void release();

private:
    static constexpr int kBloomBaseRes = 256;
    static constexpr int kBloomMaxLevels = 8;
    static constexpr int kSunraysRes = 196;
    static constexpr int kDisplayVariants = 8;

    static std::string withKeywords(const std::string& src, int flags, int look);
    void applyBloom(GLuint dyeTex);
    void applySunrays(GLuint dyeTex);
    void releaseTargets();
    void use(UniformCache& program, float invW, float invH);
    void blitDiscarding(const Fbo& target);
    void blit(const Fbo& target);
    void bindTex(UniformCache& program, const char* name, GLuint tex, int unit);

    const ProgramLoader& loader_;
    Formats formats_;
    UniformCache prefilter_{0};
    UniformCache bloomBlur_{0};
    UniformCache bloomFinal_{0};
    UniformCache sunraysMask_{0};
    UniformCache sunrays_{0};
    UniformCache blur_{0};
    std::array<UniformCache, kDisplayVariants> display_;
    std::vector<Fbo> bloomMips_;
    std::optional<Fbo> bloomResult_;
    std::optional<Fbo> sunraysMaskTarget_;
    std::optional<Fbo> sunraysTarget_;
    std::optional<Fbo> sunraysTemp_;
    GLuint ditherTex_ = 0;
    FullscreenTriangle quad_;
    int targetW_ = 1;
    int targetH_ = 1;
    bool available_ = false;
};

}  // namespace geode::viz::fluid
