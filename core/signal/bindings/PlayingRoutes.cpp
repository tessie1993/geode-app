#include "signal/bindings/PlayingRoutes.hpp"

namespace geode::signal {

void PlayingRoutes::setUser(const viz::HandRoutes& user) {
    user_ = user;
    if (!user_.user) user_.count = 0;
    userDirty_ = true;
}

bool PlayingRoutes::resolve(const std::string& styleId, int seed, std::uint32_t handMask, TraitsOf traitsOf) {
    // The mask means nothing without a list of the user's, so alone it does not re-deal the DNA.
    const std::uint32_t mask = user_.user ? (handMask & viz::hand::kAll) : 0u;
    if (resolved_ && !userDirty_ && seed == seed_ && mask == mask_ && styleId == styleId_) return false;
    styleId_ = styleId;
    seed_ = seed;
    mask_ = mask;
    userDirty_ = false;
    resolved_ = true;
    const viz::StyleTraits traits = traitsOf ? traitsOf(styleId) : viz::StyleTraits{};
    const viz::StyleDna dna = viz::buildStyleDna(styleId, seed, traits);
    viz::HandRoutes hand = user_;
    hand.mask = mask;
    count_ = viz::mergeHand(hand, dna, routes_);
    return true;
}

}  // namespace geode::signal
