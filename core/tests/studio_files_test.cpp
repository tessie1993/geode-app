// Host-side checks for Studio's folder moves (core/library/StudioFiles) through the C ABI the shell calls.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) in a scratch folder; no device.
//
// What is pinned (#384): takes saved inside a project folder move to project-takes/<id>/ once, a take
// already at the new place wins over its old copy, a second run changes nothing, and no projects folder
// is not an error.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "api/geode_api.h"

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int moveTakes(const fs::path& root) {
    return geode_studio_move_takes_out((root / "projects").string().c_str(), (root / "project-takes").string().c_str());
}

fs::path fresh(const char* name) {
    const fs::path root = fs::temp_directory_path() / "geode_studio_files_test" / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void movesOnce() {
    const fs::path root = fresh("moves_once");
    write(root / "projects/p1/project.json", "{}");
    write(root / "projects/p1/takes/t1.json", "old one");
    write(root / "projects/p1/takes/t2.json", "old two");

    check(moveTakes(root) == 0, "nothing fails");
    check(!fs::exists(root / "projects/p1/takes"), "the old takes folder is gone");
    check(read(root / "project-takes/p1/t1.json") == "old one", "t1 moved");
    check(read(root / "project-takes/p1/t2.json") == "old two", "t2 moved");
    check(read(root / "projects/p1/project.json") == "{}", "the project file stays");

    check(moveTakes(root) == 0, "a second run fails nothing");
    check(read(root / "project-takes/p1/t1.json") == "old one", "a second run changes nothing");
}

void newCopyWins() {
    const fs::path root = fresh("new_copy_wins");
    write(root / "project-takes/p1/t1.json", "new");
    write(root / "projects/p1/takes/t1.json", "old");

    check(moveTakes(root) == 0, "nothing fails");
    check(read(root / "project-takes/p1/t1.json") == "new", "the take already moved wins");
    check(!fs::exists(root / "projects/p1/takes"), "the old copy is dropped");
}

void nothingToMove() {
    const fs::path root = fresh("nothing");
    check(moveTakes(root) == 0, "no projects folder is not an error");
    check(!fs::exists(root / "project-takes"), "and creates nothing");
    check(geode_studio_move_takes_out(nullptr, nullptr) == 0, "null paths are ignored");
}

}  // namespace

int main() {
    movesOnce();
    newCopyWins();
    nothingToMove();
    fs::remove_all(fs::temp_directory_path() / "geode_studio_files_test");
    if (failures == 0) std::printf("studio_files_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
