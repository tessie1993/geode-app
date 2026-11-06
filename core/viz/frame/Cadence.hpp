#pragma once
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>

// What a live surface's frame owes the shell, decided once per frame (#604). Pure, with no GL and no
// Android, so the host can test it (core/tests/renderer_cadence_test.cpp).
//
// Before, `VisualizerRenderer.kt` kept this in Kotlin: a skip for repeated Settings › Performance pushes,
// a first frame on a new surface cleared with direct GLES30 calls, and a poll per frame for the thermal
// reading and the Liquid label. Now the shell makes one call per frame, `geode_viz_frame_begin`, with
// the values it holds; native applies what changed and answers with a bitmask of what is due. Kotlin keeps
// only the Android reads that answer asks for (PowerManager) and the StateFlow it feeds.
//
// The bits mirror GEODE_FRAME_* in core/api/geode_api.h.
namespace geode::viz::frame {

// The first frame on a new surface: native has cleared it to black and the shell presents it and does
// nothing else. A new surface's first real frame builds the scene and compiles its shaders (~5 s on the CI
// emulator's software GPU), and GLSurfaceView parks the main thread in surfaceChanged until that frame
// ends, long enough for an input-dispatch ANR. A clear releases the main thread at once; the build runs on
// the next frame the pacer asks for, where nobody waits on it.
inline constexpr uint32_t kFrameClearOnly = 1u << 0;
// This caller won the process's once-a-second thermal slot (ThermalState::claimSample): read PowerManager
// and push it (geode_thermal_push).
inline constexpr uint32_t kFrameThermalDue = 1u << 1;
// What Liquid mode ran changed since the last frame that said so; the mode is in the liquid field.
inline constexpr uint32_t kFrameLiquidChanged = 1u << 2;
inline constexpr int kFrameLiquidShift = 8;
inline constexpr uint32_t kFrameLiquidMask = 0xFu;

// Settings › Performance as the live shell holds it, pushed every frame.
struct UserQuality {
    float renderScale = 1.0f;
    float marchScale = 1.0f;
    bool postFx = true;
    bool adaptive = true;
    int liquidHotPolicy = 0;
};

// Equal by bits, not by value: a NaN from a corrupt pref equals itself here, so it is applied once (as full
// quality, Renderer::setUserQuality) rather than on every frame.
inline bool sameQuality(const UserQuality& a, const UserQuality& b) {
    return std::bit_cast<uint32_t>(a.renderScale) == std::bit_cast<uint32_t>(b.renderScale) &&
        std::bit_cast<uint32_t>(a.marchScale) == std::bit_cast<uint32_t>(b.marchScale) && a.postFx == b.postFx &&
        a.adaptive == b.adaptive && a.liquidHotPolicy == b.liquidHotPolicy;
}

// One live surface's cadence. GL thread only: geode_viz_surface_created and geode_viz_frame_begin.
class FrameCadence {
public:
    // A new surface: its first frame is a clear, and the quality is applied again (a new handle's
    // renderer starts on its defaults).
    void surfaceCreated() {
        clearPending_ = true;
        applied_.reset();
    }

    // True when `q` differs from what was last applied, which it then becomes.
    bool offerQuality(const UserQuality& q) {
        if (applied_ && sameQuality(*applied_, q)) return false;
        applied_ = q;
        return true;
    }

    // One frame. On the clear-only frame nothing else is asked: the thermal slot stays for a drawing
    // frame (of this surface or another) and the Liquid label for the next frame. `claimThermal()` is
    // true when this caller won the slot; `takeLiquid()` is the Liquid mode when it changed, else -1.
    template <typename ClaimThermal, typename TakeLiquid>
    uint32_t step(ClaimThermal&& claimThermal, TakeLiquid&& takeLiquid) {
        if (std::exchange(clearPending_, false)) return kFrameClearOnly;
        uint32_t due = 0;
        if (claimThermal()) due |= kFrameThermalDue;
        const int mode = takeLiquid();
        if (mode >= 0) {
            due |= kFrameLiquidChanged | ((static_cast<uint32_t>(mode) & kFrameLiquidMask) << kFrameLiquidShift);
        }
        return due;
    }

    static int liquidModeOf(uint32_t due) {
        return static_cast<int>((due >> kFrameLiquidShift) & kFrameLiquidMask);
    }

private:
    bool clearPending_ = false;
    std::optional<UserQuality> applied_;
};

}  // namespace geode::viz::frame
