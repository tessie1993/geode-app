#include "viz/modulation/ModState.hpp"

#include <utility>

#include "viz/panel/CustomizeMap.hpp"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ReactionText.hpp"

namespace geode::viz::modstate {

void ModState::load(std::string lfos, std::string adsrs, std::optional<std::string> reactions, std::uint32_t mask) {
    lfos_ = std::move(lfos);
    adsrs_ = std::move(adsrs);
    reactions_ = std::move(reactions);
    user_ = reactions_ && !reactiontext::autoMarked(*reactions_);
    mask_ = user_ ? mask : 0u;
}

void ModState::setLfo(int slot, int field, float value) {
    ModDocument doc;
    doc.readLfos(lfos_);
    doc.setLfo(slot, field, value);
    lfos_ = doc.lfosText();
}

void ModState::setAdsr(int slot, int field, float value) {
    ModDocument doc;
    doc.readAdsrs(adsrs_);
    doc.setAdsr(slot, field, value);
    adsrs_ = doc.adsrsText();
}

void ModState::setLfos(std::string lfos) { lfos_ = std::move(lfos); }

void ModState::editReactions(const StyleDna& dna, const std::string& styleId, int op, int a, int b, float value) {
    ModDocument doc;
    if (reactions_) doc.readReactions(*reactions_, mask_);
    doc.editReactions(dna, styleId, op, a, b, value);
    std::string routes = doc.reactionsText();
    if (routes.empty()) {
        reactions_.reset();
        user_ = false;
        mask_ = 0u;
        return;
    }
    mask_ = doc.handMask();
    user_ = doc.hand.user;
    reactions_ = std::move(routes);
}

void ModState::loadKeep(std::string_view document) { keep_ = customize::keepNormalize(document); }

void ModState::toggleKeep(std::string_view key) { keep_ = customize::keepToggle(keep_, key); }

Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace geode::viz::modstate
