#pragma once
#include <GLES3/gl3.h>
#include <android/asset_manager.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "sound/SoundEngine.hpp"
#include "viz/Adsr.hpp"
#include "viz/CameraRig.hpp"
#include "viz/CompositePass.hpp"
#include "viz/Director.hpp"
#include "viz/FlashLimiter.hpp"
#include "viz/Framebuffer.hpp"
#include "viz/GlProfile.hpp"
#include "viz/LayerState.hpp"
#include "viz/Lfo.hpp"
#include "viz/Macros.hpp"
#include "viz/MotionField.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/OutputProbe.hpp"
#include "viz/Overlays.hpp"
#include "viz/Params.hpp"
#include "viz/PcmHandoff.hpp"
#include "viz/ProgramBinaryCache.hpp"
#include "viz/ProgramStager.hpp"
#include "viz/Reactions.hpp"
#include "viz/Scene.hpp"
#include "viz/ScenePrep.hpp"
#include "viz/SceneRegistry.hpp"
#include "viz/SceneResidency.hpp"
#include "viz/StyleSchedule.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/ThermalGovernor.hpp"
#include "viz/TouchField.hpp"
#include "viz/TrailPass.hpp"
#include "viz/UiBlurPass.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/liquid/LiquidMath.hpp"
#include "viz/liquid/LiquidPass.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// Port of VisualizerRenderer.kt's frame graph: scene pass -> trail -> composite -> target.
class Renderer {
public:
    static constexpr float kTimeWrapSeconds = 7100.0f;

    Renderer(AAssetManager* assets, std::string cacheDir);
    ~Renderer();

    // Any thread.
    void setParams(const SceneParams& params);
    void setReducedMotion(bool on) { reducedMotion_.store(on, std::memory_order_relaxed); }
    // Settings › Performance, live surfaces only: an offscreen (export) renderer never calls this,
    // so it keeps full quality. renderScale and marchScale are 0..1 multipliers stacked under the
    // thermal tier; postFx = false drops the same optional passes a hot tier does, plus fluid bloom.
    void setUserQuality(float renderScale, float marchScale, bool postFx, bool adaptive);
    // Export (geode_viz_set_offscreen): every scene is built whole on the frame that first asks for
    // it, so no exported frame ever shows the previous scene or a clear in its place. A live renderer
    // (the default) builds a new scene across frames instead; see ScenePrep.hpp.
    void setBlockingBuilds(bool on) { blockingBuilds_.store(on, std::memory_order_relaxed); }
    // Export (geode_viz_set_offscreen): never draws the UI blur, whatever region it was given.
    void setOffscreen(bool on) { offscreen_.store(on, std::memory_order_relaxed); }
    // Export: step every frame by exactly 1/fps (viz/FrameStep); 0 measures the frame time, as live does.
    void setFixedFrameRate(int fps) { fixedFps_.store(fps > 0 ? fps : 0, std::memory_order_relaxed); }
    // The app's glass sheet over the live visual (UiBlurPass): latched for the next frame. Any thread.
    void setUiBlurRegion(const UiBlurRegion& region);
    // Settings › Performance › Liquid on a hot phone (liquid::HotPolicy). Any thread.
    void setLiquidHotPolicy(int policy) { liquidHotPolicy_.store(policy, std::memory_order_relaxed); }
    // What Liquid mode ran last frame (liquid::Mode), so Customize can say when a hot phone simplified
    // or paused it. Any thread.
    int liquidMode() const { return liquidMode_.load(std::memory_order_relaxed); }
    // An export's own auto-director: once pinned, this renderer stops reading the process-wide switch
    // (director::global), so the live player moving on to another track, or past the free taste, never
    // reaches a render in progress. The export hands over the play's seed, so it makes the same film.
    void pinDirector(float amount, uint64_t seed);
    // Customize › Layers is process-wide (viz/LayerState, geode_layer_set): every renderer follows it.
    // An export pins the layer it started with (geode_viz_set_offscreen), so a live edit during a
    // background render never reaches the video; off follows the store again. Any thread.
    void pinLayer(bool on);
    void setTransition(const std::string& id, int64_t durationMs);
    void beginParamMorph(float seconds);
    void submitTouchPoints(const float* xy, int n) { touchField_.submit(xy, n); }
    void pushPcm(const float* samples, int count);
    // The sound engine's write side: interleaved PCM in play order, every sample once. A new rate builds a
    // new engine. Call from the render thread before render(), live and in export alike, so the frame the
    // engine is stepped into covers exactly the audio written up to that frame.
    void writeAudio(const float* interleaved, int frames, int channels, int sampleRateHz);
    // A seek, track change or source switch: drop the engine's history.
    void resetAudio();
    // Settings › Audio: kept across engine rebuilds (a new sample rate builds a new engine).
    void setSoundTuning(const GeodeSoundTuning& tuning);
    void queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength);
    void setFluidInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc);
    void loadMilkPreset(const std::string& path);
    void reloadMilkPreset();
    void setMilkTextureDir(const std::string& dir);
    // The path of the preset the MilkDrop scene last compiled, handed out once.
    std::string takeMilkPresetLoaded();
    // How many times the director committed a look at a new section or a drop since the last take.
    // The shell's Auto changes style on each (one Auto mode: the director picks the moments).
    int takeDirectorStyleMoments() { return directorStyleMoments_.exchange(0, std::memory_order_acq_rel); }
    void setLfoConfigs(const std::array<LfoConfig, LfoEngine::kSlots>& configs);
    void setAdsrConfigs(const std::array<AdsrConfig, AdsrEngine::kCount>& configs);
    void setReactionRoutes(const std::array<ReactRoute, ReactionEngine::kRoutes>& routes);
    // Gyro look, -1..1 per axis; read by the camera only while camGyro is on.
    void setLook(float yaw, float pitch) {
        lookYaw_.store(yaw, std::memory_order_relaxed);
        lookPitch_.store(pitch, std::memory_order_relaxed);
    }
    // W00: the full-frame underlay layer, latched here and uploaded to a GL texture at the
    // start of the next frame; see CompositePass.hpp for the pass itself. `pixels` is Android's
    // Bitmap.getPixels ARGB layout; null (or a non-positive size) clears the layer.
    void setUnderlayRgba(const uint32_t* pixels, int width, int height, int blend, float amount);
    ThermalGovernor& thermal() { return thermal_; }
    // Any thread: returns a copy (see Renderer.cpp) since fail() mutates lastError_ concurrently.
    std::string lastError() const;
    bool knows(const std::string& sceneId) const { return registry_.knows(sceneId); }
    std::vector<std::string> availableSceneIds() const { return registry_.availableIds(); }

    // GL thread.
    void onSurfaceCreated();
    void surfaceChanged(int width, int height);
    bool setScene(const std::string& sceneId);
    // Export (Studio 2D): the styles this segment switches to, by render time and by beat
    // (StyleSchedule.hpp). Latched; the next frame starts it from its beginning. Empty clears it.
    // Styles in `lockedIds` (the entitlement) are dropped from it.
    void setStyleSchedule(std::vector<StyleCue> cues, std::vector<std::string> beatCutIds, int everyBeats,
                          const std::vector<std::string>& lockedIds);
    // Called on the GL thread, inside beginFrame, each time the style schedule itself switches the scene
    // (a take's timed switch or a beat cut), after setScene accepted it and before this frame's reaction
    // matrix ticks (#771). The schedule is the one switch the renderer makes on its own; every other one
    // comes in through setScene, so its owner already knows. geode_viz sets it once, before any frame, so
    // the routes follow the new style's DNA on the switch frame. Null (the default) calls nothing.
    using ScheduledSceneHook = void (*)(void* context, const std::string& sceneId);
    void setScheduledSceneHook(ScheduledSceneHook hook, void* context) {
        scheduledSceneHook_ = hook;
        scheduledSceneContext_ = context;
    }
    void warmTransition(const std::string& id);
    void cut();
    void render(double timeSeconds, GLuint targetFbo);
    void releaseScenes();
    const GlProfile& profile() const { return profile_; }

private:
    struct Live {
        std::unique_ptr<Scene> scene;
        SceneParams params;
    };

    Scene* sceneFor(const std::string& id);
    Scene* readyScene(const std::string& id);
    Scene* readySecondary(const std::string& id, const std::string& other);
    Scene* attemptScene(const std::string& id);
    Scene* builtScene(const std::string& id);
    // Releases a built scene nothing is drawing (Liquid mode's last born style); a no-op otherwise.
    void dropScene(const std::string& id);
    // A scene just built: recorded as the most recent, then the spares over the tier's budget go (#378).
    void admitScene(const std::string& id, const Scene& scene);
    // Releases the built scenes SceneResidency says are over budget; `keepId` is pinned beside the
    // ones in use this frame.
    void trimScenes(const std::string& keepId);
    std::unique_ptr<Scene> buildScene(const std::string& id);
    float beginFrame(double timeSeconds);
    // Steps styleSchedule_ once per frame (after the sound engine) and requests its change, if any.
    void applyStyleSchedule();
    void applyRenderScale();
    static float supersampleFactor(int width, int height);
    Scene* resolveActiveScene();
    SceneParams resolveParams(float dt);
    void resolveLayerScene();
    // beginFrame, under stateLock_: takes the layer layerFollower_ hands over when it changed.
    void syncLayerLocked();
    bool ensureTargets();
    float drawSecondaryTargets(const SceneParams& p, float dt);
    void bindSecondaryTarget();
    void drawSceneTarget(Scene& scene, const SceneParams& p, float dt);
    void composite(const SceneParams& p, float progress, GLuint targetFbo, GLuint liquidTex);
    // Feeds the landed output measurements to the flash limiter and takes this frame's grade.
    void stepFlashLimiter(float dt);
    // After the composite: the UI blur region, live surfaces only; nothing at strength 0.
    void drawUiBlur(GLuint targetFbo);
    void deliverPcm(Scene& scene);
    void stepOverlays(Scene& scene, const SceneParams& p, float dt);
    void stepCamera(const SceneParams& p, float dt);
    void wireFlow(Scene& target, const SceneParams& p);
    // Liquid mode (viz/liquid): the gestures, the flow field and the dive, after the touch field steps.
    void stepLiquid(Scene& scene, const SceneParams& p, float dt);
    // What forms at the vanishing point this frame; the scene target itself for Born = Live.
    GLuint drawBorn(Scene& scene, const SceneParams& p, float dt);
    // Runs the liquid over the scene target; 0 when Liquid is off (the composite then reads the target).
    GLuint drawLiquid(Scene& scene, const SceneParams& p, float dt);
    bool liquidOn() const { return liveLiquid_ != liquid::Mode::Off && liveLiquid_ != liquid::Mode::Paused; }
    void applyPendingFluidInjection();
    void applyMilkRequests();
    void applyUnderlayUpload();
    void notePresetLoaded(const std::string& path);
    static double monotonicSeconds();
    void fail(const std::string& message);
    // Re-applies the process-wide shader overrides (viz/ShaderOverrides) to resident scenes when they change.
    void syncShaderOverrides();

    ShaderSource assets_;
    std::string cacheDir_;
    ProgramBinaryCache programCache_;
    DeviceGl deviceGl_;
    GlProfile profile_ = GlProfile::unprobed();
    ProgramLoader loader_{assets_, &programCache_};
    // Staged scene builds (live renderers only): the programs being compiled for prep_'s target.
    ProgramStager stager_{&programCache_};
    ScenePrep prep_;
    std::atomic<bool> blockingBuilds_{false};
    // While a staged attempt runs, scene errors wait here: a deferred attempt's errors are about
    // placeholders and are dropped; a whole one's are reported as an inline build would have.
    bool holdingErrors_ = false;
    std::vector<std::string> heldErrors_;
    // The requested scene is still being staged this frame, so the layer scene waits its turn.
    bool requestedPending_ = false;
    SceneRegistry registry_;
    Overlays overlays_{loader_};
    std::vector<std::pair<std::string, std::unique_ptr<Scene>>> scenes_;
    // Which of scenes_ stay resident, and which were sized before the last tier or scale change
    // (viz/SceneResidency). sizeStamp_ moves on every applyRenderScale.
    SceneResidency residency_;
    uint32_t sizeStamp_ = 0;
    std::string quadVert_;

    TouchField touchField_;
    TrailPass trailPass_;
    CompositePass compositePass_;
    CompositePass::Inputs compositeInputs_;
    // The photosensitive flash limit (FlashLimiter.hpp): the probe measures each finished frame, the
    // limiter reads those one or two frames late and grades the next frame. Its own clock (the sum of
    // frame dts) never wraps or steps back, unlike timeSeconds_.
    OutputProbe outputProbe_;
    safety::FlashLimiter flashLimiter_;
    safety::Grade flashGrade_;
    double safetyClock_ = 0.0;
    ThermalGovernor thermal_;
    LfoEngine lfo_;
    AdsrEngine adsr_;
    // Wave three's continuous replacement for FormDrive: the one stage every
    // family's SceneParams pass through so nothing can key off a drum hit.
    MotionField motionField_;
    ReactionEngine reactions_;
    ReactionEngine macroReactions_;
    // The auto-director (viz/Director): the music's feeling deciding palette, behaviour and routing.
    Director director_;
    // Set by pinDirector; read once a frame on the GL thread.
    std::atomic<bool> directorPinned_{false};
    std::atomic<int> directorStyleMoments_{0};
    int directorCommitsSeen_ = 0;  // GL thread only
    std::atomic<float> pinnedDirectorAmount_{0.0f};
    std::atomic<uint64_t> pinnedDirectorSeed_{0};
    // Note & key reactions (viz/NoteResponse), after the director's look: knobs, note colours, tint.
    NoteResponse notes_;
    // Frosted glass under the app's sheet. The region is guarded by stateLock_; uiBlurOn_ lets a frame
    // with no sheet skip the lock.
    UiBlurPass uiBlurPass_;
    UiBlurRegion uiBlurRegion_;
    std::atomic<bool> uiBlurOn_{false};
    std::atomic<bool> offscreen_{false};
    std::atomic<int> fixedFps_{0};
    // The note layer's seed: the director's play seed mixed with fresh entropy whenever that seed changes,
    // so every play (and every export) varies on its own, as the user chose (NoteResponse's class comment).
    uint64_t noteSeed(uint64_t playSeed);
    uint64_t notePlaySeed_ = 0;
    uint64_t noteEntropy_ = 0;
    bool noteSeeded_ = false;
    CameraRig camera_;
    // One engine per renderer (one per surface), so each is frame-locked to its own dt. Guarded by soundLock_
    // only because writeAudio and soundFrame are public; in practice one thread does everything.
    mutable std::mutex soundLock_;
    std::unique_ptr<sound::SoundEngine> sound_;
    GeodeSoundTuning soundTuning_ = sound::SoundEngine::defaultTuning();
    GeodeSoundFrame soundFrame_{};
    // Customize › Macros: this frame's knobs, and the ones the sound engine's tuning was last built from.
    MacroKnobs frameKnobs_{};
    // The user's knobs with the director's laid over them, this frame; what the params and the camera read.
    MacroKnobs directedKnobs_{};
    MacroKnobs tunedKnobs_{};
    std::atomic<float> lookYaw_{0.0f};
    std::atomic<float> lookPitch_{0.0f};
    Framebuffer fboA_{"sceneA"};
    Framebuffer fboB_{"sceneB"};
    GLuint quadVao_ = 0;
    GLuint paletteLutTex_ = 0;

    mutable std::mutex stateLock_;
    SceneParams requestedParams_;
    std::string requestedSceneId_ = "neon_chrome_sea"; // the app's first style (FreeTier.FIRST_STYLE_ID)
    std::string layerSceneId_;
    float layerMix_ = 0.5f;
    int layerBlend_ = static_cast<int>(BlendMode::Screen);
    // Under stateLock_: this renderer's view of viz/LayerState (followed live, pinned in an export).
    layer_state::Follower layerFollower_;
    std::string transitionId_ = "fade";
    int64_t transitionDurationMs_ = 1200;
    std::atomic<bool> reducedMotion_{false};
    std::atomic<float> userScale_{1.0f};
    std::atomic<float> marchScale_{1.0f};
    std::atomic<bool> userPostFx_{true};
    float appliedUserScale_ = 1.0f;
    // SceneRegistry::spinRate of the requested scene, set by setScene so the
    // frame never compares ids.
    std::atomic<float> sceneSpin_{0.0f};
    float morphFadeSec_ = 0.0f;
    float morphRemainSec_ = 0.0f;
    PcmHandoff pcm_;
    int framePcmCount_ = 0;  // GL thread: samples of pcmDeliverScratch_ this frame's scenes get.
    std::vector<float> pcmDeliverScratch_;
    // Drained by beginFrame; nothing queues into it since the overrides went process-wide (#364), and
    // the frame code that drains it is being moved (#344), which drops it.
    std::vector<std::pair<std::string, std::string>> pendingShaders_;
    // GL thread: the shader override each built scene was last given ("" = its built-in source), and
    // the ShaderOverrides generation syncShaderOverrides last applied.
    std::map<std::string, std::string> appliedShaders_;
    uint64_t shaderGeneration_ = 0;
    std::string fluidForceSrc_;
    std::string fluidDyeSrc_;
    bool fluidInjectionDirty_ = false;
    // W00: the underlay layer. Retained (not just "pending") so a surface recreation can
    // re-arm the dirty flag and re-upload, the same way fluidForceSrc_/fluidDyeSrc_ do above;
    // see onSurfaceCreated() and applyUnderlayUpload(). Empty = cleared.
    std::vector<uint32_t> underlayPixels_;
    int underlayWidth_ = 0;
    int underlayHeight_ = 0;
    int underlayBlend_ = 0;
    float underlayAmount_ = 0.0f;
    bool underlayDirty_ = false;
    std::string lastMilkPreset_;
    // lastMilkPreset_ is the latest request, which may have failed; this is the latest preset that
    // compiled. A rebuilt scene is seeded with both (buildScene).
    std::string lastGoodMilkPreset_;
    std::string milkPresetRequest_;
    // setStyleSchedule's latest schedule, moved into styleSchedule_ by the GL thread (stateLock_).
    std::optional<StyleSchedule> styleScheduleRequest_;
    StyleSchedule styleSchedule_;
    // setScheduledSceneHook's; set before the GL thread runs a frame, then only read there.
    ScheduledSceneHook scheduledSceneHook_ = nullptr;
    void* scheduledSceneContext_ = nullptr;
    bool milkReloadRequested_ = false;
    std::string milkTextureDir_;
    bool milkTextureDirDirty_ = false;
    std::string milkPresetLoaded_;
    // Liquid mode. The dive and the born clock are reseeded from the device's entropy each time Liquid
    // is switched on: truly random each play, the user's call, so an export does not replay the screen.
    liquid::LiquidPass liquidPass_;
    liquid::Dive liquidDive_;
    liquid::DiveFrame liquidFrame_;
    liquid::Gestures liquidGestures_;
    liquid::BornClock bornClock_;
    liquid::Mode liveLiquid_ = liquid::Mode::Off;
    Framebuffer bornFbo_{"born"};
    SceneParams bornParams_;
    bool bornRolled_ = false;
    std::string bornSceneId_;
    std::string lastBornSceneId_;
    // The secondary targets' staging asks (#369): this frame's layer, and the next style's this frame
    // and the frame before.
    std::string layerAsked_;
    std::string bornAsked_;
    std::string bornAskedBefore_;
    bool liquidWasOn_ = false;
    std::atomic<int> liquidHotPolicy_{0};
    std::atomic<int> liquidMode_{0};
    bool rippleOverlayOn_ = false;
    bool smearing_ = false;

    SceneParams displayedParams_;
    SceneParams lastFinalParams_;
    std::array<float, LfoEngine::kSlots> envRate_{};
    // The envelopes' and LFOs' luminance modulation this frame, for the flash limiter (resolveParams).
    LuminanceDrive luminanceDrive_{};
    std::array<float, LfoEngine::kSlots> envDepth_{};
    float postRotationAngle_ = 0.0f;
    float postCyclePhase_ = 0.0f;
    float postBeatPulse_ = 0.0f;
    Scene* activeScene_ = nullptr;
    Scene* outgoingScene_ = nullptr;
    Scene* layerScene_ = nullptr;
    std::optional<SceneParams> outgoingParams_;
    double transitionStartS_ = 0.0;
    bool sceneJustSwitched_ = false;
    int width_ = 1;
    int height_ = 1;
    int renderWidth_ = 1;
    int renderHeight_ = 1;
    ThermalTier appliedTier_ = ThermalTier::Full;
    double lastTimeS_ = -1.0;
    double frameNowS_ = 0.0;
    float timeSeconds_ = 0.0f;
    // Latched once per frame in beginFrame, so the
    // rest of the frame (composite(), drawSecondaryTargets(), ...) reads a
    // stable snapshot instead of racing pinLayer()/setTransition() on
    // stateLock_.
    float frameLayerMix_ = 0.5f;
    int frameLayerBlend_ = static_cast<int>(BlendMode::Screen);
    std::string frameTransitionId_ = "fade";
    int64_t frameTransitionDurationMs_ = 1200;
    std::string lastError_;
};

}  // namespace geode::viz
