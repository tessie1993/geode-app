#pragma once
#include <array>
#include <bitset>
#include <cmath>

#include "viz/modulation/ResponsePlace.hpp"
#include "viz/modulation/Responder.hpp"
#include "viz/modulation/Reactions.hpp"
#include "viz/things/ThingPool.hpp"

namespace geode::viz::things {

// The Event Responder's Things kind (responder::kThing): a response of this kind puts a Thing on the picture, or
// changes one that is there.
//
// Its kind-params (Response::params):
//   P0 shape      0 Dot, 1 Ring, 2 Shard, 3 Blob (for a Morph: the shape it blends into)
//   P1 behaviour  0 Drift, 1 Orbit, 2 Gravity, 3 Bounce (each of them drifts); 4 Morph, 5 Melt (these make nothing:
//                 they act on a Thing already there, see below)
//   P2 origin     0 middle, 1 random (seeded), 2 pitch height, 3 stereo pan, 4 from the left edge, 5 right, 6 bottom,
//                 7 top (an edge Thing enters there and crosses to the other side over its life; P3 is not used)
//   P3 speed      uv a second, along a seeded direction (0: it stays where it is)
//   P4 size       the radius in frame heights (0 reads 0.05); the response's strength scales it from half to full
//   P5 chain      chain-reaction depth 0..3: on its death a Thing leaves three smaller children, each one deeper less
// A Thing has no strength of its own, so the response's strength (after its curve) is its size, as above. Its life
// is fade in + hold + fade out (never under responder::kMinLife) and its fade in and out are the Thing's attack and
// release. Its palette position is drawn from the response's seed, over the whole palette. Orbit turns at
// kOrbitRate radians a second, one way or the other as the seed says; Gravity pulls at the Spawn's own default.
// A While response plays as Once: a Thing has its own life. Drive Follow makes or acts at a rate set by the level
// (the cap: responder::kFollowMaxPerSecond).
//
// Morph and Melt act on one Thing: the one a response of this kind made last while it lives, else the oldest live
// one, else nothing. Morph blends it into P0's shape over the response's life, Melt makes it end that long from now.
//
// Collisions. A response on the Collide row makes Things with chain 0, and drainCollisions() leaves out any contact
// that touches a Thing such a response made: a collision can start a response, but what that response makes cannot
// start the next, so the loop cannot feed itself.
class ThingKind final : public responder::ResponseKindHandler {
public:
    enum Param { kShape = 0, kBehaviour = 1, kOrigin = 2, kSpeed = 3, kSize = 4, kChain = 5 };
    enum Behave { kDrift = 0, kOrbit = 1, kGravity = 2, kBounce = 3, kMorph = 4, kMelt = 5 };
    static constexpr float kDefaultSize = 0.05f;
    static constexpr float kOrbitRate = 1.5f;
    // The most contacts one frame turns into a Collide moment, and the strength each contributes to it.
    static constexpr int kMaxCollisionsPerFrame = 4;

    explicit ThingKind(ThingPool& pool) : pool_(pool) {
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
        std::uint32_t rng = responder::seedOf(play.seed);
        act(*play.response, play.strength, responder::lifeOf(play.fadeInSeconds, play.holdSeconds, play.fadeOutSeconds),
            play.fadeInSeconds, play.fadeOutSeconds, rng);
    }

    void follow(int instance, const responder::Response& r, float level) override {
        if (instance < 0 || instance > responder::kMaxResponses) return;
        State& s = follow_[static_cast<size_t>(instance)];
        if (!responder::followOwes(s.owed, level, dt_)) return;
        const float scale = r.unit == responder::TimeUnit::Beats ? beat_ : 1.0f;
        act(r, level, responder::lifeOf(r.fadeIn * scale, r.hold * scale, r.fadeOut * scale), r.fadeIn * scale,
            r.fadeOut * scale, s.rng);
    }

    // Once a frame, after the pool has stepped: this frame's Collide events, drained, as the strength (0..1) of the
    // one Collide moment they make, or 0 when none counts. Contacts that touch a Thing a Collide response made are
    // left out, and no more than kMaxCollisionsPerFrame count; each counted contact is a share of the strength.
    float drainCollisions() {
        for (int i = 0; i < kMaxThings; ++i) {
            if (!pool_.alive(i)) collideMade_.reset(static_cast<size_t>(i));
        }
        const EventList events = pool_.drainEvents();
        int counted = 0;
        for (int i = 0; i < events.count && counted < kMaxCollisionsPerFrame; ++i) {
            const ThingEvent& e = events.items[static_cast<size_t>(i)];
            if (e.type != EventType::Collide) continue;
            if (madeByCollide(e.a) || madeByCollide(e.b)) continue;
            ++counted;
        }
        return static_cast<float>(counted) / static_cast<float>(kMaxCollisionsPerFrame);
    }

private:
    struct State {
        std::uint32_t rng = 1;
        float owed = 0.0f;
    };

    bool madeByCollide(int slot) const { return slot >= 0 && slot < kMaxThings && collideMade_.test(static_cast<size_t>(slot)); }

    // The Thing a Morph or a Melt acts on: the last one a response of this kind made while it lives, else the oldest
    // live one, else -1.
    int target() const {
        if (pool_.alive(last_)) return last_;
        int oldest = -1;
        for (int i = 0; i < kMaxThings; ++i) {
            if (pool_.alive(i) && (oldest < 0 || pool_.age(i) > pool_.age(oldest))) oldest = i;
        }
        return oldest;
    }

    void act(const responder::Response& r, float strength, float life, float fadeIn, float fadeOut, std::uint32_t& rng) {
        const int how = responder::ordinalParam(r, kBehaviour, kMelt);
        const Shape shape = static_cast<Shape>(responder::ordinalParam(r, kShape, static_cast<int>(Shape::Blob)));
        if (how == kMorph || how == kMelt) {
            const int slot = target();
            if (slot < 0) return;
            if (how == kMorph) {
                pool_.morph(slot, shape, life);
            } else {
                pool_.melt(slot, life);
            }
            return;
        }
        const responder::Place place = responder::placeOf(r, kOrigin);
        const float base = r.params[kSize] > 0.0f ? r.params[kSize] : kDefaultSize;
        const float size = base * (0.5f + 0.5f * std::clamp(strength, 0.0f, 1.0f));
        Spawn t;
        if (responder::isEdge(place)) {
            t = Spawn::fromEdge(static_cast<Edge>(responder::edgeIndex(place)), shape, size, life);
        } else {
            const responder::Point at = responder::pointOf(place, rng, pitch_, pan_);
            const responder::Point dir = responder::directionOf(rng);
            const float speed = r.params[kSpeed];
            t.x = at.x;
            t.y = at.y;
            t.vx = dir.x * speed;
            t.vy = dir.y * speed;
        }
        t.shape = shape;
        t.size = size;
        t.life_s = life;
        t.attack_s = fadeIn;
        t.release_s = fadeOut;
        t.palettePos = responder::draw(rng);
        t.behaviour = Drift;
        if (how == kOrbit) {
            t.behaviour |= Orbit;
            t.orbit = responder::draw(rng) < 0.5f ? -kOrbitRate : kOrbitRate;
        } else if (how == kGravity) {
            t.behaviour |= Gravity;
        } else if (how == kBounce) {
            t.behaviour |= Bounce;
        }
        const bool fromCollide = r.row == static_cast<int>(Moment::Collide);
        t.chain = fromCollide ? 0 : responder::ordinalParam(r, kChain, 3);
        const int slot = pool_.spawn(t);
        if (slot >= 0) last_ = slot;
        if (slot >= 0) collideMade_.set(static_cast<size_t>(slot), fromCollide);
    }

    ThingPool& pool_;
    float dt_ = 0.0f;
    float pitch_ = 0.5f;
    float pan_ = 0.5f;
    float beat_ = 0.0f;
    int last_ = -1;  // the slot a response of this kind made last
    std::bitset<kMaxThings> collideMade_;
    std::array<State, responder::kMaxResponses + 1> follow_{};
};

}  // namespace geode::viz::things
