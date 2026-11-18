#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "viz/ReactionDna.hpp"

// The user's modulation as the app holds it while it runs (geode_mod_state_*): the waves, the envelopes, the
// reaction routes with their hand mask, and Customize's keep set. The panel sends each edit here as an event
// and gets nothing back from it; this applies the edit (core/viz/ModDocument and CustomizeMap decide every
// field, range and toggle) and keeps the result. The shell reads the texts back only to save them and to draw
// the editors; a renderer still gets them through its look. Host-tested in core/tests/mod_state_test.cpp.
namespace geode::viz::modstate {

// What geode_hand_set takes: the moments the user's routes own, and whether there is a list of the user's.
struct Hand {
    std::uint32_t mask = 0;
    bool user = false;
};

class ModState {
public:
    // The texts as saved: the waves and the envelopes as they are, the user's routes (nullopt while Auto) and
    // the mask native decided for them when they were read (geode_mod_hand_load). Auto holds no mask.
    void load(std::string lfos, std::string adsrs, std::optional<std::string> reactions, std::uint32_t mask);

    // One field of one wave or envelope (ModDocument::setLfo, setAdsr): the texts are written back in full.
    void setLfo(int slot, int field, float value);
    void setAdsr(int slot, int field, float value);
    // A roll's waves (geode_mod_waves_roll), as it wrote them.
    void setLfos(std::string lfos);

    // One reaction edit (ModDocument::editReactions) over the style's own routes in dna. When no route is the
    // user's any more, every moment is the style's again: Auto, and no mask.
    void editReactions(const StyleDna& dna, const std::string& styleId, int op, int a, int b, float value);

    // The keep set as saved, normalised (customize::keepNormalize); and one Keep tap (customize::keepToggle).
    void loadKeep(std::string_view document);
    void toggleKeep(std::string_view key);

    const std::string& lfos() const { return lfos_; }
    const std::string& adsrs() const { return adsrs_; }
    const std::optional<std::string>& reactions() const { return reactions_; }
    const std::string& keep() const { return keep_; }
    Hand hand() const { return {reactions_ ? mask_ : 0u, reactions_.has_value()}; }

private:
    std::string lfos_;
    std::string adsrs_;
    std::optional<std::string> reactions_;
    std::uint32_t mask_ = 0;
    std::string keep_;
};

// The one the app holds, process-wide: the panel's events come on the main thread, the keep set's first read on
// the director's start (IO). Lock before touching it.
struct Shared {
    std::mutex lock;
    ModState state;
};

Shared& shared();

}  // namespace geode::viz::modstate
