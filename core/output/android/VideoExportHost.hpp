#pragma once

// The phone behind a video export (output/VideoExport.hpp's Host): the codecs and the muxer on the output file,
// the transcoded audio's scratch, an EGL context on the encoder's window, the renderer and the export job that
// draws each frame, and the wordmark. It wires calls the rest of the core already owns (geode_viz_*,
// geode_export_job_*, geode_export_watermark_*, output/android/MediaCodecSink, EncoderGl) and decides nothing:
// which frame is drawn and presented, and when, is the job's.
// Android only: kept out of every host-test compile line.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "output/VideoExport.hpp"

namespace geode::output::videoexport {

// What the shell hands over once, before the job runs. The look is the frame geode_viz_set_look takes.
struct Setup {
    int width = 0;
    int height = 0;
    std::string cacheDir;

    // The file the render is written into (the shell's; the muxer takes its own duplicate), and the audio's
    // scratch (whoever owns the Setup closes it).
    int outputFd = -1;
    int audioFd = -1;

    // The track the renderer hears; each prepare reads it through its own duplicate. Whoever owns the Setup
    // closes this one.
    int trackFd = -1;
    int64_t trackOffset = 0;
    int64_t trackLength = 0;
    int64_t rangeStartMs = 0;

    AAssetManager* assets = nullptr;
    std::string sceneId;
    bool entitled = false;

    // The look; lookSet false: none was handed over whole, the renderer keeps its own.
    bool lookSet = false;
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

// The Host over `setup`, which must outlive it. Every call runs on the thread that runs the job: the GL context
// is current there from openSurface to closeSurface.
std::unique_ptr<Host> androidHost(const Setup& setup);

}  // namespace geode::output::videoexport
