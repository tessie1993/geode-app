#pragma once

namespace geode::analysis {

class TempoStability {
public:
    static constexpr float kScaleOctaves = 0.25f;

    explicit TempoStability(float hopRateHz, float meanSeconds = 1.0f, float deviationSeconds = 2.0f,
                            float silenceSeconds = 1.0f);
    float value() const { return value_; }
    void step(float bpm);
    void reset();

private:
    float meanPole_;
    float devPole_;
    float silencePole_;
    float mean_ = 0.0f;
    float dev_ = kScaleOctaves;
    bool seeded_ = false;
    float value_ = 0.0f;
};

}  // namespace geode::analysis
