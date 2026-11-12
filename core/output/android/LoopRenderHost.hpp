#pragma once

// The phone behind a loop render (output/LoopRender.hpp's Host): the scratch files in the app's cache, an EGL
// context on each stop's encoder window, the renderer and the export job that draws a stop's frames, the seam
// stash and the wordmark. It wires calls the rest of the core already owns (geode_viz_*, geode_export_job_*,
// geode_export_seam_*, geode_export_watermark_*, output/android/MediaCodecSink) and decides nothing: which
// frame is drawn, stashed, blended or presented, and when, is the job's.
// Android only: kept out of every host-test compile line.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "output/LoopRender.hpp"

namespace geode::output::looprender {

// What the shell hands over once, before the job runs. The look is in the frames the live renderer takes it
// in (geode_viz_set_params, _set_lfos, _set_adsrs, _set_sound_tuning, _set_reactions).
struct Setup {
    int width = 0;
    int height = 0;
    std::string cacheDir;

    // The track every stop hears. Each stop reads it through its own duplicate; whoever owns the Setup closes
    // this one.
    int trackFd = -1;
    int64_t trackOffset = 0;
    int64_t trackLength = 0;

    AAssetManager* assets = nullptr;
    std::string milkTextureDir;
    std::string sceneId;
    std::string milkPath;
    bool hasMilkPath = false;
    bool entitled = false;

    std::vector<float> params;
    std::string lfos;
    std::string adsrs;
    std::vector<float> soundTuning;
    std::string reactions;
    bool hasReactions = false;
    bool reducedMotion = false;
    std::string transitionId;
    int64_t transitionMs = 0;
    std::vector<uint32_t> underlay;
    int underlayWidth = 0;
    int underlayHeight = 0;
    int underlayBlend = 0;
    float underlayAmount = 0.0f;

    // The wordmark's tile and where it goes; placement 0 means these frames carry none.
    std::vector<uint8_t> markRgba;
    int markWidth = 0;
    int markHeight = 0;
    int markPlacement = 0;
    int markCorner = 0;
};

// The Host over `setup`, which must outlive it. Every call but codecs(-1) and the file ones runs on the
// thread that runs the job: the GL context is current there from openSurface to closeSurface.
std::unique_ptr<Host> androidHost(const Setup& setup);

}  // namespace geode::output::looprender
