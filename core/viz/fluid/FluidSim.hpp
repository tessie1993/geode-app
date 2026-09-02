#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Quad.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/fluid/FluidEmitters.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::fluid {

// Port of FluidSim.kt: the stable-fluids solver on half-float grids, velocity-only for the flow field.
class FluidSim {
public:
    FluidSim(const ProgramLoader& loader, bool velocityOnly = false) : loader_(loader), velocityOnly_(velocityOnly) {}
    ~FluidSim() { release(); }
    FluidSim(const FluidSim&) = delete;
    FluidSim& operator=(const FluidSim&) = delete;

    int simRes = 128;
    int dyeRes = 512;
    float densityDissipation = 1.0f;
    float velocityDissipation = 0.2f;
    float pressureDamp = 0.8f;
    int pressureIterations = 20;
    float curlStrength = 30.0f;
    float dyeCeiling = 0.0f;
    float chromaticAging = 0.0f;
    float audioBass = 0.0f;
    float audioMid = 0.0f;
    float audioTreble = 0.0f;
    float audioEnergy = 0.0f;
    float audioBeat = 0.0f;
    float timeSeconds = 0.0f;
    std::function<void(const std::string&)> onShaderError = [](const std::string&) {};

    bool available() const { return available_; }
    const Formats& texFormats() const { return formats_; }
    float aspect() const { return aspect_; }
    float flowScale() const { return 2.0f * rdx_ / static_cast<float>(velocity_ ? velocity_->height() : 128); }
    GLuint dyeTex() const { return dye_ ? dye_->read().tex() : 0; }
    GLuint velocityTex() const { return velocity_ ? velocity_->read().tex() : 0; }
    int velocityGridHeight() const { return velocity_ ? velocity_->height() : 128; }

    void create();
    bool resize(int w, int h);
    bool applyResolution(int newSimRes, int newDyeRes);
    void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc);
    void queueSplat(const Splat& s);
    void step(float dtRaw);
    void drawDisplay();
    void release();

private:
    enum Prog { kSplat, kAdvect, kCurl, kVorticity, kDivergence, kPressure, kGradient, kClear, kCopy, kDisplay, kProgCount };
    static const char* progAsset(int prog);

    void allocGrids(bool preserve);
    void copyInto(const Fbo& src, const Fbo& dst);
    void compileInjectionIfNeeded();
    std::optional<UniformCache> compileCustom(const std::string& src, std::optional<UniformCache> current, std::string* firstError);
    void runInjection(DoubleFbo& target, int mode, std::optional<UniformCache>& custom, float dt);
    void useProgram(int prog, int gridW, int gridH);
    void blitDiscarding(const Fbo& target);
    void blit(const Fbo& target);
    GLint loc(int prog, const char* name) { return programs_[static_cast<size_t>(prog)].loc(name); }
    void bindTex(const char* name, GLuint tex, int unit, int prog);
    void set1f(int prog, const char* name, float v) { glUniform1f(loc(prog, name), v); }
    void set2f(int prog, const char* name, float a, float b) { glUniform2f(loc(prog, name), a, b); }
    void set3f(int prog, const char* name, float a, float b, float c) { glUniform3f(loc(prog, name), a, b, c); }
    void deleteCustom(std::optional<UniformCache>& custom);

    const ProgramLoader& loader_;
    bool velocityOnly_;
    int width_ = 1;
    int height_ = 1;
    float aspect_ = 1.0f;
    float cellSize_ = 2.0f / 128.0f;
    float rdx_ = 128.0f / 2.0f;
    float halfRdx_ = 0.5f * 128.0f / 2.0f;
    float alpha_ = -(2.0f / 128.0f) * (2.0f / 128.0f);
    Formats formats_;
    std::optional<DoubleFbo> velocity_;
    std::optional<DoubleFbo> dye_;
    std::optional<DoubleFbo> pressure_;
    std::optional<Fbo> divergence_;
    std::optional<Fbo> curl_;
    FullscreenTriangle quad_;
    GLuint linearSampler_ = 0;
    std::string baseVertSrc_;
    std::array<UniformCache, kProgCount> programs_{};
    bool programsBuilt_ = false;
    std::vector<Splat> pending_;
    std::optional<UniformCache> customForce_;
    std::optional<UniformCache> customDye_;
    std::mutex injectionLock_;
    std::string pendingForceSrc_;
    std::string pendingDyeSrc_;
    bool injectionDirty_ = false;
    bool available_ = false;
};

}  // namespace geode::viz::fluid
