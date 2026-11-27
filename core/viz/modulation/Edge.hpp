#pragma once

namespace geode::viz::edge {

inline constexpr float kRise = 0.5f;
inline constexpr float kRearm = 0.3f;

struct In {
    float level = 0.0f;
    bool hit = false;
    bool raw = false;
};

struct State {
    bool high = false;
    bool came = false;
};

inline void step(const In& in, State& s) {
    const bool high = in.level >= kRise;
    s.came = in.raw ? in.hit : (high && !s.high);
    s.high = in.level >= kRearm && (high || s.high);
}

}  // namespace geode::viz::edge
