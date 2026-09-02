#pragma once
#include "viz/GlCaps.hpp"

namespace geode::viz {

// Every function needs a current GL context on the calling thread.
namespace prober {
GlIdentity identity();
GlProbeReport probe();
int limit(unsigned int pname);
void drainErrors();
}  // namespace prober

}  // namespace geode::viz
