#pragma once

#include <filesystem>
#include <system_error>

// Studio's project folders on disk: the decisions about where a project's files live and how old
// layouts move, kept native so the shell only names the folders (EditorProjectStore).
//
// Takes live beside their project, not inside it (#384): files/project-takes/<id>/<take>.json next to
// files/projects/<id>/project.json. A long take is megabytes of JSON, and Android's cloud backup stops
// backing up everything once the app passes its ~25 MB quota; its rules can exclude a fixed path, never
// projects/<any id>/takes, so takes need a root of their own.
namespace geode::library::studio {

// Where a project kept its takes before #384.
inline constexpr const char* kOldTakesSubdir = "takes";

struct MoveReport {
    int moved = 0;    // takes now at the new place
    int dropped = 0;  // old copies of takes the new place already had
    int failed = 0;   // takes left where they were (nothing is lost; they are just not read)
};

// Moves every take saved inside a project folder (projectsDir/<id>/takes/*) to takesDir/<id>/, once: a
// later run finds nothing to move. A take already at the new place wins (it was written after the move
// began) and the old copy is dropped. An emptied old folder is removed. Also covers restoring an older
// backup zip, whose projects still carry their takes. Never throws.
inline MoveReport moveTakesOut(const std::filesystem::path& projectsDir, const std::filesystem::path& takesDir) {
    namespace fs = std::filesystem;
    MoveReport report;
    std::error_code ec;
    if (!fs::is_directory(projectsDir, ec)) return report;
    for (fs::directory_iterator project(projectsDir, ec), end; !ec && project != end; project.increment(ec)) {
        std::error_code e;
        if (!project->is_directory(e)) continue;
        const fs::path old = project->path() / kOldTakesSubdir;
        if (!fs::is_directory(old, e)) continue;
        const fs::path target = takesDir / project->path().filename();
        fs::create_directories(target, e);
        for (fs::directory_iterator take(old, e), tend; !e && take != tend; take.increment(e)) {
            std::error_code f;
            if (!take->is_regular_file(f)) continue;
            const fs::path dest = target / take->path().filename();
            if (fs::exists(dest, f)) {
                if (fs::remove(take->path(), f)) ++report.dropped; else ++report.failed;
            } else {
                fs::rename(take->path(), dest, f);
                if (f) ++report.failed; else ++report.moved;
            }
        }
        std::error_code g;
        if (fs::is_empty(old, g) && !g) fs::remove(old, g);
    }
    return report;
}

}  // namespace geode::library::studio
