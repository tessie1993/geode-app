#pragma once
#include <GLES3/gl3.h>
#include <projectM-4/projectM.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "viz/Framebuffer.hpp"
#include "viz/PcmHandoff.hpp"
#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// One projectM instance rendering into a scene-owned framebuffer (projectm_opengl_render_frame_fbo,
// backported onto v4.1.7 by tools/projectm-v4.1.7-render-fbo-backport.patch), graded by pm_post_frag.
// The engine never touches the window surface, so the scene is independent of the EGL config.
class MilkdropScene : public Scene {
public:
    MilkdropScene(ProgramLoader loader, SceneHost host) : loader_(loader), host_(std::move(host)), pcm_(kPcmCapacity * 2, 0.0f) {}
    ~MilkdropScene() override { release(); }

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Milkdrop; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeSoundFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void acceptPcmStereo(const float* mono, const float* side, int count) override;
    void queueMilkPreset(const std::string& path) override;
    void setMilkFallbackPreset(const std::string& path) override;
    void reloadMilkPreset() override;
    void setMilkTextureDir(const std::string& dir) override;

private:
    static constexpr double kLoadDebounceSeconds = 0.4;
    // How long a failed projectm_create() is left alone before the next attempt. ensureEngine()
    // runs once per frame, so without a wait a context that cannot host the engine - the state a
    // return from the background can leave behind - is retried sixty times a second forever.
    static constexpr double kCreateRetrySeconds = 2.0;
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
    void ensureEngine(double now);
    // Re-queues the preset the scene last had on screen and clears the load debounce, so a
    // freshly created engine loads it on the very next frame instead of waiting one window out.
    // A request still waiting is newer than that preset and is left to win.
    void armLastPreset();
    void loadPendingPreset(double now);
    std::optional<std::string> takeError();
    void diagnoseBlackFrame();
    void drainEngineErrors();
    void set1f(const char* name, float value) { glUniform1f(postLocs_.loc(name), value); }

    std::string id_ = "milkdrop";
    ProgramLoader loader_;
    SceneHost host_;
    // Stereo frames (L R interleaved); pcmCount_ counts frames.
    std::vector<float> pcm_;
    int pcmCount_ = 0;
    PcmStarve pcmStarve_;
    // The frame rate presets read as `fps`, and an export's render clock (seconds since this engine's
    // first frame): projectM otherwise runs on the wall clock, so a render's speed would follow the encoder.
    int engineFps_ = 0;
    double engineClockS_ = 0.0;
    // The texture search paths the engine was last given; setting them rebuilds its texture manager.
    std::vector<std::string> textureDirs_;
    // Set by a reload (under presetLock_): the next load hands the paths over again.
    bool rescanTextures_ = false;
    Engine engine_;
    // Whether engine_ holds a preset of ours. A new engine starts on projectM's idle preset, which
    // the app never shows: until this is set the scene draws black, and the first load is a hard
    // cut, because a soft cut would fade in from that idle preset.
    bool engineHasPreset_ = false;
    int width_ = 0;
    int height_ = 0;
    bool reportedCreateFailure_ = false;
    double lastCreateAttemptSeconds_ = 0.0;
    Framebuffer frame_{"milkdrop"};
    int engineWidth_ = 0;
    int engineHeight_ = 0;
    GLuint postProgram_ = 0;
    bool postProgramOk_ = false;
    UniformCache postLocs_{0};
    GLuint postVao_ = 0;
    float rotationAngle_ = 0.0f;
    float cyclePhase_ = 0.0f;
    float beatPulse_ = 0.0f;
    double lastLoadSeconds_ = 0.0;
    uint64_t seenAutoMark_ = 0;  // the last of Auto's marks a load here took (transition_choice::takeAuto)
    SceneParams params_;
    int diagFrames_ = 0;
    bool diagDone_ = false;
    bool reportedEngineGlError_ = false;
    std::mutex presetLock_;
    std::string pendingPresetPath_;
    std::string lastPresetPath_;
    std::string sharedTextureDir_;
    std::mutex errorLock_;
    std::string lastError_;
    // The last message written to logcat, so a preset that fails on every frame it is retried
    // writes one line rather than one line per frame. Guarded by errorLock_.
    std::string lastLoggedError_;
};

}  // namespace geode::viz
