#pragma once

// What pause, seek and a track change do to a running capture take (#459, ADR 0008): Settings › Behaviour ›
// Record offers three rules, and this is the one place that decides. Pure, so core/tests/take_rules_test.cpp
// walks every rule through every event on the host; output::TakeSession carries the answer out (holding the
// picture and the sound, or ending the take) and the shell only reports what the player did.
namespace geode::output {

// The Settings choice, in its wire order (geode_take_set_rule).
enum class TakeRule : int {
    PauseHoldsSeekCuts = 0,  // the default: pause holds the capture, a seek is a hard cut, a new track ends and saves
    Continuous = 1,          // keeps rolling through all of it; a take can span tracks
    PauseHoldsSeekEnds = 2,  // pause holds, and any seek (or a new track) ends the take
};

// What the player did, in wire order (geode_take_event).
enum class TakeEvent : int {
    Pause = 0,
    Resume = 1,
    Seek = 2,
    TrackChange = 3,
};

// What the take does about it, in wire order (geode_take_event's return).
enum class TakeAction : int {
    Roll = 0,     // nothing changes: picture and sound keep running
    Hold = 1,     // picture and sound stop; the take's clock stops with them, so the file has no gap
    Release = 2,  // picture and sound run again from where they held
    Cut = 3,      // a hard cut: the take runs straight on into the new position
    End = 4,      // the take ends and is saved
};

constexpr TakeRule kDefaultTakeRule = TakeRule::PauseHoldsSeekCuts;

// A rule read from the wire; anything unknown (a later build's choice) is the default.
TakeRule takeRuleOf(int wire);

// One take's state under one rule: whether it is holding. The rule is fixed when the take begins, so changing
// the setting mid-take never turns a held take into one that cannot be released.
class TakeRules {
public:
    explicit TakeRules(TakeRule rule = kDefaultTakeRule) : rule_(rule) {}

    TakeAction on(TakeEvent event);

    TakeRule rule() const { return rule_; }
    bool holding() const { return holding_; }
    bool ended() const { return ended_; }

private:
    TakeRule rule_;
    bool holding_ = false;
    bool ended_ = false;
};

}  // namespace geode::output
