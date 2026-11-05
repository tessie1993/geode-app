#include "output/TakeRules.hpp"

namespace geode::output {

TakeRule takeRuleOf(int wire) {
    switch (wire) {
        case static_cast<int>(TakeRule::Continuous):
            return TakeRule::Continuous;
        case static_cast<int>(TakeRule::PauseHoldsSeekEnds):
            return TakeRule::PauseHoldsSeekEnds;
        default:
            return kDefaultTakeRule;
    }
}

TakeAction TakeRules::on(TakeEvent event) {
    // An ended take hears nothing more: the shell may still report the pause its own stop caused.
    if (ended_) return TakeAction::Roll;
    if (rule_ == TakeRule::Continuous) return TakeAction::Roll;
    switch (event) {
        case TakeEvent::Pause:
            if (holding_) return TakeAction::Roll;
            holding_ = true;
            return TakeAction::Hold;
        case TakeEvent::Resume:
            if (!holding_) return TakeAction::Roll;
            holding_ = false;
            return TakeAction::Release;
        case TakeEvent::Seek:
            if (rule_ == TakeRule::PauseHoldsSeekEnds) {
                ended_ = true;
                return TakeAction::End;
            }
            // A seek while held stays held: the cut shows once play resumes.
            return TakeAction::Cut;
        case TakeEvent::TrackChange:
            ended_ = true;
            return TakeAction::End;
    }
    return TakeAction::Roll;
}

}  // namespace geode::output
