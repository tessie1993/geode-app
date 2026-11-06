#pragma once
#include <array>
#include <cstdint>

// Which of the shell's pushed inputs a live frame still owes native (#751). Pure, with no GL and no
// Android, so the host can test it (core/tests/push_versions_test.cpp).
//
// Before, `VisualizerRenderer.kt` kept this in Kotlin: it held the last scene params, LFO configs, ADSR
// configs and sound tuning it had pushed and compared each frame's by identity, pushing only what had
// changed, and forgot them all on a new surface. Now the shell keeps one version per input (a counter
// that grows on every write) and hands the four versions to the frame's one call, `geode_viz_frame_begin`;
// native answers with a GEODE_FRAME_PUSH_* bit for each input whose version it has not applied yet, and
// the shell packs and pushes exactly those, on the same GL thread, before `geode_viz_render`. Packing is
// the expensive half (the params codec, one LFO or ADSR array per slot), so an unchanged input costs a
// long across JNI and nothing else.
//
// The rule: a version is wanted once. The same version again is skipped; an older one (out of order, or a
// counter that went back) is ignored rather than taken as a change, so a late stale write cannot undo a
// newer push. A new surface (a new EGL context, whose renderer starts on its defaults) wants every input
// again, whatever its version.
//
// The bits mirror GEODE_FRAME_PUSH_* in core/api/geode_api.h; they sit beside frame::FrameCadence's bits
// in the same answer (below the Liquid field at bit 8).
namespace geode::viz::frame {

enum class PushInput : int { Params = 0, Lfo = 1, Adsr = 2, Tuning = 3 };

inline constexpr int kPushInputs = 4;
inline constexpr int kFramePushShift = 4;
inline constexpr uint32_t kFramePushParams = 1u << (kFramePushShift + static_cast<int>(PushInput::Params));
inline constexpr uint32_t kFramePushLfo = 1u << (kFramePushShift + static_cast<int>(PushInput::Lfo));
inline constexpr uint32_t kFramePushAdsr = 1u << (kFramePushShift + static_cast<int>(PushInput::Adsr));
inline constexpr uint32_t kFramePushTuning = 1u << (kFramePushShift + static_cast<int>(PushInput::Tuning));

// One live surface's applied versions. GL thread only: geode_viz_surface_created and geode_viz_frame_begin.
class PushVersions {
public:
    using Versions = std::array<int64_t, kPushInputs>;

    // A new surface: nothing has been applied to it.
    void surfaceCreated() { applied_.fill(kNone); }

    // This frame's versions, indexed by PushInput. Answers the GEODE_FRAME_PUSH_* bits of the inputs whose
    // version is newer than the one last applied; those count as applied from now, since the shell pushes
    // every input whose bit it gets before the frame renders.
    uint32_t offer(const Versions& versions) {
        uint32_t want = 0;
        for (int i = 0; i < kPushInputs; ++i) {
            if (versions[i] <= applied_[i]) continue;  // the same version, or a stale one
            applied_[i] = versions[i];
            want |= 1u << (kFramePushShift + i);
        }
        return want;
    }

private:
    // Below any version the shell hands over (they start at 0), so a fresh surface wants everything.
    static constexpr int64_t kNone = INT64_MIN;
    Versions applied_{kNone, kNone, kNone, kNone};
};

}  // namespace geode::viz::frame
