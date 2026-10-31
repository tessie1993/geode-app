#include "viz/scenes/AcidScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {

// lib_relief's looks (RELIEF_* in lib_relief.glsl); the show pass takes them as ints.
enum ReliefLook : int { kSatin = 0, kGlass = 1, kChrome = 2, kNeon = 3, kTerra = 4, kMembrane = 5, kNebula = 6 };

constexpr int kRecipes = 10;

// What a hard cut switches each recipe to: a neighbour that keeps the style's character (the TV's
// liquid fold becomes a mirror room, the well becomes a spiral throat, the two glitch recipes swap)
// so a new section reads as the same style turning a corner, not as a different style.
constexpr std::array<int, kRecipes> kAltMode = {8, 3, 8, 1, 9, 6, 5, 2, 2, 4};
// Each recipe's two materials, alternating on hard cuts (lib_relief's reliefLook, on the same
// parity of the cut count as kAltMode, so the recipe and the material turn together).
constexpr std::array<int, kRecipes> kLookBase = {kNeon, kGlass, kChrome, kMembrane, kGlass, kNeon, kTerra, kChrome, kChrome, kSatin};
constexpr std::array<int, kRecipes> kLookAlt = {kChrome, kMembrane, kNeon, kNebula, kSatin, kTerra, kNeon, kNebula, kGlass, kMembrane};
// Relief thickness in uv: the tunnels (well, droste) carve deepest, the CRT and glitch recipes stay
// shallow so their scanlines and blocks keep their edges.
constexpr std::array<float, kRecipes> kDepth = {0.05f, 0.07f, 0.055f, 0.07f, 0.05f, 0.04f, 0.035f, 0.055f, 0.06f, 0.05f};

size_t recipeIndex(int mode) { return static_cast<size_t>(std::clamp(mode, 0, kRecipes - 1)); }

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

// Exponential ease toward a target at `rate` per second.
float ease(float current, float target, float rate, float dt) { return current + (target - current) * std::min(rate * dt, 1.0f); }

float hash01(float n) {
    const float x = std::sin(n * 12.9898f) * 43758.5453f;
    return x - std::floor(x);
}

}  // namespace

int AcidScene::altMode(int mode) { return kAltMode[recipeIndex(mode)]; }
int AcidScene::lookBase(int mode) { return kLookBase[recipeIndex(mode)]; }
int AcidScene::lookAlt(int mode) { return kLookAlt[recipeIndex(mode)]; }
float AcidScene::reliefDepth(int mode) { return kDepth[recipeIndex(mode)]; }

void AcidScene::init() {
    step_ = UniformCache(0);
    show_ = UniformCache(0);
    vao_ = 0;
    programOk_ = false;
    state_.reset();
    std::string error;
    const std::string quad = loader_.source("quad_vert.glsl", &error);
    const GLuint step = loader_.buildSource(quad, loader_.source("acid_step_frag.glsl", &error), &error);
    if (step == 0) {
        host_.onShaderError("Acid unavailable on this GPU: " + error);
        return;
    }
    step_ = UniformCache(step);
    const GLuint show = loader_.buildSource(quad, loader_.source("acid_show_frag.glsl", &error), &error);
    if (show == 0) {
        host_.onShaderError("Acid unavailable on this GPU: " + error);
        return;
    }
    show_ = UniformCache(show);
    glGenVertexArrays(1, &vao_);
    sound_.init();
    programOk_ = true;
}

void AcidScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (state_) state_->release();
    state_.reset();
}

void AcidScene::update(const GeodeSoundFrame& features, float dt) {
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    sound_.step(features, dt);
    conduct(features, std::clamp(dt, 0.0f, 1.0f / 15.0f));
    last_ = features;
}

fluid::DoubleFbo* AcidScene::ensureState() {
    if (state_) return &*state_;
    const auto [w, h] = fluid::resolution(kSimRes, width_, height_);
    state_.emplace(w, h, fluid::kRgba8, true);
    state_->create();
    if (state_->ok()) return &*state_;
    state_->release();
    state_.reset();
    return nullptr;
}

// The musical state of the loop, stepped once per sound frame. Everything kept here is an eased
// value or a counter, so the draw can read it once per rendered frame without losing a one-frame
// trigger, even when two updates land before a draw.
void AcidScene::conduct(const GeodeSoundFrame& f, float dt) {
    const SceneParams& p = params_;
    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float beatResponse = std::clamp(p.beatResponse, 0.0f, 2.0f);

    envBass_ = slewEnvelope(envBass_, clampedBand(live::bass(f)), dt, kEnvRisePerSec, kEnvFallPerSec);
    envMid_ = slewEnvelope(envMid_, clampedBand(live::mid(f)), dt, kEnvRisePerSec, kEnvFallPerSec);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(live::treble(f)), dt, kEnvRisePerSec, kEnvFallPerSec);
    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * beatResponse, beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
    if (hit * beatResponse > kGlitchThreshold) {
        glitch_ = 1.0f;
        glitchEpoch_ = std::fmod(glitchEpoch_ + 1.0f, 1024.0f);
    }
    glitch_ = std::max(glitch_ - dt * kGlitchDecay, 0.0f);
    fillSpokes(f.spectrum);

    // The sound clock. curvedTime runs at vol_att^2.5 (quiet barely moves it, loud races), so its
    // rate is how hard the music is driving right now. Every term added is >= 0: the spin and the
    // warps' drift speed up and slow down with the song but never run backwards. A wrap or a seek
    // (the clock jumping back) reads as rate 0 for that one frame.
    const float curved = finiteOr(f.curvedTime, 0.0f);
    float rate = 0.0f;
    if (lastCurved_ >= 0.0f && curved >= lastCurved_ && dt > 0.0f) rate = std::min((curved - lastCurved_) / dt, 3.0f);
    lastCurved_ = curved;
    spinRate_ = ease(spinRate_, rate, 3.0f, dt);
    clock_ = std::fmod(clock_ + dt * speed * (0.12f + spinRate_), kSceneTimeWrapSeconds);

    // Silence and a return both settle the echo: presence fades out over ~2 s of silence, and a
    // held "returned" keeps the first seconds of the music back gently instead of slamming in.
    const float returned = std::clamp(sound_.signals().value(ReactSource::Returned), 0.0f, 1.0f);
    const float calmTarget = std::max(1.0f - std::clamp(f.presence, 0.0f, 1.0f), 0.8f * returned);
    calm_ = ease(calm_, calmTarget, 1.5f, dt);
    const float motion = 1.0f - 0.75f * calm_;

    // The kick toggle reverses the local vortex; the eased toggle (~60 ms) is slowed further so the
    // whirlpool swings round over a third of a second instead of flipping in one frame.
    twistDir_ = ease(twistDir_, 2.0f * std::clamp(f.kickToggle, 0.0f, 1.0f) - 1.0f, 5.0f, dt);

    // Counted kicks: every 4th lunges the zoom, every 8th surges the spin, every 16th swaps the
    // source recipe and nudges the palette. Bar-sized moves on top of the per-kick punch.
    if (f.kick4 > 0.0f) lunge4_ = 1.0f;
    if (f.kick8 > 0.0f) lunge8_ = 1.0f;
    if (f.kick16 > 0.0f) {
        kick16Count_ = (kick16Count_ + 1) % 2;
        hueTarget_ += 0.12f;
    }
    lunge4_ = std::max(lunge4_ - dt * 2.0f, 0.0f);
    lunge8_ = std::max(lunge8_ - dt * 1.4f, 0.0f);

    // The vanishing point: each kick's random re-aims it sideways, the melody's height lifts it
    // (weighted by how sure the pitch is), and calm draws it home to the centre.
    const float conf = std::clamp(f.pitchConfidence, 0.0f, 1.0f);
    const float targetX = (std::clamp(f.kickRandom, 0.0f, 1.0f) - 0.5f) * 0.36f * motion;
    const float targetY = (std::clamp(f.pitchHeight, 0.0f, 1.0f) - 0.5f) * 0.45f * conf * motion;
    centreX_ = ease(centreX_, targetX, 2.5f, dt);
    centreY_ = ease(centreY_, targetY, 1.8f, dt);
    melodyAmt_ = ease(melodyAmt_, conf > 0.35f ? conf : 0.0f, 4.0f, dt);

    // Hue: a note change steps it in the interval's direction (bigger leaps step further), a hard
    // cut jumps it a quarter to half a turn, a different jump per section. Both glide.
    if (f.noteChange > 0.0f) {
        const float interval = std::clamp(finiteOr(f.interval, 0.0f), -1.0f, 1.0f);
        hueTarget_ += (interval >= 0.0f ? 1.0f : -1.0f) * (0.035f + 0.1f * std::fabs(interval));
    }
    if (f.hardCut > 0.0f) hueTarget_ += 0.22f + 0.28f * hash01(sound_.signals().cutCount() + 1.0f);
    hue_ = ease(hue_, hueTarget_, 2.5f, dt);
    if (std::fabs(hueTarget_) > 16.0f) {
        // Whole turns: the shader takes fract, so shifting both keeps the glide seamless.
        const float shift = hueTarget_ > 0.0f ? 16.0f : -16.0f;
        hueTarget_ -= shift;
        hue_ -= shift;
    }

    // Echo length follows the energy trend: a build lengthens the tails, a drop-off shortens them.
    const float trend = std::clamp(finiteOr(f.energyTrend, 0.5f), 0.0f, 1.5f);
    feedbackTrim_ = ease(feedbackTrim_, std::clamp((trend - 0.5f) * 0.08f, -0.03f, 0.015f), 1.0f, dt);
    drift_ = ease(drift_, std::clamp(finiteOr(f.stereoPan, 0.0f), -1.0f, 1.0f), 2.0f, dt);
    wobble_ = ease(wobble_, std::clamp(finiteOr(f.glide + f.vibrato, 0.0f), 0.0f, 1.0f), 4.0f, dt);
}

void AcidScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    fluid::DoubleFbo* loop = ensureState();
    if (!loop) return;
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeSoundFrame& f = last_;
    const ReactSignals& sig = sound_.signals();

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float beatResponse = std::clamp(p.beatResponse, 0.0f, 2.0f);
    const float drive = safeAudioDrive(p.audioDrive);
    const float motion = 1.0f - 0.75f * calm_;
    const float kickEnv = std::clamp(f.kickEnv, 0.0f, 1.0f) * beatResponse;
    const float snareEnv = std::clamp(f.snareEnv, 0.0f, 1.0f) * beatResponse;
    const float hatEnv = std::clamp(f.hatEnv, 0.0f, 1.0f) * beatResponse;
    const float surge = std::clamp(sig.value(ReactSource::Surge), 0.0f, 1.0f);
    const float cutHeld = std::clamp(sig.value(ReactSource::HardCut), 0.0f, 1.0f);
    // Odd sections run the recipe's alternate; the show pass's reliefLook reads the same parity.
    const bool altSection = (static_cast<int>(sig.cutCount()) % 2) == 1;
    const int mode = altSection ? altMode(style_.mode) : style_.mode;
    const int source = (style_.source + (kick16Count_ != 0 ? 2 : 0)) % 4;

    // Per-frame moves, scaled by frames so they hold at any refresh rate. The zoom punches on the
    // kick and lunges on every 4th, a surge pushes the tunnel in and a hard cut throws it.
    const float frames = dt * 60.0f;
    const float zoomStep = 1.0f + (style_.zoom - 1.0f) * motion +
        (0.03f * kickEnv + 0.025f * lunge4_ * beatResponse + 0.02f * surge + 0.012f * cutHeld) * motion;
    const float zoom = std::pow(std::clamp(zoomStep, 0.97f, 1.08f), frames);
    // Spin: the style's rate times the sound clock's rate, plus the snare's kick and every 8th
    // kick's surge, all in the style's own direction, so the frame never turns backwards.
    const float spinSign = style_.rotate < 0.0f ? -1.0f : 1.0f;
    const float rotate = (style_.rotate * speed * (0.35f + 0.65f * std::min(spinRate_, 2.0f)) +
                          spinSign * (0.022f * snareEnv + 0.03f * lunge8_ * beatResponse)) *
        motion * frames;
    const float twist = twistDir_ * (0.004f + 0.03f * kickEnv) * motion * frames;
    const float hueShift = style_.hueRate * dt * speed * (0.5f + 0.5f * std::min(spinRate_, 2.0f));
    const float feedback = std::pow(std::min(style_.feedback + feedbackTrim_, kFeedbackCap), frames);
    const float flux = std::clamp(finiteOr(f.flux, 1.0f), 0.0f, 2.0f);
    const float modulate = style_.modulate * (0.5f + 0.5f * flux);
    const float liquid = style_.liquid + std::clamp(p.turbulence, 0.0f, 1.0f) * 0.6f + 0.5f * wobble_;
    const float baseHue = hue::base(p.paletteBase()) + style_.hueOffset + hue_;
    const float hueSpan = hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan;
    const float pitchClass = std::clamp(f.pitchClass, 0.0f, 1.0f);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);

    glUseProgram(step_.program());
    glBindFramebuffer(GL_FRAMEBUFFER, loop->write().fbo());
    glViewport(0, 0, loop->width(), loop->height());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, loop->read().tex());
    glUniform1i(step_.loc("uPrev"), 0);
    glUniform2f(step_.loc("uRes"), static_cast<float>(loop->width()), static_cast<float>(loop->height()));
    glUniform1i(step_.loc("uStyle"), mode);
    glUniform1i(step_.loc("uSource"), source);
    glUniform1f(step_.loc("uZoom"), zoom);
    glUniform1f(step_.loc("uRotate"), rotate);
    glUniform1f(step_.loc("uHueShift"), hueShift);
    glUniform1f(step_.loc("uFeedback"), feedback);
    glUniform1f(step_.loc("uModulate"), modulate);
    glUniform1f(step_.loc("uGlitch"), glitch_ * style_.glitch);
    glUniform1f(step_.loc("uEpoch"), glitchEpoch_);
    glUniform1f(step_.loc("uClock"), clock_);
    glUniform1f(step_.loc("uBass"), envBass_);
    glUniform1f(step_.loc("uMid"), envMid_);
    glUniform1f(step_.loc("uTreble"), envTreble_);
    glUniform1f(step_.loc("uBeat"), beatPulse_);
    glUniform1f(step_.loc("uStrike"), std::clamp(pcmStrike_, 0.0f, 1.5f));
    glUniform1f(step_.loc("uDrive"), drive);
    glUniform1fv(step_.loc("uSpokes"), kSpokes, spokes_.data());
    glUniform1f(step_.loc("uBaseHue"), baseHue);
    glUniform1f(step_.loc("uHueSpan"), hueSpan);
    glUniform1f(step_.loc("uLiquid"), liquid);
    glUniform2f(step_.loc("uCentre"), centreX_, centreY_);
    glUniform1f(step_.loc("uTwist"), twist);
    glUniform1f(step_.loc("uShimmer"), 0.004f * hatEnv * motion);
    glUniform2f(step_.loc("uDrift"), -drift_ * 0.0025f * frames * motion, 0.0f);
    glUniform1f(step_.loc("uMirror"), std::clamp(0.75f * snareEnv, 0.0f, 1.0f));
    glUniform1f(step_.loc("uMirrorSide"), f.snareToggle > 0.5f ? 1.0f : -1.0f);
    glUniform1f(step_.loc("uWobble"), wobble_);
    glUniform1f(step_.loc("uKickEnv"), std::min(kickEnv, 1.0f));
    glUniform1f(step_.loc("uSnareEnv"), std::min(snareEnv, 1.0f));
    glUniform1f(step_.loc("uHatEnv"), std::min(hatEnv, 1.0f));
    glUniform1f(step_.loc("uSeed"), sig.onsetCount());
    glUniform4f(step_.loc("uMelody"), pitchClass, std::clamp(f.pitchHeight, 0.0f, 1.0f), melodyAmt_ * 0.9f,
                baseHue + pitchClass * hueSpan);
    uploadSceneTouch(step_, touch_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    loop->swap();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(show_.program());
    // The relief reads the whole sound frame (lib_sound) with its texture on unit 3; the state
    // stays on unit 0, bound after the upload so the active unit is ours again.
    sound_.upload(show_, drive, 3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, loop->read().tex());
    glUniform1i(show_.loc("uState"), 0);
    glUniform2f(show_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1f(show_.loc("uScanline"), style_.scanline);
    glUniform1f(show_.loc("uCurve"), style_.curve);
    glUniform1f(show_.loc("uSat"), style_.saturation);
    glUniform1f(show_.loc("uFloorHue"), baseHue);
    glUniform1f(show_.loc("uOverdrive"), style_.overdrive);
    glUniform1f(show_.loc("uHit"),
                std::clamp(std::max(kickEnv, std::clamp(pcmStrike_, 0.0f, 1.5f) * 0.6f + 0.4f * beatPulse_), 0.0f, 1.0f));
    glUniform1f(show_.loc("uDepth"), reliefDepth(style_.mode));
    glUniform1i(show_.loc("uLookBase"), lookBase(style_.mode));
    glUniform1i(show_.loc("uLookAlt"), lookAlt(style_.mode));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void AcidScene::fillSpokes(const float* bands) {
    for (int i = 0; i < kSpokes; ++i) {
        const int from = i * GEODE_SOUND_SPECTRUM_POINTS / kSpokes;
        const int to = std::max(std::min((i + 1) * GEODE_SOUND_SPECTRUM_POINTS / kSpokes, GEODE_SOUND_SPECTRUM_POINTS), from + 1);
        float acc = 0.0f;
        for (int b = from; b < to; ++b) acc += bands[b];
        spokes_[static_cast<size_t>(i)] = std::clamp(acc / static_cast<float>(to - from), 0.0f, 1.0f);
    }
}

void AcidScene::release() {
    for (UniformCache* c : {&step_, &show_}) {
        if (c->program() != 0) glDeleteProgram(c->program());
        *c = UniformCache(0);
    }
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    sound_.release();
    if (state_) state_->release();
    state_.reset();
    vao_ = 0;
    programOk_ = false;
}

}  // namespace geode::viz
