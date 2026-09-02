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
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::fluid {

// Port of RippleSim.kt: a height-field wave grid with an optional advected ink layer.
class RippleSim {
public:
    explicit RippleSim(const ProgramLoader& loader) : loader_(loader) {}
    ~RippleSim() { release(); }
    RippleSim(const RippleSim&) = delete;
    RippleSim& operator=(const RippleSim&) = delete;

    float waveSpeed = 1.2f;
    float damping = 0.985f;
    bool inkEnabled = false;
    float inkFlow = 1.0f;
    float inkDissipation = 0.35f;
    std::function<void(const std::string&)> onShaderError = [](const std::string&) {};

    int simRes() const { return simRes_; }
    bool available() const { return available_; }
    float aspect() const { return aspect_; }
    GLuint heightTex() const { return grid_ ? grid_->read().tex() : 0; }
    GLuint inkTex() const { return ink_ ? ink_->read().tex() : 0; }
    bool inkAvailable() const { return ink_ && ink_->ok(); }
    float texelW() const { return 1.0f / static_cast<float>(grid_ ? grid_->width() : 1); }
    float texelH() const { return 1.0f / static_cast<float>(grid_ ? grid_->height() : 1); }
    const Formats& texFormats() const { return formats_; }

    void create();
    bool resize(int w, int h);
    bool applyResolution(int newSimRes);
    void queueDrop(float x, float y, float radius, float amplitude, float r = 0.0f, float g = 0.0f, float b = 0.0f);
    void queueStroke(float x, float y, float dx, float dy, float dt, float radius, float strength, float r = 0.0f, float g = 0.0f,
                     float b = 0.0f);
    void step(float dtRaw);
    void release();

private:
    struct Drop {
        float x;
        float y;
        float radius;
        float amplitude;
        float r;
        float g;
        float b;
    };
    enum Prog { kSplat, kUpdate, kInkSplat, kInkAdvect, kProgCount };
    static constexpr int kMaxPending = 64;
    static constexpr int kDropsPerPass = 8;
    static constexpr int kMaxSubsteps = 6;
    static constexpr float kInkCeiling = 6.0f;

    void allocGrid();
    void clearInk();
    void useProgram(int prog, int gridW, int gridH);
    void blitDiscarding(const Fbo& target);
    GLint loc(int prog, const char* name) { return programs_[static_cast<size_t>(prog)].loc(name); }
    void bindTex(const char* name, GLuint tex, int unit, int prog);
    void set1f(int prog, const char* name, float v) { glUniform1f(loc(prog, name), v); }

    const ProgramLoader& loader_;
    int simRes_ = 384;
    bool available_ = false;
    int width_ = 1;
    int height_ = 1;
    float aspect_ = 1.0f;
    float cellSize_ = 2.0f / 384.0f;
    Formats formats_;
    std::optional<DoubleFbo> grid_;
    std::optional<DoubleFbo> ink_;
    FullscreenTriangle quad_;
    std::array<UniformCache, kProgCount> programs_;
    int programsBuilt_ = 0;
    std::mutex pendingLock_;
    std::vector<Drop> pending_;
    std::vector<Drop> drained_;
    std::array<float, kDropsPerPass * 4> dropVec_{};
    std::array<float, kDropsPerPass * 4> dropColorVec_{};
};

}  // namespace geode::viz::fluid
