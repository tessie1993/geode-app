#pragma once

namespace geode::analysis {

class Envelope {
public:
    Envelope(float attackSeconds, float releaseSeconds) : attackSeconds_(attackSeconds), releaseSeconds_(releaseSeconds) {}
    float attackSeconds() const { return attackSeconds_; }
    void setAttackSeconds(float value) { attackSeconds_ = value; }
    float releaseSeconds() const { return releaseSeconds_; }
    void setReleaseSeconds(float value) { releaseSeconds_ = value; }
    float value() const { return value_; }
    float step(float target, float dtSeconds);
    void primeTo(float level) { value_ = level; }
    void reset() { value_ = 0.0f; }

private:
    float attackSeconds_;
    float releaseSeconds_;
    float value_ = 0.0f;
};

}  // namespace geode::analysis
