#pragma once
#include <array>
#include <cstdint>

// Settings › Performance's three tiers and the first-run pick for this phone (ADR 0008, #437).
// Live surfaces only (the view, the wallpaper, the dream); an export never reads them. Pure, so the
// host can test it (core/tests/quality_test.cpp). Kotlin reads the tiers through geode_perf_tier
// and passes in what Android says about the phone (RAM, the low-RAM flag, the media performance
// class) for geode_perf_mode_for_device.
namespace geode::viz::frame {

enum class QualityMode : int { Saver = 0, Balanced = 1, High = 2 };
inline constexpr int kQualityModes = 3;

struct QualityTier {
    float renderScale;  // fraction of the surface's pixels the scene renders
    float fpsCap;       // the live cap the pacer divides to
    float marchScale;   // ray-march step budget for marched styles
    bool postFx;        // trails and the other optional passes
    bool adaptive;      // any sustained frame-time deficit steers the thermal tier; off, only a severe one
};

// Saver is cheaper than High on every axis; Balanced sits between. Every value is one the
// Performance tab offers as a choice, so picking a tier never shows a value the tab can't.
inline constexpr std::array<QualityTier, kQualityModes> kQualityTiers = {{
    {0.5f, 30.0f, 0.5f, false, true},
    {0.75f, 60.0f, 0.75f, true, true},
    {1.0f, 60.0f, 1.0f, true, false},
}};

inline const QualityTier& qualityTier(QualityMode mode) { return kQualityTiers[static_cast<int>(mode)]; }

// The first-run pick. A low-RAM phone, or one under 3 GB, starts on Saver. Media performance class
// 12 (Android S, 31) or better starts on High. Everything else starts on Balanced, however much RAM
// it has: RAM says nothing about the GPU, and a 6 GB phone picked for High drew a marched style at
// 20 fps at full resolution. An unknown RAM (0) is not read as small.
inline constexpr int64_t kMiB = 1024LL * 1024LL;
inline constexpr int64_t kSaverBelowRam = 3000 * kMiB;
inline constexpr int kHighPerformanceClass = 31;

inline QualityMode modeForDevice(int64_t totalRamBytes, bool lowRamDevice, int mediaPerformanceClass) {
    if (lowRamDevice || (totalRamBytes >= 1 && totalRamBytes < kSaverBelowRam)) return QualityMode::Saver;
    if (mediaPerformanceClass >= kHighPerformanceClass) return QualityMode::High;
    return QualityMode::Balanced;
}

}  // namespace geode::viz::frame
