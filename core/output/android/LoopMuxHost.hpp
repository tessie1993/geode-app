#pragma once

// The phone behind a loop extend's copy (output/LoopMux.hpp's Host): AMediaExtractor reading each rendered loop's
// encoded video back, the codecs and the muxer on the output file (output/android/MediaCodecSink), and the
// soundtrack's scratch files read by offset. It decides nothing: which sample goes where, and when, is the job's.
// Android only: kept out of every host-test compile line.

#include <memory>
#include <string>
#include <vector>

#include "output/LoopMux.hpp"

namespace geode::output::loopmux {

// What the shell hands over once, before the job runs.
struct Setup {
    // The file the copy is written into (the shell's; the muxer takes its own duplicate).
    int outputFd = -1;
    // The rendered loops' files, in the order the plan indexes them.
    std::vector<std::string> loopPaths;
    // The soundtrack's scratch files, one per clip (whoever owns the Setup closes them).
    std::vector<int> clipFds;
};

// The Host over `setup`, which must outlive it. Every call runs on the thread that runs the job.
std::unique_ptr<Host> androidHost(const Setup& setup);

}  // namespace geode::output::loopmux
