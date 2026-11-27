#pragma once
#include <bit>
#include <cmath>

namespace geode::viz::timebase {

inline constexpr double kPeriodSeconds = 7100.0;
inline constexpr double kPrecisionCeilingSeconds = static_cast<double>(std::bit_ceil(static_cast<unsigned>(kPeriodSeconds)));

struct In {
    double step = 0.0;
    bool seam = false;
};

inline void step(const In& in, double& seconds) {
    double t = seconds + in.step;
    if (t >= kPeriodSeconds && (in.seam || t >= kPrecisionCeilingSeconds)) t = std::fmod(t, kPeriodSeconds);
    seconds = t < 0.0 ? 0.0 : t;
}

}  // namespace geode::viz::timebase
