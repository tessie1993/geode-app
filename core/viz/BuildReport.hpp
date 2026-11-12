#pragma once
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// What the stage says when a style did not build: the style, the stage it stopped at and the
// first lines of the driver's log, short enough to read off a phone. Pure; viz_math_test pins it.
namespace geode::viz::build_report {

enum class Stage { Asset, Compile, Link, Stall };

constexpr int kMaxLines = 4;
constexpr std::size_t kMaxLineChars = 160;

inline const char* name(Stage stage) {
    switch (stage) {
        case Stage::Asset: return "asset";
        case Stage::Compile: return "compile";
        case Stage::Link: return "link";
        case Stage::Stall: return "stall";
    }
    return "compile";
}

// ShaderSource names a missing or refused file, Program and ProgramStager prefix a link log.
inline Stage stageOf(const std::string& message) {
    for (const char* mark : {"missing shader", "unknown shader include", "circular shader include"}) {
        if (message.find(mark) != std::string::npos) return Stage::Asset;
    }
    if (message.find("Link failed") != std::string::npos || message.find("link failed") != std::string::npos) return Stage::Link;
    return Stage::Compile;
}

inline std::string firstLines(const std::string& log) {
    std::string out;
    int lines = 0;
    std::size_t at = 0;
    while (at < log.size() && lines < kMaxLines) {
        std::size_t end = log.find('\n', at);
        if (end == std::string::npos) end = log.size();
        std::string line = log.substr(at, end - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\0')) line.pop_back();
        at = end + 1;
        if (line.empty()) continue;
        if (line.size() > kMaxLineChars) line.resize(kMaxLineChars);
        if (!out.empty()) out += '\n';
        out += line;
        ++lines;
    }
    return out;
}

inline std::string text(const std::string& id, Stage stage, const std::string& log) {
    std::string out = id + " did not load (" + name(stage) + ")";
    const std::string lines = firstLines(log);
    if (stage == Stage::Stall) {
        out += "\nits shaders did not finish compiling";
    } else if (lines.empty()) {
        out += "\nthe driver gave no message";
    }
    if (!lines.empty()) out += "\n" + lines;
    return out;
}

// From what a failed build reported: its last message that says anything.
inline std::string of(const std::string& id, const std::vector<std::string>& errors) {
    for (auto it = errors.rbegin(); it != errors.rend(); ++it) {
        if (!it->empty()) return text(id, stageOf(*it), *it);
    }
    return text(id, Stage::Compile, "");
}

}  // namespace geode::viz::build_report
