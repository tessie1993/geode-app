#pragma once
#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// What a live surface's frame owes, decided once per frame (#604): a skip for a repeated Settings › Performance
// value, the Liquid label and the error text, each told only on a change. Pure, with no GL and no Android, so the host can test it
// (core/tests/renderer_cadence_test.cpp). frame::LiveFrame steps it inside geode_viz_frame.
//
// The Liquid and error bits are also GEODE_FRAME_* events in core/api/geode_api.h. Bit 0 was a new surface's clear-only
// first frame, gone with the main-thread GLSurfaceView it released.
namespace geode::viz::frame {

// What Liquid mode ran changed since the last frame that said so; the mode is in the liquid field.
inline constexpr uint32_t kFrameLiquidChanged = 1u << 2;
// The renderer's error text changed since the last frame that said so: read it (geode_viz_last_error).
inline constexpr uint32_t kFrameErrorChanged = 1u << 3;
inline constexpr int kFrameLiquidShift = 8;
inline constexpr uint32_t kFrameLiquidMask = 0xFu;

// Settings › Performance as the live shell set it (geode_viz_set_quality). The render resolution is not
// here: it is the surface's size (viz/frame/Quality bufferSize).
struct UserQuality {
    float marchScale = 1.0f;
    bool postFx = true;
};

// Equal by bits, not by value: a NaN from a corrupt pref equals itself here, so it is applied once (as full
// quality, Renderer::setUserQuality) rather than on every frame.
inline bool sameQuality(const UserQuality& a, const UserQuality& b) {
    return std::bit_cast<uint32_t>(a.marchScale) == std::bit_cast<uint32_t>(b.marchScale) && a.postFx == b.postFx;
}

// One live surface's cadence. GL thread only: geode_viz_surface_created and geode_viz_frame.
class FrameCadence {
public:
    // A new surface: the quality is applied again (a new handle's renderer starts on its defaults).
    void surfaceCreated() { applied_.reset(); }

    // True when `q` differs from what was last applied, which it then becomes.
    bool offerQuality(const UserQuality& q) {
        if (applied_ && sameQuality(*applied_, q)) return false;
        applied_ = q;
        return true;
    }

    // One frame. `takeLiquid()` is the Liquid mode when it changed, else -1.
    template <typename TakeLiquid>
    uint32_t step(TakeLiquid&& takeLiquid) {
        uint32_t due = 0;
        const int mode = takeLiquid();
        if (mode >= 0) {
            due |= kFrameLiquidChanged | ((static_cast<uint32_t>(mode) & kFrameLiquidMask) << kFrameLiquidShift);
        }
        return due;
    }

    // True when `error` differs from the text last reported, which it then becomes. A handle's first offer
    // is always a change, an empty text too, so the shell drops what it kept from the handle before.
    bool offerError(const std::string& error) {
        if (reportedError_ && *reportedError_ == error) return false;
        reportedError_ = error;
        return true;
    }

    static int liquidModeOf(uint32_t due) {
        return static_cast<int>((due >> kFrameLiquidShift) & kFrameLiquidMask);
    }

private:
    std::optional<UserQuality> applied_;
    std::optional<std::string> reportedError_;
};

}  // namespace geode::viz::frame
