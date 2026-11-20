// Host-side checks for core/cast/CastLook: the look the cast's own renderer draws, handed in by the shell and read
// on the render thread. What is pinned: nothing is applied before a look is set; a look is applied once, then again
// only after a new set; it is a deep copy (the caller's buffers may go); a null text stays null, which is a
// different look from an empty one (no reactions: the style's own routes); a renderer made later is given the whole
// look by a fresh `seen`.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cast/CastLook.hpp"

using geode::cast::CastLook;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// What apply saw, copied out of the look's borrowed pointers.
struct Seen {
    std::string scene;
    std::string reactions;
    bool reactionsNull = false;
    bool lfosNull = false;
    bool tuningNull = false;
    std::vector<float> params;
    int tuningCount = 0;
    int reduced = 0;
    long long ms = 0;
};

Seen read(const CastLook& hold, uint64_t& seen, bool* applied) {
    Seen out;
    *applied = hold.take(seen, [&](const geode_viz_look& look) {
        out.scene = look.scene_id ? look.scene_id : "";
        out.reactionsNull = look.reactions == nullptr;
        out.reactions = look.reactions ? look.reactions : "";
        out.lfosNull = look.lfos == nullptr;
        out.tuningNull = look.tuning == nullptr;
        out.tuningCount = look.tuning_count;
        if (look.params) out.params.assign(look.params, look.params + look.param_count);
        out.reduced = look.reduced_motion;
        out.ms = look.transition_ms;
    });
    return out;
}

void nothingBeforeASet() {
    CastLook hold;
    uint64_t seen = 0;
    bool applied = true;
    read(hold, seen, &applied);
    check(!applied, "no look set: nothing is applied");
}

void appliedOnceThenAgainAfterANewSet() {
    CastLook hold;
    std::vector<float> params = {0.25f, 0.5f, 1.0f};
    geode_viz_look look{};
    look.scene_id = "aurora";
    look.params = params.data();
    look.param_count = static_cast<int>(params.size());
    look.reduced_motion = 1;
    look.transition_ms = 800;
    hold.set(look);

    uint64_t seen = 0;
    bool applied = false;
    const Seen first = read(hold, seen, &applied);
    check(applied, "a set look is applied");
    check(first.scene == "aurora", "the scene crosses");
    check(first.params == params, "the params cross, in order");
    check(first.reduced == 1 && first.ms == 800, "reduced motion and the transition time cross");

    read(hold, seen, &applied);
    check(!applied, "the same look is not applied twice");

    look.scene_id = "tide";
    hold.set(look);
    const Seen second = read(hold, seen, &applied);
    check(applied && second.scene == "tide", "a new set is applied once");

    uint64_t fresh = 0;
    const Seen again = read(hold, fresh, &applied);
    check(applied && again.scene == "tide", "a renderer made later is given the whole look");
}

void deepCopyAndNulls() {
    CastLook hold;
    {
        std::string scene = "ember";
        std::string reactions = "";
        std::vector<float> tuning = {1.0f, 2.0f};
        geode_viz_look look{};
        look.scene_id = scene.c_str();
        look.reactions = reactions.c_str();
        look.tuning = tuning.data();
        look.tuning_count = 2;
        hold.set(look);
        scene = "gone";
        tuning.assign(2, 9.0f);
    }
    uint64_t seen = 0;
    bool applied = false;
    const Seen kept = read(hold, seen, &applied);
    check(kept.scene == "ember", "the scene text is a copy");
    check(!kept.reactionsNull && kept.reactions.empty(), "an empty reactions text is not null");
    check(kept.lfosNull, "an unset lfos text stays null");
    check(kept.tuningCount == 2 && !kept.tuningNull, "the tuning crosses");
    check(kept.params.empty(), "no params: none cross");

    geode_viz_look auto_{};
    auto_.scene_id = "ember";
    hold.set(auto_);
    const Seen reset = read(hold, seen, &applied);
    check(applied && reset.reactionsNull, "reactions set null again are null (Auto)");
    check(reset.tuningNull && reset.tuningCount == 0, "tuning set null again is none");
}

}  // namespace

int main() {
    nothingBeforeASet();
    appliedOnceThenAgainAfterANewSet();
    deepCopyAndNulls();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("cast_look_test: all checks passed\n");
    return 0;
}
