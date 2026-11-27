#include "output/android/CastRenderHost.hpp"

#include <chrono>
#include <exception>
#include <utility>

#include "output/android/EncoderGl.hpp"
#include "util/Log.hpp"
#include "util/ThreadNice.hpp"
#include "util/WorkHint.hpp"

namespace geode::output {

namespace {

constexpr const char* kTag = "CastRender";

// Settings › Performance's High (geode_perf_tier mode 2): the cast's own renderer draws at full quality.
constexpr int kHighTier = 2;

// What the shell last said; read by every stream's thread.
std::atomic<unsigned> gStageFlags{0};
std::atomic<const geode_pcm_ring*> gDeviceRing{nullptr};

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The ring the visuals hear now: the phone's mix while the app hears other apps, else the player's.
const geode_pcm_ring* heardRing() {
    const geode_pcm_ring* device = gDeviceRing.load();
    return device ? device : geode_pcm_ring_live();
}

// What the render thread holds, released in the order a frame no longer needs them: the tee and the fallback
// renderer need the context current, so the context goes last.
struct Held {
    EncoderGl gl;
    geode_viz* fallback = nullptr;
    geode_cast_tee* tee = nullptr;
    ~Held() {
        if (tee) geode_cast_tee_close(tee);
        if (fallback) geode_viz_destroy(fallback);
    }
};

}  // namespace

void CastRender::setStageFlags(unsigned flags) { gStageFlags.store(flags); }
void CastRender::setDeviceRing(const geode_pcm_ring* ring) { gDeviceRing.store(ring); }

CastRender::CastRender(CastRenderSetup setup) : setup_(std::move(setup)) {}

CastRender::~CastRender() {
    stop(0);
    if (thread_.joinable()) thread_.join();
}

void CastRender::start() { thread_ = std::thread([this] { run(); }); }

bool CastRender::stop(int64_t timeoutMs) {
    stopping_.store(true);
    std::unique_lock<std::mutex> lock(endMutex_);
    endCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return ended_; });
    return ended_;
}

void CastRender::run() {
    // It draws whole scenes at the encoder's rate, as the stage thread does: its rank, not its spawner's.
    if (!util::setThreadNice(util::kDisplayNice)) GEODE_LOGW(kTag, "cast render thread keeps its inherited priority");
    geode_cast_encoder* encoder = geode_cast_encoder_open(setup_.server, setup_.webrtc, setup_.music);
    bool ok = false;
    if (!encoder) {
        GEODE_LOGE(kTag, "no cast encoder");
    } else {
        try {
            ok = draw(encoder);
        } catch (const std::exception& e) {
            GEODE_LOGE(kTag, "render loop: %s", e.what());
        } catch (...) {
            GEODE_LOGE(kTag, "render loop failed");
        }
        // After the context and its window surface went; then the server and WebRTC may be freed.
        geode_cast_encoder_close(encoder);
    }
    if (!ok && !stopping_.load()) failed_.store(true);
    {
        std::lock_guard<std::mutex> lock(endMutex_);
        ended_ = true;
    }
    endCv_.notify_all();
}

geode_viz* CastRender::makeFallback(uint64_t& seenLook) {
    geode_viz* viz = geode_viz_create(setup_.assets, setup_.cacheDir.c_str());
    if (!viz) return nullptr;
    // A new renderer starts from the whole look.
    seenLook = 0;
    look_.take(seenLook, [viz](const geode_viz_look& look) { geode_viz_set_look(viz, &look); });
    float tier[GEODE_PERF_TIER_FLOATS] = {};
    if (geode_perf_tier(kHighTier, tier, GEODE_PERF_TIER_FLOATS) == GEODE_PERF_TIER_FLOATS) {
        geode_viz_set_quality(viz, tier[2], tier[3] != 0.0f ? 1 : 0);
    }
    int format[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(format);
    geode_viz_surface_created(viz);
    geode_viz_surface_changed(viz, format[0], format[1]);
    return viz;
}

bool CastRender::draw(geode_cast_encoder* encoder) {
    int format[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(format);
    const int width = format[0];
    const int height = format[1];
    const int fps = format[2];

    Held held;
    if (const char* problem = held.gl.open(geode_cast_encoder_window(encoder))) {
        GEODE_LOGE(kTag, "%s", problem);
        return false;
    }
    // The stage's own picture wherever the stage draws; null on a driver without the GPU buffer extensions.
    held.tee = geode_cast_tee_open(width, height, fps);
    if (!held.tee) GEODE_LOGI(kTag, "no tee: the cast renders on its own");
    const int64_t teeOpenedNs = nowNs();
    int64_t clock[GEODE_FRAME_CLOCK_STATE];
    geode_frame_clock_start(clock, fps, nowNs());
    int64_t fallbackDrewNs = 0;
    uint64_t seenLook = 0;
    const float paced = static_cast<float>(fps);
    const int64_t frameNs = util::WorkHint::targetNs(paced);

    while (!stopping_.load()) {
        if (geode_cast_encoder_failed(encoder)) {
            GEODE_LOGE(kTag, "the cast encoder failed");
            return false;
        }
        const int64_t wait = geode_frame_clock_wait_ns(clock, nowNs());
        if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
        const int64_t ptsNs = geode_frame_clock_take(clock, nowNs());
        const int64_t now = nowNs();
        const geode_pcm_ring* heard = heardRing();
        const bool fromStage = held.tee && geode_cast_tee_draw(held.tee, now, width, height) != 0;
        const bool stageDraws = (geode_stage_run(gStageFlags.load()) & GEODE_STAGE_DRAWS) != 0;
        if (!fromStage && held.tee &&
            geode_cast_tee_await_stage(stageDraws ? 1 : 0, held.fallback ? 1 : 0, now - teeOpenedNs)) {
            // The stage is on screen and its first copy is coming (a new surface stages its scene over several
            // frames): wait for it rather than have a second renderer show another visual first.
            geode_cast_encoder_pump(encoder, heard, geode_frame_clock_audio_until_us(clock));
            util::reportThreadWork(frameNs, nowNs() - now);
            continue;
        }
        if (!fromStage) {
            if (!held.fallback) {
                held.fallback = makeFallback(seenLook);
                if (!held.fallback) {
                    GEODE_LOGE(kTag, "no fallback renderer");
                    return false;
                }
            }
            // geode_viz_set_look passes on only the parts that differ, so a store write never restarts modulation.
            look_.take(seenLook, [&](const geode_viz_look& look) { geode_viz_set_look(held.fallback, &look); });
            // The rate handed over is the encoder's, not the stage's cap: at 30 this renderer is keeping up, so its
            // fluid monitor must not read its frames against the phone's 60 (or 120).
            const int events = geode_viz_frame(held.fallback, heard, paced, paced);
            if ((events & GEODE_FRAME_DIRECTOR_MOMENT) != 0 && geode_viz_take_director_style_moments(held.fallback) > 0) {
                moments_.fetch_add(1);
            }
            fallbackDrewNs = now;
        } else if (held.fallback && geode_cast_fallback_release(now, fallbackDrewNs)) {
            // The stage has led for a while: give the fallback's scenes and sound engine back.
            geode_viz_destroy(held.fallback);
            held.fallback = nullptr;
        }
        if (!held.gl.present(ptsNs)) {
            GEODE_LOGE(kTag, "eglSwapBuffers failed");
            return false;
        }
        geode_cast_encoder_pump(encoder, heard, geode_frame_clock_audio_until_us(clock));
        // The slot's work from its start, the sleep left out: ADPF sizes the CPU for the frame, not the wait.
        util::reportThreadWork(frameNs, nowNs() - now);
    }
    return true;
}

}  // namespace geode::output
