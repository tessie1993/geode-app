// The C ABI of core/shell (api/geode_shell_api.h): forwarders only.
#include "api/geode_shell_api.h"

#include <algorithm>

#include "shell/ExportFps.hpp"

namespace shell = geode::shell;

extern "C" {

int geode_export_fps_options(int* out, int capacity) {
    const auto options = shell::exportFpsOptions();
    const int count = static_cast<int>(options.size());
    if (out) std::copy_n(options.begin(), std::clamp(capacity, 0, count), out);
    return count;
}

int geode_export_fps_read(int stored, int fallback) { return shell::exportFpsRead(stored, fallback); }

}  // extern "C"
