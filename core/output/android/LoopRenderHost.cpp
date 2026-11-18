#include "output/android/LoopRenderHost.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "api/geode_export_job_api.h"
#include "api/geode_modulation_api.h"
#include "output/android/EncoderGl.hpp"
#include "output/android/MediaCodecSink.hpp"

namespace geode::output::looprender {

namespace {

// How many names a stop's scratch file is tried under before the cache is called unwritable.
constexpr int kNameTries = 16;

class AndroidHost final : public Host {
public:
    explicit AndroidHost(const Setup& setup) : setup_(setup) {}
    ~AndroidHost() override {
        closeScene();
        closeSurface();
    }

    std::unique_ptr<sink::Codecs> codecs(int fd) override { return sink::mediaCodecs(fd); }

    // The sweep of leftover scratch (the shell's, at start) knows these files by "geode_loop_".
    int createFile(int stop, std::string* path) override {
        const auto seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        int error = 0;
        for (int i = 0; i < kNameTries; ++i) {
            const std::string name = setup_.cacheDir + "/geode_loop_" + std::to_string(stop) + "_" +
                                     std::to_string(seed + static_cast<uint64_t>(i)) + ".mp4";
            const int fd = ::open(name.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
            if (fd >= 0) {
                *path = name;
                return fd;
            }
            error = errno;
            if (error != EEXIST) break;
        }
        *path = std::strerror(error);
        return -1;
    }
    void closeFile(int fd) override {
        if (fd >= 0) ::close(fd);
    }
    void removeFile(const std::string& path) override { ::unlink(path.c_str()); }
    void idleWait() override { std::this_thread::sleep_for(std::chrono::microseconds(kIdleWaitUs)); }

    // An ES 3 context on a recordable RGBA8888 config, current on the encoder's window.
    const char* openSurface(void* window) override { return gl_.open(window); }

    int allocateSeam(int requested, std::string* error) override {
        seam_ = geode_export_seam_create(setup_.assets, setup_.width, setup_.height);
        const char* problem = seam_ ? geode_export_pass_error(seam_) : "out of memory";
        if (problem && problem[0] != '\0') {
            *error = problem;
            return -1;
        }
        return geode_export_seam_allocate(seam_, requested);
    }

    // The export's prepare order: the renderer, the job on the track, its begin, the look, then the warm-up.
    // Native merges the look's reactions under the hand mask the begin pins, so the look goes in between.
    const char* prepareScene(const StopScene& scene) override {
        viz_ = geode_viz_create(setup_.assets, setup_.cacheDir.c_str());
        if (!viz_) return "native renderer could not be created";
        // The export job owns the descriptor it is given, so each stop gets its own.
        const int fd = setup_.trackFd >= 0 ? ::dup(setup_.trackFd) : -1;
        if (fd < 0) return "the track cannot be opened";
        job_ = geode_export_job_create(viz_, fd, setup_.trackOffset, setup_.trackLength, scene.fps, 0);
        if (!job_) return "export job not created";
        const int64_t handMask =
            geode_export_job_begin(job_, setup_.width, setup_.height, setup_.sceneId.c_str(),
                                   setup_.entitled ? 1 : 0,
                                   scene.pinDurationMs);
        if (handMask < 0) {
            error_ = "native renderer cannot draw \"" + setup_.sceneId + "\"";
            return error_.c_str();
        }
        pushLook(scene);
        if (!geode_export_job_warm(job_)) return "the track's audio could not be heard";
        return nullptr;
    }

    const char* prepareMark() override {
        if (setup_.markPlacement == 0) return nullptr;
        mark_ = geode_export_watermark_create(setup_.assets, setup_.markRgba.data(), setup_.markWidth, setup_.markHeight,
                                              setup_.markPlacement, setup_.markCorner, 0);
        const char* problem = mark_ ? geode_export_pass_error(mark_) : "out of memory";
        if (problem && problem[0] != '\0') {
            error_ = std::string("watermark pass failed: ") + problem;
            return error_.c_str();
        }
        return nullptr;
    }

    bool renderFrame(int frame) override { return geode_export_job_frame(job_, frame, 0) != 0; }
    void captureSeam(int index) override { geode_export_seam_capture(seam_, index); }
    void blendSeam(int index, int tail, int seamFrames) override {
        geode_export_seam_blend(seam_, index, tail, seamFrames);
    }

    const char* present(int64_t ptsNs) override {
        if (mark_) geode_export_watermark_draw(mark_, setup_.width, setup_.height, 0);
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
        if (seam_) geode_export_pass_destroy(seam_);
        seam_ = nullptr;
    }

    void closeSurface() override { gl_.close(); }

private:
    // The look in the order the live renderer takes it, with the stop's drift on the params frame.
    void pushLook(const StopScene& scene) {
        geode_viz_request_scene(viz_, setup_.sceneId.c_str());
        std::vector<float> params = setup_.params;
        int colorShift = -1;
        int temperature = -1;
        const int fields = geode_viz_param_count();
        for (int i = 0; i < fields; ++i) {
            const char* name = geode_viz_param_name(i);
            if (name && std::strcmp(name, "colorShift") == 0) colorShift = i;
            if (name && std::strcmp(name, "temperature") == 0) temperature = i;
        }
        driftParams(params, colorShift, temperature, scene.hueOffset, scene.warmthOffset);
        geode_viz_set_params(viz_, params.data(), static_cast<int>(params.size()));
        geode_viz_set_lfos(viz_, setup_.lfos.c_str());
        geode_viz_set_adsrs(viz_, setup_.adsrs.c_str());
        geode_viz_set_sound_tuning(viz_, setup_.soundTuning.empty() ? nullptr : setup_.soundTuning.data(),
                                   static_cast<int>(setup_.soundTuning.size()));
        geode_viz_set_reactions(viz_, setup_.hasReactions ? setup_.reactions.c_str() : nullptr);
        geode_viz_set_reduced_motion(viz_, setup_.reducedMotion ? 1 : 0);
        geode_viz_set_transition(viz_, setup_.transitionId.c_str(), setup_.transitionMs);
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
    geode_export_pass* seam_ = nullptr;
    geode_export_pass* mark_ = nullptr;
};

static_assert(sizeof(GeodeSoundTuning) == sizeof(float) * GEODE_SOUND_TUNING_FLOATS);

}  // namespace

std::unique_ptr<Host> androidHost(const Setup& setup) { return std::make_unique<AndroidHost>(setup); }

}  // namespace geode::output::looprender
