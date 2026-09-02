#pragma once
#include <GLES3/gl3.h>
#include <projectM-4/projectM.h>

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// Port of MilkdropScene.kt and milkdrop_jni.cpp: one projectM instance rendering to the default framebuffer,
// copied off it and graded by pm_post_frag.
class MilkdropScene : public Scene {
public:
    MilkdropScene(ProgramLoader loader, SceneHost host) : loader_(loader), host_(std::move(host)), pcm_(kPcmCapacity, 0.0f) {}
    ~MilkdropScene() override { release(); }

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Milkdrop; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeFeatureFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void acceptPcm(const float* samples, int count) override;
    void setWindowSize(int width, int height) override;
    void queueMilkPreset(const std::string& path) override;
    void reloadMilkPreset() override;
    void setMilkTextureDir(const std::string& dir) override;

private:
    static constexpr double kLoadDebounceSeconds = 0.4;
    static constexpr int kDiagFrames = 90;
    static constexpr int kDiagWarmup = 20;
    static constexpr int kPcmCapacity = 8192;
    static constexpr int kEnginePcmSamples = 576;
    static constexpr float kTwoPi = 6.2831853f;

    struct EngineDeleter {
        void operator()(projectm_handle h) const;
    };
    using Engine = std::unique_ptr<std::remove_pointer_t<projectm_handle>, EngineDeleter>;

    static void onPresetSwitchFailed(const char* presetFilename, const char* message, void* userData);
    static double nowSeconds();
    int effectiveWindowWidth() const { return windowWidth_ > 1 ? windowWidth_ : width_; }
    int effectiveWindowHeight() const { return windowHeight_ > 1 ? windowHeight_ : height_; }
    void ensureEngine();
    void ensureFrameTexture();
    void releaseFrameTexture();
    void loadPendingPreset(double now);
    std::optional<std::string> takeError();
    void diagnoseBlackFrame();
    void set1f(const char* name, float value) { glUniform1f(postLocs_.loc(name), value); }

    std::string id_ = "milkdrop";
    ProgramLoader loader_;
    SceneHost host_;
    std::vector<float> pcm_;
    int pcmCount_ = 0;
    Engine engine_;
    int width_ = 0;
    int height_ = 0;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
    bool reportedCreateFailure_ = false;
    GLuint frameTex_ = 0;
    int texWidth_ = 0;
    int texHeight_ = 0;
    int engineWidth_ = 0;
    int engineHeight_ = 0;
    GLuint postProgram_ = 0;
    bool postProgramOk_ = false;
    UniformCache postLocs_{0};
    GLuint postVao_ = 0;
    float rotationAngle_ = 0.0f;
    float zoomPhase_ = 0.0f;
    float cyclePhase_ = 0.0f;
    float beatPulse_ = 0.0f;
    double lastLoadSeconds_ = 0.0;
    SceneParams params_;
    int diagFrames_ = 0;
    bool diagDone_ = false;
    std::mutex presetLock_;
    std::string pendingPresetPath_;
    std::string lastPresetPath_;
    std::string sharedTextureDir_;
    std::mutex errorLock_;
    std::string lastError_;
};

}  // namespace geode::viz
