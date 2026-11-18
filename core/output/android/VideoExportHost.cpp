#include "output/android/VideoExportHost.hpp"

#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>

#include "api/geode_export_job_api.h"
#include "output/android/EncoderGl.hpp"
#include "output/android/MediaCodecSink.hpp"

namespace geode::output::videoexport {

namespace {

class AndroidHost final : public Host {
public:
    explicit AndroidHost(const Setup& setup) : setup_(setup) {}
    ~AndroidHost() override {
        closeScene();
        closeSurface();
    }

    std::unique_ptr<sink::Codecs> codecs() override { return sink::mediaCodecs(setup_.outputFd); }

    // Every byte the sample names, or as many as the scratch holds.
    int readAudio(const AudioSample& sample, uint8_t* out) override {
        if (setup_.audioFd < 0) return -1;
        int read = 0;
        while (read < sample.size) {
            const ssize_t n = ::pread(setup_.audioFd, out + read, static_cast<size_t>(sample.size - read),
                                      static_cast<off_t>(sample.offset + read));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) return -1;
            if (n == 0) break;
            read += static_cast<int>(n);
        }
        return read;
    }

    void idleWait() override { std::this_thread::sleep_for(std::chrono::microseconds(kIdleWaitUs)); }

    const char* openSurface(void* window) override { return gl_.open(window); }

    // The export's prepare order: the renderer, the job on the track, its begin, the look, then the warm-up.
    // Native merges the look's reactions under the hand mask the begin pins, so the look goes in between.
    const char* prepareScene(const Scene& scene) override {
        viz_ = geode_viz_create(setup_.assets, setup_.cacheDir.c_str());
        if (!viz_) return "native renderer could not be created";
        // The export job owns the descriptor it is given.
        const int fd = setup_.trackFd >= 0 ? ::dup(setup_.trackFd) : -1;
        if (fd < 0) return "the track cannot be opened";
        job_ = geode_export_job_create(viz_, fd, setup_.trackOffset, setup_.trackLength, scene.fps,
                                       setup_.rangeStartMs);
        if (!job_) return "export job not created";
        const int64_t handMask =
            geode_export_job_begin(job_, setup_.width, setup_.height, setup_.sceneId.c_str(),
                                   setup_.entitled ? 1 : 0,
                                   scene.durationMs);
        if (handMask < 0) {
            error_ = "native renderer cannot draw \"" + setup_.sceneId + "\"";
            return error_.c_str();
        }
        pushLook();
        // A track it cannot hear fails the render; it used to export a picture the silence rule froze (#999 F1).
        if (!geode_export_job_warm(job_)) return "the track's audio could not be heard";
        return nullptr;
    }

    const char* prepareMark(int64_t lengthMs) override {
        if (setup_.markPlacement == 0) return nullptr;
        mark_ = geode_export_watermark_create(setup_.assets, setup_.markRgba.data(), setup_.markWidth,
                                              setup_.markHeight, setup_.markPlacement, setup_.markCorner, lengthMs);
        const char* problem = mark_ ? geode_export_pass_error(mark_) : "out of memory";
        if (problem && problem[0] != '\0') {
            error_ = std::string("watermark pass failed: ") + problem;
            return error_.c_str();
        }
        return nullptr;
    }

    bool renderFrame(int64_t frame) override { return geode_export_job_frame(job_, frame, 0) != 0; }

    const char* present(int64_t ptsNs, int64_t timeMs) override {
        if (mark_) geode_export_watermark_draw(mark_, setup_.width, setup_.height, timeMs);
        return gl_.present(ptsNs) ? nullptr : "eglSwapBuffers failed";
    }

    // Each of these deletes GL names, so all of it runs while the context is still current.
    void closeScene() override {
        if (mark_) geode_export_pass_destroy(mark_);
        mark_ = nullptr;
        if (job_) geode_export_job_destroy(job_);
        job_ = nullptr;
        if (viz_) geode_viz_destroy(viz_);
        viz_ = nullptr;
    }

    void closeSurface() override { gl_.close(); }

private:
    // The look as the live renderer takes it, on the scene being drawn, then the underlay.
    void pushLook() {
        if (setup_.lookSet) {
            geode_viz_look look{};
            look.scene_id = setup_.sceneId.c_str();
            look.params = setup_.params.empty() ? nullptr : setup_.params.data();
            look.param_count = static_cast<int>(setup_.params.size());
            look.lfos = setup_.lfos.c_str();
            look.adsrs = setup_.adsrs.c_str();
            look.tuning = setup_.soundTuning.empty() ? nullptr : setup_.soundTuning.data();
            look.tuning_count = static_cast<int>(setup_.soundTuning.size());
            look.reactions = setup_.hasReactions ? setup_.reactions.c_str() : nullptr;
            look.reduced_motion = setup_.reducedMotion ? 1 : 0;
            look.transition_id = setup_.transitionId.c_str();
            look.transition_ms = setup_.transitionMs;
            geode_viz_set_look(viz_, &look);
        }
        if (setup_.underlay.empty()) {
            geode_viz_set_underlay_rgba(viz_, nullptr, 0, 0, setup_.underlayBlend, setup_.underlayAmount);
        } else {
            geode_viz_set_underlay_rgba(viz_, setup_.underlay.data(), setup_.underlayWidth, setup_.underlayHeight,
                                        setup_.underlayBlend, setup_.underlayAmount);
        }
    }

    const Setup& setup_;
    std::string error_;
    EncoderGl gl_;
    geode_viz* viz_ = nullptr;
    geode_export_job* job_ = nullptr;
    geode_export_pass* mark_ = nullptr;
};

}  // namespace

std::unique_ptr<Host> androidHost(const Setup& setup) { return std::make_unique<AndroidHost>(setup); }

}  // namespace geode::output::videoexport
