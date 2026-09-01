#pragma once

namespace geode::analysis::levels {

double rms(const float* frame, int count);
float peak(const float* frame, int count);
double zeroCrossingRate(const float* frame, int count);

}  // namespace geode::analysis::levels
