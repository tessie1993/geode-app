#pragma once

#include <string>
#include <utility>

// A .milk preset counts as shown, and is reported (and so remembered for every renderer's next start,
// MilkPresetMemory), only once the engine has drawn it for kShownFrames frames without an error. A preset
// that compiled but crashes or hangs while it draws is then never the one the app, the wallpaper or the
// dream comes back on.
namespace geode::viz::milk {

class ShownReport {
public:
    static constexpr int kShownFrames = 30;

    // The engine took [path]; a load before the last one was reported replaces it.
    void loaded(std::string path) {
        path_ = std::move(path);
        left_ = kShownFrames;
    }

    // After each frame; the path once, on the frame that completes the count, else empty.
    std::string drew(bool ok) {
        if (left_ <= 0) return {};
        if (!ok) {
            left_ = kShownFrames;
            return {};
        }
        if (--left_ > 0) return {};
        std::string out;
        out.swap(path_);
        return out;
    }

private:
    std::string path_;
    int left_ = 0;
};

}  // namespace geode::viz::milk
