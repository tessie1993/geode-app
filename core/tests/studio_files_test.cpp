// Host-side checks for Studio's folder moves (core/library/StudioFiles) through the C ABI the shell calls.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) in a scratch folder; no device.
//
// What is pinned (#384): takes saved inside a project folder move to project-takes/<id>/ once, a take
// already at the new place wins over its old copy, a second run changes nothing, and no projects folder
// is not an error. Also (tests for #435): every project keeps its takes under its own folder name, a take that
// cannot move is counted and kept, an interrupted move finishes on the next start, and nothing outside the
// two roots is written.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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

// Every file under root, as a path relative to root ('/'-separated), so a test can say what exists after a
// move and that nothing landed outside projects/ and project-takes/.
std::set<std::string> filesUnder(const fs::path& root) {
    std::set<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file()) out.insert(fs::relative(e.path(), root).generic_string());
    }
    return out;
}

// A project folder named by a real UUID (EditorProjectStore.newId), plus names with dots, a space and
// Cyrillic UTF-8 ("проект"): the folder name is the project id the shell reads takes back by, so it must
// come across byte for byte, and every project keeps its own takes even when take names repeat.
// Kills: target = takesDir (flat, the second t1 is dropped), target = takesDir / "takes", a loop that stops
// after the first project, any rewriting of the folder name.
void everyProjectKeepsItsOwnTakesUnderItsOwnId() {
    const fs::path root = fresh("own_id");
    const char* ids[] = {"3f2b8c1e-9a4d-4e6b-8f00-1c2d3e4f5a6b", "a.b", "...", ".hidden", "id with space",
                         "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB5\xD0\xBA\xD1\x82"};
    for (const char* id : ids) {
        write(root / "projects" / id / "project.json", std::string("{\"id\":\"") + id + "\"}");
        write(root / "projects" / id / "takes/t1.json", std::string("t1 of ") + id);
    }

    check(moveTakes(root) == 0, "own id: nothing fails");
    std::set<std::string> want;
    for (const char* id : ids) {
        want.insert(std::string("projects/") + id + "/project.json");
        want.insert(std::string("project-takes/") + id + "/t1.json");
        check(read(root / "project-takes" / id / "t1.json") == std::string("t1 of ") + id,
              "own id: each project's t1 is its own, under its own folder name");
    }
    check(filesUnder(root) == want, "own id: nothing lost, nothing written outside the two roots");
}

// A project that never recorded a take gets no takes folder: the move only creates what it fills.
// Kills: dropping the "no takes/ folder, skip" guard (create_directories would run for every project).
void aProjectWithoutTakesGetsNoTakesFolder() {
    const fs::path root = fresh("no_takes");
    write(root / "projects/p1/project.json", "{}");
    write(root / "projects/p2/project.json", "{}");
    write(root / "projects/p2/takes/t1.json", "t");

    check(moveTakes(root) == 0, "no takes: nothing fails");
    check(!fs::exists(root / "project-takes/p1"), "no takes: the project without takes gets no folder");
    check(read(root / "project-takes/p2/t1.json") == "t", "no takes: the other project still moves");
}

// A take that cannot move (here: a stray file sits where its project's takes folder must go) is counted and
// stays readable where it was, with its folder; the other projects still move. Nothing is ever deleted
// to make room. Kills: remove_all of the old takes folder, dropping the is_empty guard, counting a failed
// rename as moved (or returning moved/dropped instead of failed), an early return or loop stop on error.
void aTakeThatCannotMoveStaysWhereItWas() {
    const fs::path root = fresh("blocked");
    write(root / "project-takes/stuck", "not a folder");
    write(root / "projects/stuck/takes/t1.json", "one");
    write(root / "projects/stuck/takes/t2.json", "two");
    for (const char* id : {"a", "m", "z"}) write(root / "projects" / id / "takes/t1.json", id);

    check(moveTakes(root) == 2, "blocked: both takes that cannot move are reported");
    check(read(root / "projects/stuck/takes/t1.json") == "one", "blocked: t1 is still where it was");
    check(read(root / "projects/stuck/takes/t2.json") == "two", "blocked: t2 is still where it was");
    check(read(root / "project-takes/stuck") == "not a folder", "blocked: the stray file is left alone");
    for (const char* id : {"a", "m", "z"}) {
        check(read(root / "project-takes" / id / "t1.json") == id, "blocked: the other projects still move");
    }
    check(moveTakes(root) == 2, "blocked: a later run reports them again, still not lost");
}

// An interrupted move (the app killed after some takes moved) finishes on the next start: the takes left
// behind move, the ones already moved keep their new copy, and a third run changes nothing.
// Kills: skipping a project whose new folder already exists, letting the old copy overwrite the new one.
void anInterruptedMoveFinishesNextTime() {
    const fs::path root = fresh("resume");
    write(root / "project-takes/p1/t1.json", "moved, then edited");
    write(root / "projects/p1/takes/t1.json", "old t1");
    write(root / "projects/p1/takes/t2.json", "t2");
    write(root / "projects/p1/takes/t3.json", "t3");

    check(moveTakes(root) == 0, "resume: nothing fails");
    const std::set<std::string> done = {"project-takes/p1/t1.json", "project-takes/p1/t2.json",
                                        "project-takes/p1/t3.json"};
    check(filesUnder(root) == done, "resume: the rest moved and the old folder is gone");
    check(read(root / "project-takes/p1/t1.json") == "moved, then edited", "resume: the moved take wins");
    check(read(root / "project-takes/p1/t2.json") == "t2", "resume: t2 moved whole");

    check(moveTakes(root) == 0, "resume: a third run fails nothing");
    check(filesUnder(root) == done, "resume: a third run changes nothing");
}

// The shell passes two paths; one missing is ignored like both, and moves nothing.
// Kills: `!projects_dir || !takes_dir` -> `&&` (a null would become a std::filesystem::path).
void oneMissingPathMovesNothing() {
    const fs::path root = fresh("one_null");
    write(root / "projects/p1/takes/t1.json", "t");
    const std::string projects = (root / "projects").string();
    const std::string takes = (root / "project-takes").string();

    check(geode_studio_move_takes_out(projects.c_str(), nullptr) == 0, "one null: no takes folder is ignored");
    check(geode_studio_move_takes_out(nullptr, takes.c_str()) == 0, "one null: no projects folder is ignored");
    check(read(root / "projects/p1/takes/t1.json") == "t", "one null: the take is untouched");
    check(!fs::exists(root / "project-takes"), "one null: nothing is created");
}

}  // namespace

int main() {
    movesOnce();
    newCopyWins();
    nothingToMove();
    everyProjectKeepsItsOwnTakesUnderItsOwnId();
    aProjectWithoutTakesGetsNoTakesFolder();
    aTakeThatCannotMoveStaysWhereItWas();
    anInterruptedMoveFinishesNextTime();
    oneMissingPathMovesNothing();
    fs::remove_all(fs::temp_directory_path() / "geode_studio_files_test");
    if (failures == 0) std::printf("studio_files_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
