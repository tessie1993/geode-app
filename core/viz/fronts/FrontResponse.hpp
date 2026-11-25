#pragma once
#include <array>
#include <cmath>

#include "viz/fronts/FrontField.hpp"
#include "viz/modulation/ResponsePlace.hpp"
#include "viz/modulation/Responder.hpp"

namespace geode::viz::fronts {

// The Event Responder's Fronts kind (responder::kFront): a response of this kind sends fronts across the picture.
//
// Its kind-params (Response::params):
//   P0 shape    0 Line, 1 Ring, 2 Wedge
//   P1 effect   0 Brighten, 1 Colour shift, 2 Bend, 3 Invert, 4 Reveal
//   P2 origin   0 middle, 1 random (seeded), 2 pitch height, 3 stereo pan, 4 from the left edge, 5 right, 6 bottom,
//               7 top (an edge front enters there and travels across, square to it)
//   P3 speed    frame heights a second (0 reads 0.5)
//   P4 size     the band's half width, frame heights (0 reads 0.05)
//   P5 count    fronts one fire sends, 1..kMaxFronts (0 reads 1)
// A front's strength is the response's strength after its curve; its life is fade in + hold + fade out (never
// under kMinLife); its palette position is drawn from the response's seed, over the whole palette. A Line or Wedge
// from a point (not an edge) travels along a seeded direction. A While response plays as Once: a front has its own
// life. Drive Follow sends one front at a time, at a rate set by the level (the cap: responder::kFollowMaxPerSecond).
class FrontKind final : public responder::ResponseKindHandler {
public:
    enum Param { kShape = 0, kEffect = 1, kOrigin = 2, kSpeed = 3, kSize = 4, kCount = 5 };
    static constexpr float kDefaultSpeed = 0.5f;
    static constexpr float kDefaultWidth = 0.05f;

    explicit FrontKind(FrontField& fronts) : fronts_(fronts) {
        for (int i = 0; i < responder::kMaxResponses + 1; ++i) follow_[static_cast<size_t>(i)].rng = responder::followSeed(i);
    }

    void tick(float dt, float pitch, float pan, float beatSeconds) override {
        dt_ = dt;
        pitch_ = pitch;
        pan_ = pan;
        beat_ = beatSeconds;
    }

    void fire(const Play& play) override {
        if (play.response == nullptr) return;
        const responder::Response& r = *play.response;
        std::uint32_t rng = responder::seedOf(play.seed);
        const float life = responder::lifeOf(play.fadeInSeconds, play.holdSeconds, play.fadeOutSeconds);
        const int n = std::clamp(static_cast<int>(std::lround(r.params[kCount])), 1, kMaxFronts);
        for (int i = 0; i < n; ++i) send(r, play.strength, life, rng);
    }

    void follow(int instance, const responder::Response& r, float level) override {
        if (instance < 0 || instance > responder::kMaxResponses) return;
        State& s = follow_[static_cast<size_t>(instance)];
        if (!responder::followOwes(s.owed, level, dt_)) return;
        const float scale = r.unit == responder::TimeUnit::Beats ? beat_ : 1.0f;
        send(r, level, responder::lifeOf(r.fadeIn * scale, r.hold * scale, r.fadeOut * scale), s.rng);
    }

private:
    struct State {
        std::uint32_t rng = 1;
        float owed = 0.0f;
    };

    int send(const responder::Response& r, float strength, float life, std::uint32_t& rng) {
        const Kind kind = static_cast<Kind>(responder::ordinalParam(r, kShape, static_cast<int>(Kind::Wedge)));
        const responder::Place place = responder::placeOf(r, kOrigin);
        Spawn s;
        if (responder::isEdge(place)) {
            s = Spawn::fromEdge(static_cast<Edge>(responder::edgeIndex(place)), kind);
        } else {
            const responder::Point at = responder::pointOf(place, rng, pitch_, pan_);
            const responder::Point dir = responder::directionOf(rng);
            s.originX = at.x;
            s.originY = at.y;
            s.dirX = dir.x;
            s.dirY = dir.y;
        }
        s.kind = kind;
        s.effect = static_cast<Effect>(responder::ordinalParam(r, kEffect, static_cast<int>(Effect::Reveal)));
        const float speed = r.params[kSpeed];
        const float width = r.params[kSize];
        s.speed = speed > 0.0f ? speed : kDefaultSpeed;
        s.width = width > 0.0f ? width : kDefaultWidth;
        s.strength = strength;
        s.life_s = life;
        s.palettePos = responder::draw(rng);
        return fronts_.spawnFront(s);
    }

    FrontField& fronts_;
    float dt_ = 0.0f;
    float pitch_ = 0.5f;
    float pan_ = 0.5f;
    float beat_ = 0.0f;
    std::array<State, responder::kMaxResponses + 1> follow_{};
};

}  // namespace geode::viz::fronts
