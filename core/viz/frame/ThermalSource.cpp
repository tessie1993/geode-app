#include "viz/frame/ThermalSource.hpp"

#include <android/thermal.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>

#include "viz/frame/Thermal.hpp"

namespace geode::viz::thermal_source {
namespace {

// How far ahead the headroom is forecast: far enough that the tiers ease off before the platform throttles.
constexpr int kForecastSeconds = 30;

// The thermal state's clock: steady_clock is CLOCK_MONOTONIC on Android. Only the gaps between pushes matter.
double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The process's manager, acquired once and kept: its listener lives as long as the process. Atomic: another
// surface's GL thread may sample while the first is still attaching.
std::once_flag gAttach;
std::atomic<AThermalManager*> gManager{nullptr};

// On the platform's binder thread pool. ThermalState is thread-safe (its own lock and atomics).
void onStatus(void*, AThermalStatus status) {
    ThermalState::process().push(static_cast<int>(status), std::numeric_limits<float>::quiet_NaN(), monotonicSeconds());
}

}  // namespace

void attach() {
    std::call_once(gAttach, [] {
        AThermalManager* manager = AThermal_acquireManager();
        if (!manager) return;
        // ATHERMAL_STATUS_ERROR is -1, which the state reads as "no status this time".
        onStatus(nullptr, AThermal_getCurrentThermalStatus(manager));
        AThermal_registerThermalStatusListener(manager, onStatus, nullptr);
        gManager.store(manager, std::memory_order_release);
    });
}

void sampleHeadroom(double nowSeconds) {
    AThermalManager* manager = gManager.load(std::memory_order_acquire);
    const float headroom =
        manager ? AThermal_getThermalHeadroom(manager, kForecastSeconds) : std::numeric_limits<float>::quiet_NaN();
    ThermalState::process().push(-1, headroom, nowSeconds);
}

}  // namespace geode::viz::thermal_source
