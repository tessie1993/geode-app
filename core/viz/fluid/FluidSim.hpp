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
#include "viz/fluid/FluidMath.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::fluid {

// The stable-fluids solver on half-float grids (first a port of FluidSim.kt, after Dobryakov's
// WebGL fluid), velocity-only for the flow field. Per step:
//
//   advect velocity (MacCormack when `macCormack`, else semi-Lagrangian) and temperature
//   -> every splat of the frame in one batched pass per field
//   -> vorticity confinement and buoyancy, one pass
//   -> divergence, then the pressure solve as a multigrid V-cycle (FluidMath planPressureSolve
//      spends the user's "Solver iterations" budget), then the gradient subtraction
//   -> dye: splats, then advection the same way as the velocity.
//
// The temperature field exists only for a style that asks for it (`setTemperature`): the smoke,
// lava and aurora looks read it, and buoyancy turns it into lift.
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
    // Second-order advection (FluidQuality's tier decides); off costs nothing, on costs one extra
    // pass per field and a scratch target per field.
    bool macCormack = false;
    // Lift per unit of heat, sim units / s^2, and the heat a splat centre gets this frame
    // (FluidMath heatInjection), and the cooling rate per second. Read only with a temperature field.
    float buoyancy = 0.0f;
    float heat = 0.0f;
    float cooling = 0.5f;
    std::function<void(const std::string&)> onShaderError = [](const std::string&) {};

    bool available() const { return available_; }
    const Formats& texFormats() const { return formats_; }
    float aspect() const { return aspect_; }
    float flowScale() const { return 2.0f * rdx_ / static_cast<float>(velocity_ ? velocity_->height() : 128); }
    GLuint dyeTex() const { return dye_ ? dye_->read().tex() : 0; }
    GLuint velocityTex() const { return velocity_ ? velocity_->read().tex() : 0; }
    int velocityGridHeight() const { return velocity_ ? velocity_->height() : 128; }
    /** The heat field, the velocity grid's size; 0 for a style without one (samples as zero). */
    GLuint temperatureTex() const { return temperature_ ? temperature_->read().tex() : 0; }
    /** Whether this sim carries a temperature field; takes effect at the next grid allocation. */
    void setTemperature(bool on);

    void create();
    bool resize(int w, int h);
    bool applyResolution(int newSimRes, int newDyeRes);
    void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc);
    void queueSplat(const Splat& s);
    void step(float dtRaw);
    void drawDisplay();
    void release();

private:
    enum Prog {
        kSplat,
        kSplatBatch,
        kAdvect,
        kMacCormack,
        kCurl,
        kVorticity,
        kDivergence,
        kMgSmooth,
        kMgRestrict,
        kMgProlong,
        kGradient,
        kClear,
        kCopy,
        kDisplay,
        kProgCount
    };
    static const char* progAsset(int prog);

    void allocGrids(bool preserve);
    void copyInto(const Fbo& src, const Fbo& dst);
    void compileInjectionIfNeeded();
    std::optional<UniformCache> compileCustom(const std::string& src, std::optional<UniformCache> current, std::string* firstError);
    void runInjection(DoubleFbo& target, int mode, std::optional<UniformCache>& custom, float dt);
    void runBatchedSplats(DoubleFbo& target, int mode);
    // `scratch` null is first-order; else the field's MacCormack scratch, made on first use.
    void advect(DoubleFbo& field, std::optional<Fbo>* scratch, bool linearVelocity, float dt, float d0, float d1, float d2);
    bool ensureScratch(std::optional<Fbo>& scratch, const DoubleFbo& like, TexFormat fmt, bool linear);
    void solvePressure();
    void vCycle(int level, const math::SolvePlan& plan);
    void smoothPass(int level);
    DoubleFbo& levelP(int level) { return level == 0 ? *pressure_ : mgP_[static_cast<size_t>(level - 1)]; }
    GLuint levelRhs(int level) const { return level == 0 ? divergence_->tex() : mgRhs_[static_cast<size_t>(level - 1)].tex(); }
    float levelH2(int level) const;
    static void clearTarget(const Fbo& target);
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
    Formats formats_;
    std::optional<DoubleFbo> velocity_;
    std::optional<DoubleFbo> dye_;
    std::optional<DoubleFbo> pressure_;
    std::optional<Fbo> divergence_;
    std::optional<Fbo> curl_;
    std::optional<DoubleFbo> temperature_;
    std::optional<Fbo> velocityScratch_;
    std::optional<Fbo> dyeScratch_;
    // Multigrid levels 1..n-1 (level 0 is pressure_ / divergence_): a correction and a right-hand side each.
    std::vector<DoubleFbo> mgP_;
    std::vector<Fbo> mgRhs_;
    bool temperatureOn_ = false;
    FullscreenTriangle quad_;
    GLuint linearSampler_ = 0;
    std::string baseVertSrc_;
    std::array<UniformCache, kProgCount> programs_;
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
