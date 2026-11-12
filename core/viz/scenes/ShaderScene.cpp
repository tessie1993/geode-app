#include "viz/scenes/ShaderScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "util/Log.hpp"
#include "viz/Catalogue.hpp"
#include "viz/Colour.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/world/WorldAssets.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "ShaderScene";
constexpr int kPaletteRows = 5;
constexpr int kMarchMaxSteps = 128;
constexpr int kMarchFloorSteps = 64;
// The Detail range is the catalogue's: the shell's slider quotes the same numbers.
constexpr float kMarchMinDetail = catalogue::kMarchDetail.min;
constexpr float kMarchMaxDetail = catalogue::kMarchDetail.max;
// The fewest steps Settings › Performance may scale a march down to; below this most SDFs miss surfaces.
constexpr float kMarchMinScaledSteps = 24.0f;
// The length of lib_scene_uniforms' uKnob array: six knobs, k0 PULSE to k5 SPACE.
constexpr int kKnobCount = 6;

float paletteRowCoordinate(int index) {
    return (static_cast<float>(std::clamp(index, 0, kPaletteRows - 1)) + 0.5f) / static_cast<float>(kPaletteRows);
}

float flag(bool on) { return on ? 1.0f : 0.0f; }

}  // namespace

ShaderScene::ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host)
    : id_(std::move(id)),
      vertexSrc_(std::move(vertexSrc)),
      currentFragment_(std::move(fragmentSrc)),
      stockFragment_(currentFragment_),
      cache_(cache),
      host_(std::move(host)) {
    pendingFragment_ = currentFragment_;
    if (id_ == catalogue::kMorphWorld) world_ = std::make_unique<world::MorphDriver>();
    if (id_ == catalogue::kPool) pool_ = std::make_unique<pool::Pool>();
}

ShaderScene::~ShaderScene() { release(); }

void ShaderScene::setFragmentSource(const std::string& source) {
    std::lock_guard<std::mutex> lock(pendingLock_);
    // "" is a cleared shader override (viz/ShaderOverrides): back to the built-in style.
    pendingFragment_ = source.empty() ? stockFragment_ : source;
}

void ShaderScene::setWorldAssets(const std::string& shapes, const std::string& maps) {
    if (shapes.size() != world::kShapeBytes || maps.size() != world::kTexBytes) return;
    world::setShapeVolumes(std::vector<uint8_t>(shapes.begin(), shapes.end()));
    worldShapes_ = shapes;
    worldMaps_ = maps;
}

void ShaderScene::init() {
    motionField_.setVariant(styleVariantSeed(id_));
    program_ = 0;
    uniforms_ = UniformCache(0);
    worldParts_ = 0;
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pendingFragment_ = currentFragment_;
    }
    glGenVertexArrays(1, &vao_);
    sound_.init();
}

void ShaderScene::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void ShaderScene::update(const GeodeSoundFrame& sound, float dt) {
    sound_.step(sound, dt);
    const SceneParams& p = params_;
    shaderTime_ = std::fmod(shaderTime_ + p.speed * dt, kTimeWrapSeconds);
    rotationAngle_ = std::fmod(rotationAngle_ + p.rotation * dt, kTwoPi);
    if (p.colorCycle) cyclePhase_ = std::fmod(cyclePhase_ + p.cycleSpeed * dt, 1.0f);
    // Wave three: the continuous motion layer, never a transient/beat trigger.
    motionField_.step(sound, dt, p.speed);
    const float soundTimes[3] = {sound.bassTime, sound.midTime, sound.trebleTime};
    for (int b = 0; b < 3; ++b) soundTimeSped_[b].step(soundTimes[b], p.speed, kTimeWrapSeconds);
    if (pool_) {
        std::array<float, pool::kMaxFingers * 2> fingers{};
        int down = 0;
        if (touchField_) {
            const auto& pts = touchField_->points();
            for (int i = 0; i < TouchField::kMaxPoints && down < pool::kMaxFingers; i++) {
                const size_t base = static_cast<size_t>(i * TouchField::kPointStride);
                // A lifted finger fades out of the field; only one still down stirs the water.
                if (pts[base + 2] < 1.0f) continue;
                fingers[static_cast<size_t>(down * 2)] = pts[base];
                fingers[static_cast<size_t>(down * 2 + 1)] = pts[base + 1];
                down++;
            }
        }
        pool_->step(dt, poolControls(), fingers.data(), down, static_cast<float>(width_) / static_cast<float>(std::max(height_, 1)));
    }
    if (world_) {
        // A new seed is a new play: the world starts over from one of its seeds.
        const uint64_t seed = host_.playSeed ? host_.playSeed() : 0;
        if (!worldSeeded_ || seed != world_->seedValue()) {
            world_->reset(seed);
            worldSeeded_ = true;
        }
        world::DriverInput in = world::DriverInput::fromSound(sound, p.audioDrive);
        in.morph = p.morph;
        in.warp = p.warp;
        in.turbulence = p.turbulence;
        in.detail = p.marchDetail;
        in.intensity = p.intensity;
        if (const NoteUniforms* notes = host_.notes ? host_.notes() : nullptr) {
            std::copy(notes->knobs.begin(), notes->knobs.end(), in.knobs.begin());
        }
        world_->step(in, dt);
        worldDt_ = dt;
    }
}

void ShaderScene::draw(float timeSeconds) {
    (void) timeSeconds;
    compilePendingIfAny();
    if (program_ == 0) return;
    specializeWorld();
    glDisable(GL_BLEND);
    glUseProgram(program_);
    uploadParams();
    glUniform2f(uniforms_.loc("uResolution"), static_cast<float>(width_), static_cast<float>(height_));
    const SceneParams& p = params_;
    // The look's palette stack draws through the colour map's path: one ramp, read along pal(t).
    const GLuint ramp = host_.paletteRamp ? host_.paletteRamp() : 0;
    const GLuint lut = ramp != 0 ? ramp : paletteLutTex_;
    const bool lutSelected = ramp != 0 || (p.paletteLut >= 0 && paletteLutTex_ != 0);
    if (lut != 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lut);
        glUniform1i(uniforms_.loc("uPalLut"), 2);
        glActiveTexture(GL_TEXTURE0);
    }
    set1f("uPalLutMix", flag(lutSelected));
    set1f("uPalLutRow", ramp != 0 ? 0.5f : paletteRowCoordinate(std::max(p.paletteLut, 0)));
    const float marchScale = host_.marchScale ? host_.marchScale() : 1.0f;
    set1f("uSteps", std::max(std::round(marchSteps(p.marchDetail) * marchScale), kMarchMinScaledSteps));
    uploadMotion();
    uploadTouch();
    uploadCamera();
    uploadWorld();
    uploadPool();
    uploadField();
    // The whole GeodeSoundFrame for lib_sound; its texture takes unit 3 (1 and 2 are the flow and the palette LUT).
    sound_.upload(uniforms_, p.audioDrive);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void ShaderScene::uploadParams() {
    const SceneParams& p = params_;
    set1f("uTime", shaderTime_);
    set1f("uZoom", p.zoom);
    set1f("uRotation", rotationAngle_);
    // The bare phase, for user-written styles that still spell their own zoom; no bundled style reads it.
    set1f("uColorShift", p.colorShift + cyclePhase_ + p.paletteShift);
    set1f("uHueRange", p.hueRange);
    set1f("uSat", p.saturation);
    set1f("uBright", p.brightness);
    set1f("uInvert", flag(p.invert));
    set1f("uIntensity", p.intensity);
    set1f("uMirrorX", flag(p.mirror));
    set1f("uTurbulence", p.turbulence);
    set1f("uPalBase", p.paletteBase());
    set1f("uPalRange", p.paletteRange());
    set1f("uPal2Base", p.palette2Base());
    set1f("uPal2Range", p.palette2Range());
    set1f("uPaletteMix", p.paletteMix);
    {
        const auto stops = p.paletteStops();
        const int count = p.paletteStopCount();
        std::array<float, 3 * colour::kMaxStops> rgb{};
        for (int i = 0; i < count; ++i) {
            const colour::Rgb c = colour::unpack(stops[static_cast<size_t>(i)]);
            rgb[static_cast<size_t>(3 * i)] = c.r;
            rgb[static_cast<size_t>(3 * i + 1)] = c.g;
            rgb[static_cast<size_t>(3 * i + 2)] = c.b;
        }
        set1f("uPalStopCount", static_cast<float>(count));
        glUniform3fv(uniforms_.loc("uPalStops"), colour::kMaxStops, rgb.data());
    }
    set1f("uDuotone", flag(p.duotone));
    set1f("uBloom", p.bloom);
    set1f("uWarp", p.warp);
    set1f("uRipple", p.ripple);
    {
        // The resolved (modulated) folds, rounded natively to a whole count (SceneParams::kaleidoUniforms).
        const SceneParams::KaleidoUniforms k = SceneParams::kaleidoUniforms(p.kaleidoFolds);
        set1f("uSymmetry", k.folds);
        set1f("uKaleido", k.on);
    }
    set1f("uMorph", p.morph);
    set1f("uPixelate", p.pixelate);
    set1f("uPosterize", p.posterize);
    set1f("uSway", p.sway);
    set1f("uDriftX", p.driftX);
    set1f("uDriftY", p.driftY);
    set1f("uTile", p.tile);
    set1f("uTwist", p.twist);
    set1f("uTemperature", p.temperature);
    set1f("uSolarize", flag(p.solarize));
    set1f("uContrast", p.contrast);
    set1f("uGamma", p.gamma);
    // The style knobs (lib_scene_uniforms' uKnob): driven by meaning from the
    // music by Note & key reactions (viz/NoteResponse) when that is on, else
    // held at the midpoint each style was tuned around (NoteUniforms' default),
    // so with the switch off every style draws exactly as it did. Only as many
    // elements as survived linking are written (uploading past the live length
    // is an INVALID_OPERATION), and a style that reads none gets -1 and a no-op.
    static const NoteUniforms kResting{};
    const NoteUniforms* notes = host_.notes ? host_.notes() : nullptr;
    if (!notes) notes = &kResting;
    static_assert(NoteUniforms::kKnobs == kKnobCount, "uKnob is six knobs");
    glUniform1fv(uniforms_.loc("uKnob"), uniforms_.arrayCount("uKnob", kKnobCount), notes->knobs.data());
}

// Every style is handed these; the ones that read none of them link them away
// and glUniform on a -1 location is a no-op, so this costs nothing on a style
// that has not been converted.
void ShaderScene::uploadMotion() {
    // Wave three: the continuous replacement for the spike-latched state.
    // Nothing here is a trigger; every value is a running average, a
    // phase-locked oscillator gated by confidence, or a slow re-target that
    // eases over seconds. See viz/MotionField.hpp for the derivation.
    const MotionField::State& m = motionField_.state();
    set1f("uEnergyRel", m.energyRel);
    set1f("uBassRel", m.bassRel);
    set1f("uMidRel", m.midRel);
    set1f("uTrebRel", m.trebRel);
    set1f("uMotionBright", m.bright);
    set1f("uHarmony", m.harmony);
    set1f("uKeyHue", m.keyHue);
    set1f("uKeyStrength", m.keyStrength);
    set1f("uBarOsc", m.barOsc);
    glUniform2f(uniforms_.loc("uOrbit"), m.orbitX, m.orbitY);
    set1f("uDrift", m.drift);
    set1f("uBreath", m.breath);
    set1f("uFlowPhase", m.flowPhase);
    set1f("uFlowTime", m.flowTime);
    glUniform3f(uniforms_.loc("uSoundTimeSped"), soundTimeSped_[0].value, soundTimeSped_[1].value, soundTimeSped_[2].value);

    // Legacy since wave three, held at their neutral constant until R08
    // deletes the readers: a spawn born once and never re-rolled, a direction
    // that never turns.
    set1f("uSpawnSeed", 0.0f);
    set1f("uSpawnAge", 1000.0f);
    set1f("uFormPhase", 0.0f);
    glUniform2f(uniforms_.loc("uMoveDir"), 1.0f, 0.0f);
}

void ShaderScene::uploadTouch() {
    if (touchField_) {
        touchAnchor_ = {touchField_->anchorX(), touchField_->anchorY(), touchField_->anchorStrength(), touchField_->anchorAge()};
        touchPoints_ = touchField_->points();
    } else {
        touchAnchor_.fill(0.0f);
        touchPoints_.fill(0.0f);
    }
    glUniform4fv(uniforms_.loc("uTouchAnchor"), 1, touchAnchor_.data());
    // Only as many elements as survived linking: uploading past the live length is an INVALID_OPERATION.
    glUniform4fv(uniforms_.loc("uTouchPoints"), uniforms_.arrayCount("uTouchPoints", TouchField::kMaxPoints), touchPoints_.data());
    glUniform1i(uniforms_.loc("uTouchCount"), touchField_ ? touchField_->count() : 0);
    glUniform1i(uniforms_.loc("uTouchGesture"), touchField_ ? touchField_->gesture() : TouchField::kGestureNone);
    glUniform2f(uniforms_.loc("uTouchAxis"), touchField_ ? touchField_->axisX() : 0.0f, touchField_ ? touchField_->axisY() : 0.0f);
    set1f("uTouchSpin", touchField_ ? touchField_->spin() : 0.0f);
}

void ShaderScene::uploadCamera() {
    if (camera_) {
        const CameraPose& c = *camera_;
        const float basis[9] = {c.right[0], c.right[1], c.right[2], c.up[0], c.up[1], c.up[2], c.fwd[0], c.fwd[1], c.fwd[2]};
        glUniform3fv(uniforms_.loc("uCamPos"), 1, c.pos.data());
        glUniformMatrix3fv(uniforms_.loc("uCamBasis"), 1, GL_FALSE, basis);
        set1f("uCamFocal", c.focal);
        set1f("uCamTravel", c.travel);
        set1f("uCamDolly", c.dolly);
    }
}

void ShaderScene::uploadWorld() {
    if (!world_) return;
    if (worldShapesTex_ == 0 && !worldShapes_.empty()) {
        // The slabs stacked in z would be 32 x 32 x 768, over the 256 GLES 3.0 promises; the atlas
        // (WorldAssets::packAtlas, 192 x 128 x 32) fits any device. Should a driver still report
        // less, leave the assets unbound: lib_world draws its sphere (WG_ASSETS 0), not garbage.
        GLint max3d = 0;
        glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
        const std::vector<uint8_t> atlas = world::packAtlas(std::vector<uint8_t>(worldShapes_.begin(), worldShapes_.end()));
        if (max3d < world::atlasMaxDimension() || atlas.empty()) {
            GEODE_LOGW(kTag, "Morph World shapes need a %d^3 texture, this device allows %d: drawing spheres",
                       world::atlasMaxDimension(), static_cast<int>(max3d));
            worldShapes_.clear();
            worldMaps_.clear();
            world_->setAssetsBound(false);
        } else {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glGenTextures(1, &worldShapesTex_);
            glBindTexture(GL_TEXTURE_3D, worldShapesTex_);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, world::kAtlasWidth, world::kAtlasHeight, world::kAtlasDepth, 0, GL_RED,
                         GL_UNSIGNED_BYTE, atlas.data());
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenTextures(1, &worldMapsTex_);
            glBindTexture(GL_TEXTURE_2D_ARRAY, worldMapsTex_);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, world::kTexRes, world::kTexRes, world::kTexCount, 0, GL_RED,
                         GL_UNSIGNED_BYTE, worldMaps_.data());
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            world_->setAssetsBound(worldShapesTex_ != 0 && worldMapsTex_ != 0);
        }
    }
    if (worldShapesTex_ != 0) {
        // Units 4 and 5: 1 is the flow, 2 the palette LUT, 3 the sound texture.
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_3D, worldShapesTex_);
        glUniform1i(uniforms_.loc("uWorldShapes"), 4);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, worldMapsTex_);
        glUniform1i(uniforms_.loc("uWorldTex"), 5);
        glActiveTexture(GL_TEXTURE0);
    }
    // The rig was stepped before this draw: the world's z phase and wake follow the eye.
    if (camera_) world_->travel(camera_->pos, worldDt_);
    worldDt_ = 0.0f;
    const auto& u = world_->uniforms();
    glUniform4fv(uniforms_.loc("uWorld"), uniforms_.arrayCount("uWorld", world::kVec4), u.data());
}

pool::Controls ShaderScene::poolControls() const {
    const SceneParams& p = params_;
    pool::Controls c;
    c.morph = p.morph;
    c.warp = p.warp;
    c.turbulence = p.turbulence;
    c.intensity = p.intensity;
    c.speed = p.speed;
    if (const NoteUniforms* notes = host_.notes ? host_.notes() : nullptr) std::copy(notes->knobs.begin(), notes->knobs.end(), c.knobs.begin());
    return c;
}

void ShaderScene::uploadPool() {
    if (!pool_) return;
    const pool::Surface s = pool_->surface(poolControls());
    static_assert(sizeof(pool::Wave) == 4 * sizeof(float) && sizeof(pool::Ring) == 4 * sizeof(float) &&
                  sizeof(pool::Wake) == 4 * sizeof(float));
    glUniform4fv(uniforms_.loc("uPoolWaves"), uniforms_.arrayCount("uPoolWaves", pool::kWaves), &s.waves[0].kx);
    glUniform4fv(uniforms_.loc("uPoolRings"), uniforms_.arrayCount("uPoolRings", pool::kRings), &s.rings[0].x);
    glUniform4fv(uniforms_.loc("uPoolWakes"), uniforms_.arrayCount("uPoolWakes", pool::kMaxWakes), &s.wakes[0].x);
    glUniform4f(uniforms_.loc("uPoolLook"), s.look.refraction, s.look.focus, s.look.caustics, s.look.shade);
    glUniform4f(uniforms_.loc("uPoolView"), s.view.sinTilt, s.view.cosTilt, s.view.height, s.view.fov);
}

void ShaderScene::uploadField() {
    if (!field_ || field_->texture == 0) return;
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, field_->texture);
    glUniform1i(uniforms_.loc("uField"), 4);
    glActiveTexture(GL_TEXTURE0);
    glUniform2fv(uniforms_.loc("uFieldRes"), 1, field_->size.data());
    glUniform4fv(uniforms_.loc("uFieldGeom"), 1, field_->geom.data());
    glUniform4fv(uniforms_.loc("uFieldShape"), 1, field_->shape.data());
    glUniform4fv(uniforms_.loc("uFieldLook"), uniforms_.arrayCount("uFieldLook", kFieldLooks), field_->look.data());
}

void ShaderScene::specializeWorld() {
    // A user's shader override keeps its own source.
    if (!world_ || currentFragment_ != stockFragment_) return;
    const uint32_t want = world_->parts();
    if (want == world::MorphDriver::kAllParts || (worldParts_ != 0 && (want & ~worldParts_) == 0)) return;
    // Built here, in the frame that retargeted, before it draws: an export renders the same frames, and
    // a retarget's new parts are never missing from the frame that first needs them.
    std::string error;
    const GLuint built = program::build(vertexSrc_, world::withParts(stockFragment_, want), cache_, &error);
    // On a failure the stock program, which has every part, keeps drawing; marking the parts done
    // keeps a broken driver from being asked again every frame.
    worldParts_ = want;
    if (built == 0) {
        GEODE_LOGW(kTag, "Morph World parts %u did not build, drawing every part: %s", want, error.c_str());
        return;
    }
    glDeleteProgram(program_);
    program_ = built;
    uniforms_ = UniformCache(built);
    drawsCamera_ = uniforms_.loc("uCamBasis") >= 0;
}

void ShaderScene::set1f(const char* name, float value) {
    glUniform1f(uniforms_.loc(name), value);
}

float ShaderScene::marchSteps(float detail) {
    if (detail != stepsDetail_) {
        stepsDetail_ = detail;
        const float t = std::clamp((detail - kMarchMinDetail) / (kMarchMaxDetail - kMarchMinDetail), 0.0f, 1.0f);
        stepsBudget_ = static_cast<float>(std::lround(kMarchFloorSteps + (kMarchMaxSteps - kMarchFloorSteps) * t));
    }
    return stepsBudget_;
}

void ShaderScene::compilePendingIfAny() {
    std::optional<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pending.swap(pendingFragment_);
    }
    if (!pending) return;
    std::string error;
    const GLuint built = program::build(vertexSrc_, *pending, cache_, &error);
    if (built == 0) {
        host_.onShaderError(error);
        return;
    }
    if (program_ != 0) glDeleteProgram(program_);
    program_ = built;
    uniforms_ = UniformCache(built);
    drawsCamera_ = uniforms_.loc("uCamBasis") >= 0;
    worldParts_ = 0;
    currentFragment_ = *pending;
    host_.onShaderError("");
}

void ShaderScene::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    sound_.release();
    if (worldShapesTex_ != 0) glDeleteTextures(1, &worldShapesTex_);
    if (worldMapsTex_ != 0) glDeleteTextures(1, &worldMapsTex_);
    worldShapesTex_ = 0;
    worldMapsTex_ = 0;
    if (world_) world_->setAssetsBound(false);
    program_ = 0;
    vao_ = 0;
    uniforms_ = UniformCache(0);
}

}  // namespace geode::viz
