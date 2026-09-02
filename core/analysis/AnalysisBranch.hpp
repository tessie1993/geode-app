#pragma once

namespace geode::analysis {

struct AnalysisBranch {
    const char* name;
    int windowFrames;
    int hopFrames;

    static constexpr AnalysisBranch transient() { return {"transient", 512, 256}; }
    static constexpr AnalysisBranch general() { return {"general", 1024, 512}; }
    static constexpr AnalysisBranch pitch() { return {"pitch", 4096, 512}; }
    static constexpr AnalysisBranch harmony() { return {"harmony", 8192, 512}; }
};

}  // namespace geode::analysis
