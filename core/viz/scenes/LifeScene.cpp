#include "viz/scenes/LifeScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "viz/Quad.hpp"
#include "viz/Reactions.hpp"

namespace geode::viz {

namespace {
constexpr int kCensusProbes[5][2] = {{1, 2}, {3, 2}, {2, 1}, {2, 3}, {2, 2}};

// lib_relief's materials (RELIEF_* in the shader).
enum LifeMaterial { kSatin = 0, kGlass = 1, kChrome = 2, kNeon = 3, kTerra = 4, kMembrane = 5, kNebula = 6, kInk = 7 };

// Each style's own look: the young-matter material and the old-matter (soma) one, which trade places on
// every hard cut, plus how deep the relief is. The three Gray-Scott alternatives are the style's other
// rule presets (Karl Sims' parameterization, Du 1, Dv 0.5): known living (f, k) pairs near its own, so a
// morph between them transforms the pattern rather than killing it (and the census reseeds if it does).
struct LifeLook {
    const char* id;
    int young;
    int old;
    float depth;
    float gs[3][2];
};

constexpr LifeLook kLooks[] = {
    {"life_orbium", kMembrane, kNeon, 0.06f, {}},
    {"life_gyre", kGlass, kChrome, 0.05f, {}},
    {"life_helix", kSatin, kMembrane, 0.055f, {}},
    {"life_pulsar", kNeon, kNebula, 0.05f, {}},
    {"life_hydro", kGlass, kMembrane, 0.065f, {}},
    {"life_bug", kChrome, kTerra, 0.045f, {}},
    {"life_mitosis", kMembrane, kGlass, 0.06f, {{0.035f, 0.065f}, {0.037f, 0.06f}, {0.039f, 0.058f}}},
    {"life_coral", kTerra, kSatin, 0.07f, {{0.062f, 0.0609f}, {0.042f, 0.059f}, {0.039f, 0.058f}}},
    {"life_labyrinth", kInk, kTerra, 0.04f, {{0.029f, 0.057f}, {0.034f, 0.056f}, {0.022f, 0.051f}}},
    {"life_worms", kNebula, kNeon, 0.05f, {{0.062f, 0.0609f}, {0.0545f, 0.062f}, {0.046f, 0.0594f}}},
};

const LifeLook* lookFor(const char* id) {
    for (const auto& look : kLooks) {
        if (std::strcmp(look.id, id) == 0) return &look;
    }
    return nullptr;
}

// Lenia presets as (mu, sigma) multipliers on the species: wider survival grows fatter, rounder
// organisms, narrower thinner and more nervous ones. Kept close: a Lenia species far off its
// parameters dies, and the point is the same creature in another mood.
constexpr float kLeniaPresets[4][2] = {{1.0f, 1.0f}, {1.03f, 1.22f}, {0.97f, 0.88f}, {1.07f, 1.12f}};

float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
float ease(float current, float target, float dt, float seconds) { return current + (target - current) * (1.0f - std::exp(-dt / seconds)); }
}  // namespace

LifeScene::Rule LifeScene::preset(int index) const {
    const int i = ((index % kPresets) + kPresets) % kPresets;
    Rule r{style_.mu, style_.sigma, style_.feed, style_.kill};
    if (style_.rule == 0) {
        r.mu *= kLeniaPresets[i][0];
        r.sigma *= kLeniaPresets[i][1];
    } else if (i > 0) {
        if (const LifeLook* look = lookFor(style_.id)) {
            r.feed = look->gs[i - 1][0];
            r.kill = look->gs[i - 1][1];
        }
    }
    return r;
}

void LifeScene::init() {
    stepProgram_ = 0;
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    formats_.reset();
    state_.reset();
    preset_ = 0;
    rule_ = preset(0);
    live_ = rule_;
    std::string error;
    const std::string quad = loader_.source("quad_vert.glsl", &error);
    stepProgram_ = loader_.buildSource(quad, loader_.source("life_step_frag.glsl", &error), &error);
    if (stepProgram_ == 0) {
        host_.onShaderError("Life unavailable on this GPU: " + error);
        return;
    }
    showProgram_ = loader_.buildSource(quad, loader_.source("life_show_frag.glsl", &error), &error);
    if (showProgram_ == 0) {
        host_.onShaderError("Life unavailable on this GPU: " + error);
        return;
    }
    stepLocs_ = UniformCache(stepProgram_);
    showLocs_ = UniformCache(showProgram_);
    glGenVertexArrays(1, &vao_);
    sound_.init();
    programOk_ = true;
    seedRemain_ = kSeedSeconds;
}

void LifeScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (state_) state_->release();
    state_.reset();
    seedRemain_ = kSeedSeconds;
}

void LifeScene::update(const GeodeSoundFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    sound_.step(features, dt);
    steer(features, std::clamp(dt, 0.0f, 1.0f / 15.0f));
}

// Folds one sound frame into the solver's inputs. Accumulates (feeds, drift) rather than overwrites, so
// two updates between draws lose nothing.
void LifeScene::steer(const GeodeSoundFrame& f, float dt) {
    const SceneParams& p = params_;
    const float drive = safeAudioDrive(p.audioDrive);
    const float beat = std::clamp(p.beatResponse, 0.0f, 2.0f);
    const float surge = clamp01(sound_.signals().value(ReactSource::Surge));

    // Quiet = still, busy = squirming: the solver's time rate follows presence (which fades out over
    // ~2 s of silence) times how busy the music is (level, spectral change, note starts).
    const float busy = clamp01((0.45f * clamp01(f.level) + 0.3f * clamp01(f.flux * 0.5f) + 0.25f * clamp01(f.onsetEnv)) * drive);
    const float rateTarget = kStillRate + (1.0f - kStillRate) * clamp01(f.presence) * clamp01(0.3f + 0.7f * busy + 0.3f * surge);
    rate_ = ease(rate_, rateTarget, dt, kRateSeconds);

    // Drums. Each kick steps the birth site round a golden spiral, kick4 throws it across the field,
    // the kick toggle mirrors it left/right and the kick random sets its reach and blob size. The
    // matter lands in one lump at the envelope's peak: Lenia eats a lump spread over frames before it
    // can grow into an organism.
    if (f.kick > 0.0f) {
        kickAngle_ = std::fmod(kickAngle_ + kGoldenAngle * (f.kick4 > 0.0f ? 3.0f : 1.0f), kTwoPi);
        const float reach = 0.14f + 0.24f * clamp01(f.kickRandom);
        const float side = f.kickToggle > 0.5f ? -1.0f : 1.0f;
        kickX_ = 0.5f + side * reach * std::cos(kickAngle_);
        kickY_ = 0.5f + reach * std::sin(kickAngle_);
        kickSize_ = 0.7f + 0.8f * clamp01(f.kickRandom);
        const float punch = std::max(clamp01(f.kick), clamp01(f.kickEnv));
        kickFeed_ = std::max(kickFeed_, (0.4f + 0.6f * punch) * drive * beat);
    }
    // Every 8th kick the drift turns a quarter; every 16th the rule steps to the next preset.
    if (f.kick8 > 0.0f) headingTarget_ += 0.25f * kTwoPi;
    if (f.kick16 > 0.0f) preset_ = (preset_ + 1) % kPresets;
    // A hard cut (new section): another preset (never the current one), a fresh culture scattered over
    // the old one, and the drift swings. The present pass swaps its materials on the same count.
    if (f.hardCut > 0.0f) {
        const int cuts = static_cast<int>(sound_.signals().cutCount());
        preset_ = (preset_ + 1 + cuts % (kPresets - 1)) % kPresets;
        seedRemain_ = kSeedSeconds;
        seedSalt_ = std::fmod(seedSalt_ + 17.31f, 100.0f);
        headingTarget_ += kGoldenAngle;
    }
    // Hats are spores: fine noise, sparse, on the hat envelope plus a little standing treble.
    sprinkle_ = std::max(sprinkle_, (clamp01(f.hatEnv) + 0.25f * clamp01(f.treblePresence) * clamp01(f.high) + 0.4f * pcmStrike_) * drive);

    // Melody. The bud site follows the lead: pitch class across, pitch height up. A struck note jumps
    // there and buds; a legato change or a glide slides it, dragging a trail of matter. New matter is
    // stamped with the melody's ink (state alpha), which the present pass turns into hue, so colonies
    // carry the colour of the line that grew them.
    const float conf = clamp01(f.pitchConfidence);
    if (conf > 0.2f) {
        const float tx = 0.12f + 0.76f * clamp01(f.pitchClass);
        const float ty = 0.15f + 0.7f * clamp01(f.pitchHeight);
        const bool struck = (f.noteChange > 0.0f && f.legato <= 0.0f) || f.reattack > 0.0f;
        budX_ = struck ? tx : ease(budX_, tx, dt, 0.3f);
        budY_ = struck ? ty : ease(budY_, ty, dt, 0.3f);
        if (struck) budFeed_ = std::max(budFeed_, 0.55f * conf * drive);
        budFeed_ += clamp01(f.glide) * conf * dt * 3.0f * drive;
        ink_ = ease(ink_, 0.7f * clamp01(f.pitchHeight) + 0.3f * clamp01(f.pitchClass), dt, 0.4f);
    }

    // The rule breathes: the preset morphs in, the snare opens the birth window for a moment (a bloom
    // per backbeat), a surge widens it for its two seconds.
    const Rule target = preset(preset_);
    rule_.mu = ease(rule_.mu, target.mu, dt, kMorphSeconds);
    rule_.sigma = ease(rule_.sigma, target.sigma, dt, kMorphSeconds);
    rule_.feed = ease(rule_.feed, target.feed, dt, kMorphSeconds);
    rule_.kill = ease(rule_.kill, target.kill, dt, kMorphSeconds);
    const float snare = clamp01(f.snareEnv) * std::min(drive, 1.5f);
    live_ = rule_;
    live_.mu *= 1.0f - 0.05f * snare;
    live_.sigma *= 1.0f + 0.3f * snare + 0.2f * surge;
    live_.feed += 0.003f * snare + 0.003f * surge;
    live_.kill -= 0.002f * snare;

    // Drift on the curved clock (barely moves in quiet passages, races in loud ones). Only its forward
    // steps count, so the field never runs backwards, and the wrap at an hour is one skipped frame.
    const float curved = std::isfinite(f.curvedTime) ? f.curvedTime : lastCurved_;
    const float advance = clockPrimed_ ? std::clamp(curved - lastCurved_, 0.0f, 0.25f) : 0.0f;
    lastCurved_ = curved;
    clockPrimed_ = true;
    heading_ = ease(heading_, headingTarget_, dt, 1.2f);
    const float texels = advance * kDriftTexelsPerClock * std::clamp(p.speed, 0.05f, 4.0f);
    driftX_ += std::cos(heading_) * texels;
    driftY_ += std::sin(heading_) * texels;
    if (std::fabs(headingTarget_) > 1000.0f) {
        const float wrapped = std::fmod(headingTarget_, kTwoPi);
        heading_ += wrapped - headingTarget_;
        headingTarget_ = wrapped;
    }
}

fluid::DoubleFbo* LifeScene::ensureState() {
    if (state_) return &*state_;
    if (!formats_) formats_ = fluid::probeFormats();
    byteState_ = !formats_->ok;
    const fluid::TexFormat texFmt = byteState_ ? fluid::kRgba8 : formats_->rgba;
    const auto [w, h] = fluid::resolution(kSimRes, width_, height_);
    state_.emplace(w, h, texFmt, true);
    state_->create();
    if (state_->ok()) {
        // A torus: the drift carries organisms off one edge and back in at the other, and the kernel
        // sees whole neighbourhoods at the border instead of a smeared clamp.
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, state_->read().tex());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            state_->swap();
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        return &*state_;
    }
    state_->release();
    state_.reset();
    return nullptr;
}

void LifeScene::census(fluid::DoubleFbo& field, GLint restoreFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, field.read().fbo());
    float maxA = 0.0f;
    float maxV = 0.0f;
    float minLive = std::numeric_limits<float>::max();
    bool sane = true;
    for (const auto& probe : kCensusProbes) {
        const int x = field.width() * probe[0] / 4;
        const int y = field.height() * probe[1] / 4;
        float a = 0.0f;
        float v = 0.0f;
        if (byteState_) {
            unsigned char bytes[4] = {0, 0, 0, 0};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
            a = static_cast<float>(bytes[0]) / 255.0f;
            v = static_cast<float>(bytes[1]) / 255.0f;
        } else {
            float texel[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, texel);
            a = texel[0];
            v = texel[1];
        }
        sane = sane && std::isfinite(a) && std::isfinite(v);
        maxA = std::max(maxA, a);
        maxV = std::max(maxV, v);
        minLive = std::min(minLive, style_.rule == 0 ? a : v);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(restoreFbo));
    if (!sane) return;
    const float maxLive = style_.rule == 0 ? maxA : maxV;
    const bool starving = maxLive < kStarved && (style_.rule == 1 ? maxA > 0.9f : true);
    if (starving || minLive > kOvergrown) {
        seedRemain_ = kSeedSeconds;
        seedSalt_ = std::fmod(seedSalt_ + 5.17f, 100.0f);
    }
}

void LifeScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    fluid::DoubleFbo* field = ensureState();
    if (!field) return;
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const float drive = safeAudioDrive(p.audioDrive);

    seedRemain_ = std::max(seedRemain_ - dt, 0.0f);
    censusAge_ += dt;
    if (censusAge_ >= kCensusSeconds) {
        censusAge_ = 0.0f;
        census(*field, prevFbo);
    }

    // Whole texels of drift go into the solver (an exact shift, no smear); the remainder is drawn by
    // the present pass, so the motion is smooth at any speed.
    const float shiftX = std::trunc(driftX_);
    const float shiftY = std::trunc(driftY_);
    driftX_ -= shiftX;
    driftY_ -= shiftY;

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const int substeps = std::clamp(static_cast<int>(std::lround(style_.substeps * speed)), 1, 8);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);

    glUseProgram(stepProgram_);
    glViewport(0, 0, field->width(), field->height());
    glUniform2f(stepLocs_.loc("uRes"), static_cast<float>(field->width()), static_cast<float>(field->height()));
    glUniform1i(stepLocs_.loc("uRule"), style_.rule);
    // The rate scales the step, never the stability limit: Gray-Scott runs at dt <= 1 (Du 1 is stable
    // to 1.25 on this stencil), Lenia's 1/T only gets finer.
    glUniform1f(stepLocs_.loc("uDt"), style_.dt * rate_);
    glUniform1f(stepLocs_.loc("uAgeRate"), rate_);
    glUniform1i(stepLocs_.loc("uCore"), style_.core);
    glUniform1i(stepLocs_.loc("uGrowth"), style_.growth);
    glUniform1f(stepLocs_.loc("uMu"), live_.mu);
    glUniform1f(stepLocs_.loc("uSigma"), std::max(live_.sigma, 1e-3f));
    glUniform1f(stepLocs_.loc("uRadius"), style_.radius);
    glUniform1i(stepLocs_.loc("uRings"), style_.rings);
    glUniform3f(stepLocs_.loc("uB"), style_.b1, style_.b2, style_.b3);
    glUniform1f(stepLocs_.loc("uF"), std::max(live_.feed, 0.0f));
    glUniform1f(stepLocs_.loc("uK"), std::max(live_.kill, 0.0f));
    glUniform2f(stepLocs_.loc("uDiff"), 1.0f, 0.5f);
    glUniform1f(stepLocs_.loc("uAniso"), style_.aniso);
    glUniform1f(stepLocs_.loc("uSeedJitter"), style_.seedJitter);
    glUniform1f(stepLocs_.loc("uSeedSalt"), seedSalt_);
    glUniform1f(stepLocs_.loc("uTime"), time_);
    glUniform2f(stepLocs_.loc("uKickPos"), kickX_, kickY_);
    glUniform1f(stepLocs_.loc("uKickSize"), kickSize_);
    glUniform2f(stepLocs_.loc("uBudPos"), budX_, budY_);
    glUniform1f(stepLocs_.loc("uInk"), ink_);
    for (int pass = 0; pass < substeps; ++pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, field->write().fbo());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, field->read().tex());
        glUniform1i(stepLocs_.loc("uPrev"), 0);
        // Injections and the drift land once per frame, not once per substep.
        const bool first = pass == 0;
        glUniform2f(stepLocs_.loc("uShift"), first ? shiftX : 0.0f, first ? shiftY : 0.0f);
        glUniform1f(stepLocs_.loc("uSeed"), first ? seedRemain_ / kSeedSeconds : 0.0f);
        glUniform1f(stepLocs_.loc("uKick"), first ? std::min(kickFeed_, 1.5f) : 0.0f);
        glUniform1f(stepLocs_.loc("uBud"), first ? std::min(budFeed_, 1.0f) : 0.0f);
        glUniform1f(stepLocs_.loc("uSprinkle"), first ? sprinkle_ : 0.0f);
        uploadSceneTouch(stepLocs_, touch_, first);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        field->swap();
    }
    kickFeed_ = 0.0f;
    budFeed_ = 0.0f;
    sprinkle_ = 0.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(showProgram_);
    sound_.upload(showLocs_, drive, 3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, field->read().tex());
    glUniform1i(showLocs_.loc("uState"), 0);
    glUniform2f(showLocs_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform2f(showLocs_.loc("uSimRes"), static_cast<float>(field->width()), static_cast<float>(field->height()));
    glUniform2f(showLocs_.loc("uFrac"), driftX_, driftY_);
    const LifeLook* look = lookFor(style_.id);
    const int fallbackYoung = style_.rule == 0 ? kMembrane : kTerra;
    glUniform1i(showLocs_.loc("uLookYoung"), look ? look->young : fallbackYoung);
    glUniform1i(showLocs_.loc("uLookOld"), look ? look->old : kGlass);
    glUniform1f(showLocs_.loc("uDepth"), look ? look->depth : 0.05f);
    glUniform1f(showLocs_.loc("uShowV"), style_.rule == 1 ? 1.0f : 0.0f);
    glUniform1f(showLocs_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(showLocs_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void LifeScene::release() {
    if (stepProgram_ != 0) glDeleteProgram(stepProgram_);
    if (showProgram_ != 0) glDeleteProgram(showProgram_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (state_) state_->release();
    state_.reset();
    formats_.reset();
    sound_.release();
    stepProgram_ = 0;
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    stepLocs_ = UniformCache(0);
    showLocs_ = UniformCache(0);
}

}  // namespace geode::viz
