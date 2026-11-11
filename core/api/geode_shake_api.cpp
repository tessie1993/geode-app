#include <new>

#include "api/geode_api.h"
#include "viz/Shake.hpp"

struct geode_shake {
    geode::viz::ShakeDetector detector;
};

extern "C" {

geode_shake* geode_shake_create(void) { return new (std::nothrow) geode_shake(); }

void geode_shake_destroy(geode_shake* s) { delete s; }

void geode_shake_reset(geode_shake* s) {
    if (s) s->detector.reset();
}

int geode_shake_sample(geode_shake* s, int64_t time_ns, float x, float y, float z) {
    return s && s->detector.sample(static_cast<double>(time_ns) * 1e-9, x, y, z) ? 1 : 0;
}

}  // extern "C"
