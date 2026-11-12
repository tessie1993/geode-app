#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

// The MilkDrop preset on screen, kept once for the whole process and saved across restarts.
//
// A native renderer does not live as long as the preset should: the app's view destroys its handle on
// every detach, an activity rebuild makes a new one, and the wallpaper, the dream and export build their
// own. Each would start on projectM's idle preset unless told otherwise, and that idle logo is the reset
// this prevents: every new renderer starts on restorePath, and export takes it too. One memory for every
// surface is the user's call: the wallpaper shows the preset the app last showed, and a preset the
// wallpaper moves on to is the one the app comes back on.
//
// Only a load the renderer has reported (MilkShown: drawn clean) counts as shown, and only that is saved.
// A request that never reports was in flight, failed to compile or failed to draw, so it is used only
// while nothing has been shown. A remembered path whose file has gone is skipped; with nothing left, a
// first launch starts on a bundled preset. Every call is thread-safe. "" means none.
namespace geode::viz::milk {

class Memory {
public:
    using Usable = std::function<bool(const std::string&)>;
    using FirstRun = std::function<std::string()>;

    // The path the last run saved; only the first seed counts, and none after any other change.
    void seed(const std::string& saved);
    // The UI asked for path.
    void request(const std::string& path);
    // A renderer reported path shown. True when it is new, so the caller saves it.
    bool loaded(const std::string& path);
    // paths were deleted; a restore must not ask for them again. True when the saved path must be dropped.
    bool forget(const std::vector<std::string>& paths);

    // The last preset shown, while usable; "" when none has been.
    std::string lastShown(const Usable& usable) const;
    // What a new renderer must start on: the last shown, else a usable request, else firstRun's answer.
    std::string restorePath(const Usable& usable, const FirstRun& firstRun) const;

private:
    mutable std::mutex mutex_;
    bool seeded_ = false;
    std::string requested_;
    std::string shown_;
};

}  // namespace geode::viz::milk
