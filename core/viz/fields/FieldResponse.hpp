#pragma once
#include <array>
#include <cmath>

#include "viz/fields/FieldSet.hpp"
#include "viz/modulation/ResponsePlace.hpp"
#include "viz/modulation/Responder.hpp"

namespace geode::viz::fields {

// The Event Responder's Fields kind (responder::kField): a response of this kind puts force points on the picture.
//
// Its kind-params (Response::params):
//   P0 kind     0 Attract, 1 Repel, 2 Swirl
//   P1 turn     Swirl only: 0 counter-clockwise, 1 clockwise
//   P2 origin   0 middle, 1 random (seeded), 2 pitch height, 3 stereo pan, 4 from the left edge, 5 right, 6 bottom,
//               7 top (a point on that edge, halfway along it; it drifts inward when P3 is above 0)
//   P3 drift    uv a second, along a seeded direction (an edge Field drifts straight in); 0: it stays where it is
//   P4 radius   frame heights (0 reads 0.3)
//   P5 count    Fields one fire makes, 1..kMaxFields (0 reads 1); every one at the origin (a random origin draws
//               each its own point)
// A Field's strength is the response's strength after its curve times kMaxStrength; its life is fade in + hold +
// fade out (never under responder::kMinLife) with the fade in and out as the Field's attack and release. A While
// response plays as Once.
//
// Drive Follow holds one Field per response whose strength tracks the level (setStrength every frame): it is made
// when the level first rises, lives kFollowLife seconds and is replaced a moment before it ends. When the response
// stops being followed it ends at most kFollowLife seconds later, at the strength it last had.
class FieldKind final : public responder::ResponseKindHandler {
public:
    enum Param { kKind = 0, kTurn = 1, kOrigin = 2, kDrift = 3, kRadius = 4, kCount = 5 };
    static constexpr float kDefaultRadius = 0.3f;
    static constexpr float kFollowLife = 2.0f;
    static constexpr float kFollowHandoff = 0.25f;

    explicit FieldKind(FieldSet& fields) : fields_(fields) {
        for (int i = 0; i < responder::kMaxResponses + 1; ++i) follow_[static_cast<size_t>(i)].rng = responder::followSeed(i);
    }

    void tick(float, float pitch, float pan, float) override {
        pitch_ = pitch;
        pan_ = pan;
    }

    void fire(const Play& play) override {
        if (play.response == nullptr) return;
        const responder::Response& r = *play.response;
        std::uint32_t rng = responder::seedOf(play.seed);
        const float life = responder::lifeOf(play.fadeInSeconds, play.holdSeconds, play.fadeOutSeconds);
        const int n = std::clamp(static_cast<int>(std::lround(r.params[kCount])), 1, kMaxFields);
        const responder::Place place = responder::placeOf(r, kOrigin);
        Spawn s = make(r, place, rng);
        s.life_s = life;
        s.attack_s = play.fadeInSeconds;
        s.release_s = play.fadeOutSeconds;
        s.strength = signedStrength(r, play.strength);
        for (int i = 0; i < n; ++i) {
            if (i > 0 && place == responder::Place::Random) {
                const responder::Point at = responder::pointOf(place, rng, pitch_, pan_);
                s.x = at.x;
                s.y = at.y;
            }
            fields_.spawn(s);
        }
    }

    void follow(int instance, const responder::Response& r, float level) override {
        if (instance < 0 || instance > responder::kMaxResponses) return;
        State& st = follow_[static_cast<size_t>(instance)];
        const float strength = signedStrength(r, level);
        const bool held = fields_.alive(st.slot) && fields_.age(st.slot) < kFollowLife - kFollowHandoff;
        if (held) {
            fields_.setStrength(st.slot, strength);
            return;
        }
        if (!(level > 0.0f)) return;
        // The one that is ending lets go as its successor takes the level.
        if (fields_.alive(st.slot)) fields_.setStrength(st.slot, 0.0f);
        Spawn s = make(r, responder::placeOf(r, kOrigin), st.rng);
        s.life_s = kFollowLife;
        s.attack_s = 0.0f;
        s.release_s = 0.0f;
        s.strength = strength;
        st.slot = fields_.spawn(s);
    }

private:
    struct State {
        std::uint32_t rng = 1;
        int slot = -1;
    };

    // The positive size a level or a strength gives, with a Swirl's sign from P1.
    static float signedStrength(const responder::Response& r, float strength) {
        const float s = std::clamp(strength, 0.0f, 1.0f) * kMaxStrength;
        return responder::ordinalParam(r, kKind, static_cast<int>(Kind::Swirl)) == static_cast<int>(Kind::Swirl) &&
                       responder::ordinalParam(r, kTurn, 1) == 1
                   ? -s
                   : s;
    }

    // Everything of a Field but its life, its envelope and its strength.
    Spawn make(const responder::Response& r, responder::Place place, std::uint32_t& rng) {
        Spawn s;
        s.kind = static_cast<Kind>(responder::ordinalParam(r, kKind, static_cast<int>(Kind::Swirl)));
        const float radius = r.params[kRadius];
        s.radius = radius > 0.0f ? radius : kDefaultRadius;
        const float drift = std::max(r.params[kDrift], 0.0f);
        if (responder::isEdge(place)) {
            // Halfway along the edge, drifting inward.
            switch (responder::edgeIndex(place)) {
                case 0: s.x = 0.0f; s.y = 0.5f; s.vx = drift; break;
                case 1: s.x = 1.0f; s.y = 0.5f; s.vx = -drift; break;
                case 2: s.x = 0.5f; s.y = 0.0f; s.vy = drift; break;
                default: s.x = 0.5f; s.y = 1.0f; s.vy = -drift; break;
            }
        } else {
            const responder::Point at = responder::pointOf(place, rng, pitch_, pan_);
            const responder::Point dir = responder::directionOf(rng);
            s.x = at.x;
            s.y = at.y;
            s.vx = dir.x * drift;
            s.vy = dir.y * drift;
        }
        return s;
    }

    FieldSet& fields_;
    float pitch_ = 0.5f;
    float pan_ = 0.5f;
    std::array<State, responder::kMaxResponses + 1> follow_{};
};

}  // namespace geode::viz::fields
