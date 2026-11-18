#include "viz/CompositeGrade.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::grade {

float integrateRotation(float angle, float rotation, float dt) { return std::fmod(angle + rotation * dt, kTau); }

}  // namespace geode::viz::grade
