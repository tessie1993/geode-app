#pragma once
#include <array>
#include <vector>

namespace geode::viz::cymatics {

// Port of CymaticsMath.kt, CymaticsPlate.kt and CymaticsDrops.kt.
constexpr float kMinBandHz = 30.0f;
constexpr float kMaxBandHz = 16000.0f;
constexpr int kMaxOrder = 14;
constexpr int kMaxRenderedModes = 8;
constexpr float kMinFundamentalHz = 40.0f;
constexpr float kMaxFundamentalHz = 440.0f;
constexpr float kAttackSeconds = 0.035f;
constexpr float kMinRingSeconds = 0.06f;
constexpr float kMaxRingSeconds = 2.5f;
constexpr float kSilence = 1e-4f;
constexpr int kWhitenRadius = 4;
constexpr float kVibrationHzPerOrder = 0.14f;
constexpr float kMinVibrationHz = 0.12f;
constexpr float kMaxVibrationHz = 1.6f;
constexpr float kMaxDrive = 4.0f;
constexpr float kLiveAmplitude = 0.02f;

struct Mode {
    int n;
    int m;
    float wavenumber;
};

const std::vector<Mode>& modes();
float bandCenterHz(int band, int bandCount);
float wavenumberFor(float hz, float fundamentalHz);
int modeIndexFor(float wavenumber);
std::vector<int> bandModeMap(int bandCount, float fundamentalHz);
float ringSeconds(float ring);
float smoothing(float dt, float tau);
float vibrationHz(float wavenumber);
float safeDrive(float raw);
float fieldLiveness(float totalAmplitude);
float wrapPhase(float value, float period);
float approachHue(float current, float target, float alpha);

class Plate {
public:
    Plate();
    void reset();
    void excite(const float* bands, int bandCount, float dt, float fundamentalHz, float drive, float ringSeconds, float focus);
    void advancePhases(float dt, float speed);
    // Writes up to `limit` modes as (n, m, amplitude, phase) quads; returns how many.
    int snapshot(int limit, float* out, int outFloats);
    float dominantWavenumber() const { return dominantWavenumber_; }

private:
    static constexpr float kWhitenGain = 2.6f;

    void ensureMap(int bandCount, float fundamentalHz);
    void localMean(const float* bands, int bandCount);

    std::vector<float> amplitudes_;
    std::vector<bool> taken_;
    std::vector<float> excitation_;
    std::vector<float> phases_;
    std::vector<int> map_;
    int mapBandCount_ = -1;
    float mapFundamental_ = -1.0f;
    std::vector<float> smoothed_;
    float dominantWavenumber_ = 0.0f;
};

class Drops {
public:
    static constexpr int kSlots = 6;
    static constexpr float kOmega = 2.4f;
    static constexpr float kDecaySeconds = 1.4f;
    static constexpr float kSpawnThreshold = 0.3f;
    static constexpr float kCooldownSeconds = 0.12f;
    static constexpr float kDropSilence = 0.004f;
    static constexpr float kSpread = 1.1f;

    const std::array<float, kSlots * 4>& packed() const { return packed_; }
    void update(float dt, float hit);
    void reset();

private:
    void spawn(float strength);
    static float hash(int n);

    std::array<float, kSlots * 4> packed_{};
    int next_ = 0;
    float cooldown_ = 0.0f;
    int seed_ = 0;
};

}  // namespace geode::viz::cymatics
