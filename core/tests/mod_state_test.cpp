// Host-side checks for the modulation the app holds while it runs (core/viz/modulation/ModState, api/geode_mod_state_api.cpp):
// a panel event applies the same rules as the document's own edits, the routes go back to Auto when none is the
// user's, the keep set is normalised and toggled, and the hand mask and the keep set reach the process-wide
// setters. geode_hand_set and geode_director_keep_set are recorded here instead of reaching a director.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Modulation document host tests"); no GL.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_modulation_api.h"
#include "viz/params/Config.hpp"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ModState.hpp"
#include "viz/panel/ParamRandomizer.hpp"
#include "viz/modulation/Reactions.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

struct HandPush {
    unsigned mask;
    int user;
};

std::vector<HandPush> handPushes;
std::vector<std::string> keepPushes;

// A text the C ABI hands out, in full.
std::string textOf(int which) {
    const int need = geode_mod_state_text(which, nullptr, 0);
    std::vector<char> out(static_cast<size_t>(need) + 1);
    geode_mod_state_text(which, out.data(), need + 1);
    return std::string(out.data());
}

std::string lfoEdit(const std::string& lfos, int slot, int field, float value) {
    const int need = geode_mod_lfo_edit(lfos.c_str(), slot, field, value, nullptr, 0);
    std::vector<char> out(static_cast<size_t>(need) + 1);
    geode_mod_lfo_edit(lfos.c_str(), slot, field, value, out.data(), need + 1);
    return std::string(out.data());
}

std::string adsrEdit(const std::string& adsrs, int slot, int field, float value) {
    const int need = geode_mod_adsr_edit(adsrs.c_str(), slot, field, value, nullptr, 0);
    std::vector<char> out(static_cast<size_t>(need) + 1);
    geode_mod_adsr_edit(adsrs.c_str(), slot, field, value, out.data(), need + 1);
    return std::string(out.data());
}

void aLoadHoldsTheSavedTextsAsTheyAre() {
    modstate::ModState state;
    state.load("not a list", "", std::nullopt, GEODE_HAND_ALL);
    check(state.lfos() == "not a list", "the saved waves are held as saved");
    check(!state.reactions().has_value(), "no saved routes: Auto");
    check(state.hand().mask == 0u && !state.hand().user, "Auto holds no mask, whatever was saved");

    state.load("", "", std::string("reactions/2"), 6u);
    check(state.reactions() == std::optional<std::string>("reactions/2"), "the user's routes are held");
    check(state.hand().mask == 6u && state.hand().user, "the user's routes keep their mask");
}

void anEventAppliesTheDocumentsOwnEdit() {
    modstate::ModState state;
    state.load("", "", std::nullopt, 0u);
    const int depth = static_cast<int>(LfoField::Depth);
    state.setLfo(1, depth, 99.0f);
    check(state.lfos() == lfoEdit("", 1, depth, 99.0f), "a wave event is geode_mod_lfo_edit's edit");
    ModDocument read;
    read.readLfos(state.lfos());
    check(read.lfos[1].depth == config::spec(config::Slot::lfo_depth).max, "a wave's depth lands inside its range");
    const std::string before = state.lfos();
    state.setLfo(99, depth, 0.5f);
    check(state.lfos() == before, "an unknown wave changes nothing");

    const int attack = static_cast<int>(AdsrField::Attack);
    state.setAdsr(0, attack, -5.0f);
    check(state.adsrs() == adsrEdit("", 0, attack, -5.0f), "an envelope event is geode_mod_adsr_edit's edit");

    state.setLfos("rolled");
    check(state.lfos() == "rolled", "a roll's waves are held as it wrote them");
}

void reactionEventsMakeTheListTheUsersAndClearToAuto() {
    const StyleDna none{};
    const std::string style;
    modstate::ModState state;
    state.load("", "", std::nullopt, 0u);
    state.editReactions(none, style, static_cast<int>(ReactionEdit::RouteAdd), 0, 0, 0.0f);
    check(state.reactions().has_value() && !state.reactions()->empty(), "adding a route makes a list of the user's");
    check(state.hand().user && state.hand().mask == hand::kAll, "a whole list of the user's owns every moment");

    ModDocument doc;
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::RouteAdd), 0, 0, 0.0f);
    check(*state.reactions() == doc.reactionsText(), "the event is the document's edit");

    state.editReactions(none, style, -1, 0, 0, 0.0f);
    check(*state.reactions() == doc.reactionsText(), "an edit that names nothing changes nothing");

    state.editReactions(none, style, static_cast<int>(ReactionEdit::Clear), 0, 0, 0.0f);
    check(!state.reactions().has_value() && state.hand().mask == 0u && !state.hand().user, "clearing is Auto again, no mask");
}

void theKeepSetIsNormalisedAndToggled() {
    const std::string key = randomizer::rolledKeys().front().key;
    modstate::ModState state;
    state.loadKeep(key + "\nno_such_control\n#PRO\n" + key);
    check(state.keep() == key + "\n", "a saved keep set is held normalised");
    state.toggleKeep(key);
    check(state.keep().empty(), "a tap on a kept control lets it go");
    state.toggleKeep("#LOOK");
    check(state.keep() == "#LOOK\n", "a tap on a category keeps it");
    state.toggleKeep("no_such_control");
    check(state.keep() == "#LOOK\n", "a tap on an unknown key changes nothing");
}

void theAbiHoldsOneStateAndPushesItsSetters() {
    handPushes.clear();
    keepPushes.clear();
    geode_mod_state_load("", "", nullptr, GEODE_HAND_ALL);
    check(handPushes.size() == 1 && handPushes[0].mask == 0u && handPushes[0].user == 0, "a load with no routes pushes Auto");
    check(textOf(GEODE_MOD_STATE_REACTIONS).empty() && geode_mod_state_hand_mask() == 0u, "Auto reads as no routes, no mask");

    geode_mod_state_load("", "", "reactions/2", 5u);
    check(handPushes.size() == 2 && handPushes[1].mask == 5u && handPushes[1].user == 1, "a load with routes pushes their mask");
    check(textOf(GEODE_MOD_STATE_REACTIONS) == "reactions/2" && geode_mod_state_hand_mask() == 5u, "the routes read back");

    const int depth = static_cast<int>(LfoField::Depth);
    geode_mod_state_lfo_set(0, depth, 0.25f);
    check(textOf(GEODE_MOD_STATE_LFOS) == lfoEdit("", 0, depth, 0.25f), "a wave event reads back as the edit");
    geode_mod_state_lfo_set(0, depth, 0.75f);
    check(textOf(GEODE_MOD_STATE_LFOS) == lfoEdit(lfoEdit("", 0, depth, 0.25f), 0, depth, 0.75f), "events build on what is held");
    const int sustain = static_cast<int>(AdsrField::Sustain);
    geode_mod_state_adsr_set(1, sustain, 0.5f);
    check(textOf(GEODE_MOD_STATE_ADSRS) == adsrEdit("", 1, sustain, 0.5f), "an envelope event reads back as the edit");

    const std::string key = randomizer::rolledKeys().front().key;
    geode_mod_state_keep_load((key + "\n#NOPE\n").c_str());
    check(textOf(GEODE_MOD_STATE_KEEP) == key + "\n", "the saved keep set reads back normalised");
    check(keepPushes.size() == 1 && keepPushes[0] == key + "\n", "the director gets the keep set at load");
    geode_mod_state_keep_toggle(key.c_str());
    check(textOf(GEODE_MOD_STATE_KEEP).empty(), "a Keep tap reads back");
    check(keepPushes.size() == 2 && keepPushes[1].empty(), "the director gets the set after a tap");
    geode_mod_state_keep_toggle(nullptr);
    check(keepPushes.size() == 3 && keepPushes[2].empty(), "a tap on nothing keeps the set as it was");

    check(textOf(99).empty(), "an unknown text reads empty");
    char shortBuffer[4] = {'x', 'x', 'x', 'x'};
    const int need = geode_mod_state_text(GEODE_MOD_STATE_LFOS, shortBuffer, 4);
    check(need == static_cast<int>(textOf(GEODE_MOD_STATE_LFOS).size()) && shortBuffer[3] == '\0',
          "a short buffer gets what fits and the length");
}

}  // namespace

// The process-wide setters, recorded.
extern "C" void geode_hand_set(unsigned mask, int has_user) { handPushes.push_back({mask, has_user}); }
extern "C" void geode_director_keep_set(const char* keys) { keepPushes.emplace_back(keys ? keys : ""); }

int main() {
    aLoadHoldsTheSavedTextsAsTheyAre();
    anEventAppliesTheDocumentsOwnEdit();
    reactionEventsMakeTheListTheUsersAndClearToAuto();
    theKeepSetIsNormalisedAndToggled();
    theAbiHoldsOneStateAndPushesItsSetters();
    if (failures) {
        std::printf("%d modulation state check(s) failed\n", failures);
        return 1;
    }
    std::printf("modulation state checks passed\n");
    return 0;
}
