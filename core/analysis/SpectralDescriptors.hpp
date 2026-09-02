#pragma once

namespace geode::analysis::spectral {

constexpr double kSilenceTotal = 1e-12;
constexpr double kDefaultRolloff = 0.85;

double centroidHz(const float* magnitudes, int count, double binHz);
double bandwidthHz(const float* magnitudes, int count, double binHz, double centroidHz);
double rolloffHz(const float* magnitudes, int count, double binHz, double fraction = kDefaultRolloff);
double flatness(const float* magnitudes, int count);

}  // namespace geode::analysis::spectral
