#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "viz/frame/Cadence.hpp"

// One live frame, start to end, as one call (geode_viz_frame). Pure, with no GL and no Android: the stages
// come in through LiveStages, so core/tests/live_frame_test.cpp pins the order and every event bit, and
// geode_viz_api.cpp only binds the real renderer, sensors and clocks to them.
//
// The shell no longer offers the look every frame. It pushes each part when the UI changes it, from whatever
// thread that is, through the handle's any-thread setters; what has to reach the GL thread on a frame (the
// scene, Settings › Performance, the gyro) waits in the latches here, and the frame applies it. What comes
// back is one word of events the shell acts on only when a bit is set: no JNI call per frame for a label, a
// path or a count that did not change.
//
// The bits mirror GEODE_FRAME_* in core/api/geode_api.h.
namespace geode::viz::frame {

// The first frame on a new surface, cleared to black: present it, nothing else ran (Cadence.hpp says why).
inline constexpr uint32_t kEventClearOnly = kFrameClearOnly;
// What Liquid mode ran changed since the last frame that said so: LiveFrame::liquidMode() holds it.
inline constexpr uint32_t kEventLiquidChanged = kFrameLiquidChanged;
// The renderer's error text changed (a handle's first drawing frame always says so): read it.
inline constexpr uint32_t kEventErrorChanged = kFrameErrorChanged;
// MilkDrop finished loading a preset: take its path.
inline constexpr uint32_t kEventMilkLoaded = 1u << 4;
// The director committed a look at a new section or a drop: take the count.
inline constexpr uint32_t kEventDirectorMoment = 1u << 5;
// The still asked for was read back this frame: read it.
inline constexpr uint32_t kEventStillReady = 1u << 6;

// One game rotation vector reading (quaternion x, y, z, w) and the display rotation it was read on.
struct GyroReading {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    int rotation = 0;
};

// The phone's tilt as the sensor last reported it. Any thread pushes; the GL thread takes once a frame. A
// push after an off starts a new session, so the frame recaptures the baseline even when no frame saw the
// gyro off between the two.
class GyroLatch {
public:
    static constexpr int64_t kOff = -1;

    void push(const GyroReading& r) {
        std::lock_guard<std::mutex> lock(lock_);
        if (!on_) {
            on_ = true;
            ++session_;
        }
        reading_ = r;
    }

    void off() {
        std::lock_guard<std::mutex> lock(lock_);
        on_ = false;
    }

    // kOff while off; else the session the latest reading belongs to, with the reading in `out`.
    int64_t take(GyroReading& out) const {
        std::lock_guard<std::mutex> lock(lock_);
        if (!on_) return kOff;
        out = reading_;
        return session_;
    }

private:
    mutable std::mutex lock_;
    bool on_ = false;
    int64_t session_ = 0;
    GyroReading reading_{};
};

// The latest of a value any thread sets and the GL thread reads, with a generation the frame compares
// without the lock: a frame that finds nothing new costs one atomic load.
template <typename T>
class Latch {
public:
    void set(T value) {
        std::lock_guard<std::mutex> lock(lock_);
        value_ = std::move(value);
        generation_.store(generation_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    // 0 until the first set.
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    // The value now, and the generation it is.
    uint64_t copy(T& out) const {
        std::lock_guard<std::mutex> lock(lock_);
        out = value_;
        return generation_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex lock_;
    T value_{};
    std::atomic<uint64_t> generation_{0};
};

// What one live frame does, stage by stage. geode_viz_api.cpp binds the renderer; the host test records.
class LiveStages {
public:
    virtual ~LiveStages() = default;
    // The rate the surface's pacer draws at and the surface's own cap, for the governor's thermal step.
    virtual void setPacedFps(float pacedFps, float capFps) = 0;
    // Settings › Performance, only when it changed (or on a new surface).
    virtual void applyQuality(const UserQuality& q) = 0;
    virtual void clearToBlack() = 0;
    // True when this surface won the process's thermal slot on a second a headroom is due.
    virtual bool claimThermal() = 0;
    // Asks the platform for its forecast headroom and hands it to the thermal state.
    virtual void sampleThermal() = 0;
    // The Liquid mode when it changed since the last take, else -1.
    virtual int takeLiquidMode() = 0;
    virtual void gyroReset() = 0;
    virtual void gyroSample(const GyroReading& r) = 0;
    // False when the renderer cannot draw `id` (yet): the next frame asks again.
    virtual bool setScene(const std::string& id) = 0;
    virtual void feedRing() = 0;
    virtual void render() = 0;
    virtual std::string lastError() = 0;
    // Pending, not taken: the shell takes each on its bit.
    virtual bool milkPresetLoaded() = 0;
    virtual bool directorMomentsPending() = 0;
    // True once, on the frame the still was read back.
    virtual bool stillReady() = 0;
};

// One live surface's frame protocol. The latches are any thread; surfaceCreated and run are the GL thread's.
class LiveFrame {
public:
    void requestScene(std::string id) { scene_.set(std::move(id)); }
    void setQuality(const UserQuality& q) { quality_.set(q); }
    void gyroPush(const GyroReading& r) { gyro_.push(r); }
    void gyroOff() { gyro_.off(); }

    // A new surface: its first frame is a clear, and the quality is applied again.
    void surfaceCreated() {
        cadence_.surfaceCreated();
        qualitySeen_ = 0;
    }

    // The Liquid mode the last kEventLiquidChanged said. Any thread.
    int liquidMode() const { return liquidMode_.load(std::memory_order_relaxed); }

    uint32_t run(LiveStages& s, float pacedFps, float capFps) {
        s.setPacedFps(pacedFps, capFps);
        if (const uint64_t g = quality_.generation(); g != qualitySeen_) {
            UserQuality q;
            qualitySeen_ = quality_.copy(q);
            if (cadence_.offerQuality(q)) s.applyQuality(q);
        }
        const uint32_t due = cadence_.step([&s] { return s.claimThermal(); }, [&s] { return s.takeLiquidMode(); });
        if (due & kFrameClearOnly) {
            s.clearToBlack();
            return kEventClearOnly;
        }
        uint32_t events = 0;
        if (due & kFrameThermalDue) s.sampleThermal();
        if (due & kFrameLiquidChanged) {
            liquidMode_.store(FrameCadence::liquidModeOf(due), std::memory_order_relaxed);
            events |= kEventLiquidChanged;
        }
        GyroReading reading;
        const int64_t session = gyro_.take(reading);
        // Off is a session too: one reset when the gyro goes off, not one a frame.
        if (session != gyroApplied_) {
            s.gyroReset();
            gyroApplied_ = session;
        }
        if (session != GyroLatch::kOff) s.gyroSample(reading);
        if (const uint64_t g = scene_.generation(); g != sceneApplied_) {
            if (g != sceneCopied_) sceneCopied_ = scene_.copy(sceneId_);
            if (s.setScene(sceneId_)) sceneApplied_ = sceneCopied_;
        }
        s.feedRing();
        s.render();
        // An error raised while this frame drew is told on this frame.
        if (cadence_.offerError(s.lastError())) events |= kEventErrorChanged;
        if (s.milkPresetLoaded()) events |= kEventMilkLoaded;
        if (s.directorMomentsPending()) events |= kEventDirectorMoment;
        if (s.stillReady()) events |= kEventStillReady;
        return events;
    }

private:
    FrameCadence cadence_;
    Latch<UserQuality> quality_;
    uint64_t qualitySeen_ = 0;
    Latch<std::string> scene_;
    uint64_t sceneApplied_ = 0;
    uint64_t sceneCopied_ = 0;
    std::string sceneId_;
    GyroLatch gyro_;
    int64_t gyroApplied_ = GyroLatch::kOff;
    std::atomic<int> liquidMode_{0};
};

}  // namespace geode::viz::frame
