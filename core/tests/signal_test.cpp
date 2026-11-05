// Host test of core/signal's params and events: the schema's one clamp, every cell of the param state
// machine's transition table, glides, trials and their spring back, the timeline taking and giving back a
// param, modulation that sums before one clamp (so a late stage can still pull back an early one), the
// sound-frame event detector, the bus's order and re-entrancy, and event routes (every Nth, steps,
// toggles, the finger still winning, and the flash limit on luminance pulses). Then two identical
// pipelines over 600 frames give bit-identical params.
// After those, a mutation pass: each case names the mutant of the PR's code it kills (range ends, NaN, the
// state machine's origin checks, the 4096 wrap, a route firing once per event, the flash clock), plus seeded
// properties (raw mt19937 words) over random inputs: values stay in range, a finger and a timeline win, and
// modulation order never matters.
// Built by CI (.github/workflows/android.yml, "Native host tests") from core/signal alone.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "signal/bindings/EventRoutes.hpp"
#include "signal/events/EventBus.hpp"
#include "signal/events/SoundEvents.hpp"
#include "signal/params/ParamStore.hpp"

using namespace geode::signal;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A small schema with one row of each kind.
enum : ParamId { kZoom, kHue, kFolds, kMirror, kBrightness, kParamCount };

ParamSchema schema() {
    return ParamSchema({
        {"zoom", ParamKind::Float, 1.0f, 0.3f, 3.0f, false},
        {"hue", ParamKind::Circular, 0.0f, 0.0f, 1.0f, false},
        {"symmetry", ParamKind::Int, 6.0f, 1.0f, 12.0f, false},
        {"mirror", ParamKind::Bool, 0.0f, 0.0f, 1.0f, false},
        {"brightness", ParamKind::Float, 1.0f, 0.0f, 2.0f, true},
    });
}

void testSchema() {
    const ParamSchema s = schema();
    check(s.size() == kParamCount, "schema keeps every row");
    check(s.idsUnique(), "schema ids unique");
    check(s.find("symmetry") == std::optional<ParamId>(kFolds), "find by id");
    check(!s.find("Symmetry").has_value(), "ids are exact, never labels");
    check(near(s.clamp(kZoom, 9.0f), 3.0f) && near(s.clamp(kZoom, -1.0f), 0.3f), "float clamps");
    check(near(s.clamp(kZoom, NAN), 1.0f) && near(s.clamp(kZoom, INFINITY), 1.0f), "non-finite becomes the default");
    check(near(s.clamp(kFolds, 6.6f), 7.0f) && near(s.clamp(kFolds, 40.0f), 12.0f), "int rounds then clamps");
    check(near(s.clamp(kMirror, 0.49f), 0.0f) && near(s.clamp(kMirror, 0.5f), 1.0f), "bool snaps at one half");
    check(near(s.clamp(kHue, 1.25f), 0.25f) && near(s.clamp(kHue, -0.25f), 0.75f) && near(s.clamp(kHue, 1.0f), 0.0f),
          "circular wraps into [min, max)");
    const ParamSchema swapped({{"x", ParamKind::Float, 9.0f, 5.0f, 1.0f, false}});
    check(near(swapped.def(0).min, 1.0f) && near(swapped.def(0).max, 5.0f) && near(swapped.def(0).defaultValue, 5.0f),
          "a reversed range is swapped and the default clamped into it");
}

// ---- the state machine's table, cell by cell ----------------------------------------------------------

const ParamDef kDef{"zoom", ParamKind::Float, 1.0f, 0.3f, 3.0f, false};

ParamCell inPhase(ParamPhase phase) {
    ParamCell c = restingCell(kDef);
    switch (phase) {
        case ParamPhase::Rest: break;
        case ParamPhase::Glide:
            // A quarter of the way: off the default, so a Reset has somewhere to glide.
            apply(c, ParamInput::glideTo(2.0f, 1.0f), kDef);
            advance(c, 0.25f, kDef);
            break;
        case ParamPhase::Held: apply(c, ParamInput::hold(), kDef); break;
        case ParamPhase::Trial: apply(c, ParamInput::tryValue(2.5f), kDef); break;
        case ParamPhase::Driven: apply(c, ParamInput::drive(), kDef); break;
    }
    return c;
}

struct Row {
    ParamPhase from;
    ParamInput input;
    Outcome outcome;
    ParamPhase to;
};

void testTransitionTable() {
    using P = ParamPhase;
    using O = Outcome;
    const Origin U = Origin::User, Pr = Origin::Preset, T = Origin::Timeline, E = Origin::Engine;
    const Row rows[] = {
        // Rest
        {P::Rest, ParamInput::set(2.0f, U), O::Applied, P::Rest},
        {P::Rest, ParamInput::set(2.0f, E), O::Applied, P::Rest},
        {P::Rest, ParamInput::glideTo(2.0f, 1.0f, Pr), O::Applied, P::Glide},
        {P::Rest, ParamInput::glideTo(2.0f, 0.0f, Pr), O::Applied, P::Rest},
        {P::Rest, ParamInput::hold(), O::Applied, P::Held},
        {P::Rest, ParamInput::release(), O::Ignored, P::Rest},
        {P::Rest, ParamInput::tryValue(2.5f), O::Applied, P::Trial},
        {P::Rest, ParamInput::drive(), O::Applied, P::Driven},
        {P::Rest, ParamInput::undrive(), O::Ignored, P::Rest},
        {P::Rest, ParamInput::reset(0.5f), O::Applied, P::Rest},  // already on the default: lands at once
        // Glide
        {P::Glide, ParamInput::set(2.0f, U), O::Applied, P::Rest},
        {P::Glide, ParamInput::glideTo(0.5f, 1.0f, Pr), O::Applied, P::Glide},
        {P::Glide, ParamInput::hold(), O::Applied, P::Held},
        {P::Glide, ParamInput::release(), O::Ignored, P::Glide},
        {P::Glide, ParamInput::tryValue(2.5f), O::Applied, P::Trial},
        {P::Glide, ParamInput::drive(), O::Applied, P::Driven},
        {P::Glide, ParamInput::reset(0.5f), O::Applied, P::Glide},
        // Held
        {P::Held, ParamInput::set(2.0f, U), O::Applied, P::Held},
        {P::Held, ParamInput::set(2.0f, Pr), O::Rejected, P::Held},
        {P::Held, ParamInput::set(2.0f, E), O::Rejected, P::Held},
        {P::Held, ParamInput::glideTo(2.0f, 1.0f, Pr), O::Rejected, P::Held},
        {P::Held, ParamInput::hold(), O::Ignored, P::Held},
        {P::Held, ParamInput::release(), O::Applied, P::Rest},
        {P::Held, ParamInput::tryValue(2.5f), O::Applied, P::Trial},
        {P::Held, ParamInput::drive(), O::Rejected, P::Held},
        {P::Held, ParamInput::undrive(), O::Ignored, P::Held},
        {P::Held, ParamInput::reset(0.5f), O::Rejected, P::Held},
        // Trial
        {P::Trial, ParamInput::set(2.0f, U), O::Applied, P::Trial},
        {P::Trial, ParamInput::set(2.0f, Pr), O::Applied, P::Trial},
        {P::Trial, ParamInput::set(2.0f, E), O::Rejected, P::Trial},
        {P::Trial, ParamInput::set(2.0f, T), O::Rejected, P::Trial},
        {P::Trial, ParamInput::glideTo(2.0f, 1.0f, Pr), O::Applied, P::Trial},
        {P::Trial, ParamInput::hold(), O::Applied, P::Trial},
        {P::Trial, ParamInput::release(), O::Applied, P::Glide},
        {P::Trial, ParamInput::tryValue(2.8f), O::Applied, P::Trial},
        {P::Trial, ParamInput::drive(), O::Applied, P::Driven},
        {P::Trial, ParamInput::undrive(), O::Ignored, P::Trial},
        {P::Trial, ParamInput::reset(0.5f), O::Applied, P::Trial},
        // Driven
        {P::Driven, ParamInput::set(2.0f, T), O::Applied, P::Driven},
        {P::Driven, ParamInput::set(2.0f, U), O::Rejected, P::Driven},
        {P::Driven, ParamInput::glideTo(2.0f, 1.0f, Pr), O::Rejected, P::Driven},
        {P::Driven, ParamInput::glideTo(2.0f, 1.0f, T), O::Applied, P::Driven},
        {P::Driven, ParamInput::hold(), O::Rejected, P::Driven},
        {P::Driven, ParamInput::release(), O::Ignored, P::Driven},
        {P::Driven, ParamInput::tryValue(2.5f), O::Rejected, P::Driven},
        {P::Driven, ParamInput::drive(), O::Ignored, P::Driven},
        {P::Driven, ParamInput::reset(0.5f), O::Rejected, P::Driven},
    };
    for (const Row& r : rows) {
        ParamCell c = inPhase(r.from);
        const Outcome got = apply(c, r.input, kDef);
        const std::string name = std::string(phaseName(r.from)) + " + " + inputName(r.input.kind);
        check(got == r.outcome, "outcome of " + name);
        check(c.phase == r.to, "phase after " + name + " is " + phaseName(r.to) + ", got " + phaseName(c.phase));
    }
    // Undrive from Driven glides back to what the user had, over the spring-back time.
    ParamCell c = inPhase(ParamPhase::Rest);
    apply(c, ParamInput::set(2.0f), kDef);
    apply(c, ParamInput::drive(), kDef);
    apply(c, ParamInput::set(0.5f, Origin::Timeline), kDef);
    check(near(c.base, 0.5f) && near(c.committed, 2.0f), "a timeline moves the base, the user's value waits underneath");
    check(apply(c, ParamInput::undrive(), kDef) == Outcome::Applied && c.phase == ParamPhase::Glide, "undrive glides back");
    advance(c, kSpringBackSeconds + 0.01f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 2.0f), "and lands on the user's value");
}

void testGlide() {
    ParamCell c = restingCell(kDef);
    apply(c, ParamInput::glideTo(3.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    check(near(c.committed, 3.0f) && near(c.base, 1.0f), "a glide commits its target at once and starts where it was");
    advance(c, 0.5f, kDef);
    check(near(c.base, 2.0f), "linear glide is halfway at half time");
    advance(c, 0.6f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 3.0f), "a glide lands on its target and rests");
    // A hold stops a glide where it is.
    apply(c, ParamInput::glideTo(1.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    advance(c, 0.25f, kDef);
    apply(c, ParamInput::hold(), kDef);
    check(c.phase == ParamPhase::Held && near(c.base, 2.5f) && near(c.committed, 2.5f), "a finger stops a glide where it is");
    // Int and Bool never interpolate.
    const ParamDef folds{"symmetry", ParamKind::Int, 6.0f, 1.0f, 12.0f, false};
    ParamCell f = restingCell(folds);
    apply(f, ParamInput::glideTo(9.0f, 1.0f), folds);
    check(f.phase == ParamPhase::Rest && near(f.base, 9.0f), "an int glide lands at once");
    // A circular glide goes the short way round.
    const ParamDef hue{"hue", ParamKind::Circular, 0.9f, 0.0f, 1.0f, false};
    ParamCell h = restingCell(hue);
    apply(h, ParamInput::glideTo(0.1f, 1.0f, Origin::Preset, Ease::Linear), hue);
    advance(h, 0.5f, hue);
    check(near(h.base, 0.0f, 1e-3f) || near(h.base, 1.0f, 1e-3f), "a hue glides across the wrap, not the long way");
}

void testTrial() {
    ParamCell c = restingCell(kDef);
    apply(c, ParamInput::set(1.5f), kDef);
    apply(c, ParamInput::tryValue(2.8f, 1.0f), kDef);
    check(near(c.base, 2.8f) && near(c.committed, 1.5f), "a trial shows the tried value over the kept one");
    advance(c, 0.9f, kDef);
    check(c.phase == ParamPhase::Trial, "an untouched trial shows until its time is up");
    advance(c, 0.2f, kDef);
    check(c.phase == ParamPhase::Glide, "then springs back");
    advance(c, kSpringBackSeconds, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 1.5f), "to the kept value");
    // Dragging a locked slider: held, no timeout, springs back on release.
    apply(c, ParamInput::hold(), kDef);
    apply(c, ParamInput::tryValue(2.0f), kDef);
    apply(c, ParamInput::set(2.9f), kDef);
    advance(c, 10.0f, kDef);
    check(c.phase == ParamPhase::Trial && near(c.base, 2.9f), "a held trial follows the finger and never times out");
    apply(c, ParamInput::set(0.4f, Origin::Preset), kDef);
    check(near(c.committed, 0.4f), "a preset during a trial moves what it springs back to");
    apply(c, ParamInput::release(), kDef);
    advance(c, kSpringBackSeconds + 0.01f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 0.4f), "release springs back to the preset's value");
}

void testModulation() {
    ParamStore store(schema());
    store.input(kZoom, ParamInput::set(1.0f));
    store.modulate(kZoom, Combine::Add, 100.0f);
    check(near(store.effective(kZoom), 3.0f), "a huge modulation clamps to the max");
    store.modulate(kZoom, Combine::Add, -100.0f);
    check(near(store.effective(kZoom), 1.0f), "and a later one pulls it back: one clamp, at the end");
    store.clearModulation();
    store.modulate(kZoom, Combine::Multiply, 2.0f);
    store.modulate(kZoom, Combine::Add, 0.25f);
    check(near(store.effective(kZoom), 2.25f), "base x multiply + add");
    store.clearModulation();
    store.modulate(kZoom, Combine::Add, 0.25f);
    store.modulate(kZoom, Combine::Multiply, 2.0f);
    check(near(store.effective(kZoom), 2.25f), "the order modulators run in does not matter");
    store.clearModulation();
    store.modulate(kZoom, Combine::Toward, 3.0f, 0.5f);
    check(near(store.effective(kZoom), 2.0f), "toward pulls by its weight");
    store.modulate(kZoom, Combine::Multiply, 2.0f, 0.0f);
    check(near(store.effective(kZoom), 2.0f), "a weight of 0 leaves a factor out");
    store.clearModulation();
    store.modulate(kZoom, Combine::Add, NAN);
    store.modulate(kZoom, Combine::Add, 1.0f, INFINITY);
    check(near(store.effective(kZoom), 1.0f), "non-finite modulation is dropped");
    store.clearModulation();
    store.input(kHue, ParamInput::set(0.9f));
    store.modulate(kHue, Combine::Add, 0.2f);
    check(near(store.effective(kHue), 0.1f), "a hue adds round its circle");
    store.clearModulation();
    store.modulate(kHue, Combine::Toward, 0.1f, 1.0f);
    check(near(store.effective(kHue), 0.1f), "and is pulled the short way");
    // Held: the finger's value exactly.
    store.clearModulation();
    store.input(kZoom, ParamInput::hold());
    store.input(kZoom, ParamInput::set(1.7f));
    store.modulate(kZoom, Combine::Add, 0.5f);
    check(near(store.effective(kZoom), 1.7f), "a held control shows exactly what the finger set");
    store.input(kZoom, ParamInput::release());
    check(near(store.effective(kZoom), 2.2f), "and the modulation comes back on release");
    check(store.input(static_cast<ParamId>(99), ParamInput::set(1.0f)) == Outcome::Rejected, "unknown ids are rejected");
}

// ---- events ----------------------------------------------------------------------------------------------

GeodeSoundFrame quietFrame() {
    GeodeSoundFrame f{};
    f.sectionState = GEODE_SECTION_VERSE;
    return f;
}

void testWireIds() {
    check(eventWireId(EventKind::Kick) == "kick" && eventWireId(EventKind::BuildStart) == "build_start", "wire ids");
    for (std::size_t i = 0; i < kEventKindCount; ++i) {
        const auto k = static_cast<EventKind>(i);
        check(eventKindFromWire(eventWireId(k)) == std::optional<EventKind>(k), "wire id round trip " + std::to_string(i));
    }
    check(!eventKindFromWire("Kick").has_value(), "wire ids are exact");
}

void testSoundEvents() {
    SoundEvents detect;
    EventQueue q;
    GeodeSoundFrame f = quietFrame();
    detect.detect(f, 0.0, 1.0f / 60.0f, q);
    check(q.empty(), "a quiet frame raises nothing");

    q.clear();
    f.kick = 0.8f;
    f.downbeat = 1.0f;
    detect.detect(f, 1.0, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Kick && near(q[0].strength, 0.8f) && q[1].kind == EventKind::Downbeat,
          "triggers raise their events, in table order, with the trigger's strength");

    q.clear();
    f = quietFrame();
    f.sectionState = GEODE_SECTION_BUILD;
    detect.detect(f, 2.0, 1.0f / 60.0f, q);
    check(q.size() == 1 && q[0].kind == EventKind::BuildStart, "entering a build raises BuildStart once");
    q.clear();
    detect.detect(f, 2.1, 1.0f / 60.0f, q);
    check(q.empty(), "staying in it raises nothing");
    f.sectionState = GEODE_SECTION_DROP;
    f.drop = 1.0f;
    detect.detect(f, 2.2, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Drop && q[1].kind == EventKind::BuildEnd, "a drop ends the build");

    q.clear();
    f = quietFrame();
    f.sectionChange = 1.0f;
    f.sectionId = 3.0f;
    detect.detect(f, 3.0, 1.0f / 60.0f, q);
    check(q.size() == 1 && q[0].kind == EventKind::SectionChange && q[0].value == 3, "a section change carries its id");

    // Counters: two beats between drawn frames are two Beat events, not one.
    SoundEvents grid;
    GeodeSoundFrame g = quietFrame();
    g.beatCount = 10.0f;
    g.barCount = 2.0f;
    q.clear();
    grid.detect(g, 5.0, 1.0f / 60.0f, q);
    check(q.empty(), "the first frame only seeds the counters");
    g.beat = 0.9f;
    g.beatCount = 12.0f;
    q.clear();
    grid.detect(g, 5.1, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Beat && q[1].kind == EventKind::Beat && near(q[1].strength, 0.9f),
          "two beats between frames raise two Beat events");
    g.beat = 1.0f;
    g.downbeat = 1.0f;
    g.beatCount = 13.0f;
    g.barCount = 3.0f;
    q.clear();
    grid.detect(g, 5.2, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Beat && q[1].kind == EventKind::Downbeat, "one beat, one downbeat");
    g.beat = 0.0f;
    g.downbeat = 0.0f;
    g.beatCount = 15.0f;
    q.clear();
    grid.detect(g, 5.3, 1.0f / 60.0f, q);
    check(q.empty(), "unlocked beats tick the counter but raise nothing (no trigger)");
    g.beat = 1.0f;
    g.beatCount = 100.0f;
    q.clear();
    grid.detect(g, 5.4, 1.0f / 60.0f, q);
    check(q.size() == static_cast<std::size_t>(SoundEvents::kMaxBurst), "a stall's burst is capped");
    g.beat = 1.0f;
    g.downbeat = 1.0f;
    g.beatCount = 1.0f;
    g.barCount = 0.0f;
    q.clear();
    grid.detect(g, 5.5, 1.0f / 60.0f, q);
    check(q.empty(), "a counter going backwards (a reset) is a resync and raises nothing");
    g.beatCount = 2.0f;
    g.barCount = 1.0f;
    q.clear();
    grid.detect(g, 5.6, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Beat && q[1].kind == EventKind::Downbeat, "and counts on from the new base");
    g.downbeat = 0.0f;
    g.beatCount = 4095.0f;
    grid.detect(g, 5.7, 1.0f / 60.0f, q);
    g.beatCount = 1.0f;
    q.clear();
    grid.detect(g, 5.8, 1.0f / 60.0f, q);
    check(q.size() == 2 && q[0].kind == EventKind::Beat && q[1].kind == EventKind::Beat, "the 4096 wrap counts forward");

    // Key: the frame's key (Harmony's, already held) changing value; no rule of our own on top.
    const auto setKey = [](GeodeSoundFrame& fr, int tonic, bool minor) {
        fr.keyRoot = static_cast<float>(tonic) / 12.0f;
        fr.keyHue = static_cast<float>(((minor ? tonic + 3 : tonic) * 7) % 12) / 12.0f;
        fr.keyMajor = minor ? 0.2f : 0.8f;
    };
    SoundEvents keys;
    GeodeSoundFrame k = quietFrame();
    k.keyClarity = 0.8f;
    setKey(k, 7, false);  // G major
    check(keyOf(k) == 7, "G major is key 7");
    setKey(k, 7, true);
    check(keyOf(k) == 19, "G minor is key 19");
    setKey(k, 7, false);
    q.clear();
    keys.detect(k, 6.0, 1.0f / 60.0f, q);
    check(q.empty(), "the first frame seeds the key");
    q.clear();
    keys.detect(k, 6.1, 1.0f / 60.0f, q);
    check(q.empty(), "the same key raises nothing");
    setKey(k, 7, true);
    k.keyClarity = 0.1f;  // no clarity rule: Harmony's key is the key
    q.clear();
    keys.detect(k, 6.2, 1.0f / 60.0f, q);
    check(q.size() == 1 && q[0].kind == EventKind::KeyChange && q[0].value == 19 && near(q[0].strength, 0.1f),
          "a change of the frame's key raises KeyChange at once, with its clarity");
    k.keyMajor = 0.9f;  // keyMajor is a lean, not the key: it does not change the key
    q.clear();
    keys.detect(k, 6.3, 1.0f / 60.0f, q);
    check(q.empty(), "keyMajor alone changes nothing");
    k.keyHue = 0.5f;  // names no key with this tonic
    check(keyOf(k) == -1, "an inconsistent root and hue name no key");
    q.clear();
    keys.detect(k, 6.4, 1.0f / 60.0f, q);
    check(q.empty(), "and raise nothing");

    // NaN in the frame raises nothing and breaks nothing.
    q.clear();
    GeodeSoundFrame bad = quietFrame();
    bad.kick = NAN;
    bad.sectionState = NAN;
    detect.detect(bad, 4.0, 1.0f / 60.0f, q);
    check(q.empty(), "a NaN trigger or section state raises nothing");
}

void testQueue() {
    EventQueue q;
    for (std::size_t i = 0; i < EventQueue::kCapacity + 5; ++i) q.push(Event{EventKind::Beat, 1.0f, 0, 0.0});
    check(q.size() == EventQueue::kCapacity && q.dropped() == 5, "a full queue drops and counts");
}

void testBus() {
    EventBus bus;
    std::vector<std::string> heard;
    bus.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { heard.push_back("a"); }, 0);
    bus.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { heard.push_back("b"); }, 5);
    const SubscriptionId c = bus.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { heard.push_back("c"); }, 0);
    bus.subscribe(EventFilter::of({EventKind::Snare}), [&](const Event&) { heard.push_back("snare"); }, 9);
    bus.subscribe(EventFilter::of({EventKind::Kick}, 0.5f), [&](const Event&) { heard.push_back("loud"); }, -1);
    EventQueue q;
    q.push(Event{EventKind::Kick, 0.3f, 0, 0.0});
    bus.dispatch(q);
    check((heard == std::vector<std::string>{"b", "a", "c"}), "priority first, then subscription order; filters hold");
    heard.clear();
    check(bus.unsubscribe(c) && !bus.unsubscribe(c), "unsubscribe once");

    // Re-entrancy: a handler that removes a later one and adds a new one.
    SubscriptionId later = kNoSubscription;
    bus.subscribe(EventFilter::of({EventKind::Beat}),
                  [&](const Event&) {
                      heard.push_back("first");
                      bus.unsubscribe(later);
                      bus.subscribe(EventFilter::of({EventKind::Beat}), [&](const Event&) { heard.push_back("new"); });
                  },
                  1);
    later = bus.subscribe(EventFilter::of({EventKind::Beat}), [&](const Event&) { heard.push_back("later"); }, 0);
    EventQueue beats;
    beats.push(Event{EventKind::Beat, 1.0f, 0, 0.0});
    beats.push(Event{EventKind::Beat, 1.0f, 0, 0.0});
    bus.dispatch(beats);
    check((heard == std::vector<std::string>{"first", "first"}), "a removed subscriber hears nothing more; a new one waits a frame");
    heard.clear();
    EventQueue one;
    one.push(Event{EventKind::Beat, 1.0f, 0, 0.0});
    bus.dispatch(one);
    check(heard.size() >= 2 && heard[0] == "first" && heard[1] == "new", "the new subscriber hears the next frame");
}

void testEventRoutes() {
    ParamStore store(schema());
    EventBus bus;
    EventRoutes routes(store);
    routes.attach(bus);
    routes.add({EventFilter::of({EventKind::Downbeat}), 4, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Drop}), 1, kMirror, {ActionKind::Toggle, 0.0f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Phrase}), 1, kHue, {ActionKind::Nudge, 0.3f, 0.0f}});
    check(routes.add({EventFilter::any(), 1, static_cast<ParamId>(99), {}}) == -1, "a route to an unknown param is refused");

    EventQueue q;
    for (int i = 0; i < 8; ++i) {
        q.clear();
        q.push(Event{EventKind::Downbeat, 1.0f, 0, i * 0.5});
        bus.dispatch(q);
    }
    check(near(store.committed(kFolds), 8.0f), "every 4th downbeat steps the folds: twice in eight");
    store.input(kFolds, ParamInput::set(12.0f));
    for (int i = 0; i < 4; ++i) {
        q.clear();
        q.push(Event{EventKind::Downbeat, 1.0f, 0, 10.0 + i});
        bus.dispatch(q);
    }
    check(near(store.committed(kFolds), 1.0f), "a step past the max wraps to the min");

    q.clear();
    q.push(Event{EventKind::Drop, 1.0f, 0, 20.0});
    bus.dispatch(q);
    check(near(store.committed(kMirror), 1.0f), "a drop toggles the mirror on");

    store.input(kHue, ParamInput::set(0.8f));
    q.clear();
    q.push(Event{EventKind::Phrase, 1.0f, 0, 21.0});
    bus.dispatch(q);
    check(near(store.committed(kHue), 0.1f), "a nudge goes round the hue's circle");

    // The finger wins: an event cannot move a held param.
    store.input(kHue, ParamInput::hold());
    q.clear();
    q.push(Event{EventKind::Phrase, 1.0f, 0, 22.0});
    bus.dispatch(q);
    check(near(store.committed(kHue), 0.1f), "an event never moves a param under a finger");
    store.input(kHue, ParamInput::release());

    // Flash safety: a kick on every frame, pulsing brightness, starts at most three pulses a second.
    EventRoutes flash(store);
    flash.attach(bus);
    flash.add({EventFilter::of({EventKind::Kick}), 1, kBrightness, {ActionKind::Pulse, 1.0f, 0.02f}});
    std::size_t started = 0;
    for (int frame = 0; frame < 60; ++frame) {
        const std::size_t before = flash.livePulses();
        q.clear();
        q.push(Event{EventKind::Kick, 1.0f, 0, frame / 60.0});
        bus.dispatch(q);
        if (flash.livePulses() > before) ++started;
        store.clearModulation();
        flash.modulate(1.0f / 60.0f);
    }
    check(started <= 3, "luminance pulses are limited to three a second (" + std::to_string(started) + ")");
    check(flash.pulsesLimited() >= 56, "and the rest are counted as limited");
    // The limit slows a luminance pulse to at least kLuminanceMinDecay: still well above zero 0.2 s on.
    EventRoutes slow(store);
    slow.add({EventFilter::of({EventKind::Snare}), 1, kBrightness, {ActionKind::Pulse, 1.0f, 0.01f}});
    slow.onEvent(Event{EventKind::Snare, 1.0f, 0, 100.0});
    for (int i = 0; i < 12; ++i) {
        store.clearModulation();
        slow.modulate(1.0f / 60.0f);
    }
    store.clearModulation();
    slow.modulate(1.0f / 60.0f);
    const float tail = store.effective(kBrightness) - store.base(kBrightness);
    check(tail > 0.4f, "a luminance pulse is slowed, never a one-frame flash (" + std::to_string(tail) + ")");
    // A pulse scales by the event's strength and fades away.
    EventRoutes zoom(store);
    zoom.add({EventFilter::of({EventKind::Onset}), 1, kZoom, {ActionKind::Pulse, 1.0f, 0.1f}});
    zoom.onEvent(Event{EventKind::Onset, 0.5f, 0, 200.0});
    store.clearModulation();
    zoom.modulate(1.0f / 60.0f);
    check(near(store.effective(kZoom) - store.base(kZoom), 0.5f), "a pulse starts at amount x strength");
    for (int i = 0; i < 60; ++i) {
        store.clearModulation();
        zoom.modulate(1.0f / 60.0f);
    }
    check(zoom.livePulses() == 0, "and retires once it has faded");
}

// Hold and Undrive check their origin like Drive and Try: only a finger holds, only a timeline lets go.
// The factories set the origin, so these inputs are built by hand (a replay, a wire format).
void testOriginChecks() {
    ParamInput hold = ParamInput::hold();
    ParamInput undrive = ParamInput::undrive();
    for (Origin o : {Origin::Preset, Origin::Timeline, Origin::Engine}) {
        hold.origin = o;
        for (ParamPhase from : {ParamPhase::Rest, ParamPhase::Glide, ParamPhase::Trial}) {
            ParamCell c = inPhase(from);
            const ParamCell before = c;
            check(apply(c, hold, kDef) == Outcome::Rejected, std::string("a non-user hold is rejected in ") + phaseName(from));
            check(c.phase == before.phase && c.base == before.base && c.committed == before.committed &&
                      c.trialHeld == before.trialHeld,
                  std::string("and leaves the cell as it was in ") + phaseName(from));
        }
    }
    for (Origin o : {Origin::User, Origin::Preset, Origin::Engine}) {
        undrive.origin = o;
        ParamCell c = inPhase(ParamPhase::Driven);
        check(apply(c, undrive, kDef) == Outcome::Rejected && c.phase == ParamPhase::Driven,
              "only the timeline undrives a driven param");
        // Elsewhere an undrive means nothing, whoever sends it.
        ParamCell r = inPhase(ParamPhase::Rest);
        check(apply(r, undrive, kDef) == Outcome::Ignored && r.phase == ParamPhase::Rest, "an undrive at rest is ignored");
    }
    // The real origins still pass.
    ParamCell c = inPhase(ParamPhase::Rest);
    check(apply(c, ParamInput::hold(), kDef) == Outcome::Applied && c.phase == ParamPhase::Held, "the user's hold holds");
    c = inPhase(ParamPhase::Driven);
    apply(c, ParamInput::set(2.5f, Origin::Timeline), kDef);  // off the user's value, so there is a glide back
    check(apply(c, ParamInput::undrive(), kDef) == Outcome::Applied && c.phase == ParamPhase::Glide,
          "the timeline's undrive glides back");
}

// A glide of negative or non-finite seconds is rejected in every phase; 0 lands at once, like a Set.
void testBadGlides() {
    for (float s : {INFINITY, -INFINITY, NAN, -1.0f, -1e-6f}) {
        for (ParamPhase from : {ParamPhase::Rest, ParamPhase::Glide, ParamPhase::Trial, ParamPhase::Driven}) {
            const Origin o = from == ParamPhase::Driven ? Origin::Timeline : Origin::Preset;
            ParamCell c = inPhase(from);
            const ParamCell before = c;
            check(apply(c, ParamInput::glideTo(2.0f, s, o), kDef) == Outcome::Rejected,
                  std::string("a glide of ") + std::to_string(s) + " s is rejected in " + phaseName(from));
            check(c.phase == before.phase && c.base == before.base && c.committed == before.committed,
                  std::string("and leaves the cell as it was in ") + phaseName(from));
            ParamCell r = inPhase(from);
            check(apply(r, ParamInput::reset(s, Origin::Preset), kDef) == Outcome::Rejected,
                  std::string("a reset of ") + std::to_string(s) + " s is rejected in " + phaseName(from));
        }
    }
    // The infinite glide in particular never starts, so it can never hang short of its target.
    ParamCell c = inPhase(ParamPhase::Rest);
    apply(c, ParamInput::glideTo(2.0f, INFINITY), kDef);
    for (int i = 0; i < 600; ++i) advance(c, 1.0f / 60.0f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 1.0f), "an infinite glide leaves the param where it was");
    // Zero is valid: it lands at once, exactly like a Set.
    ParamCell g = inPhase(ParamPhase::Glide);
    ParamCell s = inPhase(ParamPhase::Glide);
    check(apply(g, ParamInput::glideTo(2.0f, 0.0f), kDef) == Outcome::Applied, "a glide of 0 s is applied");
    apply(s, ParamInput::set(2.0f, Origin::Preset), kDef);
    check(g.phase == ParamPhase::Rest && s.phase == ParamPhase::Rest && g.base == s.base && g.committed == s.committed,
          "and lands at once, like a Set");
}

// Engine time is not monotonic. Time before a param's last pulse is a new clock: the param's old pulses
// and flash history go, the first pulse on the new clock starts, and the limit holds within it.
void testTimeGoingBack() {
    ParamStore store(schema());
    EventRoutes routes(store);
    routes.add({EventFilter::of({EventKind::Kick}), 1, kBrightness, {ActionKind::Pulse, 0.4f, 0.5f}});
    routes.add({EventFilter::of({EventKind::Snare}), 1, kZoom, {ActionKind::Pulse, 0.5f, 0.5f}});
    routes.onEvent(Event{EventKind::Kick, 1.0f, 0, 100.0});
    routes.onEvent(Event{EventKind::Snare, 1.0f, 0, 100.0});
    check(routes.livePulses() == 2, "two pulses on the old clock");
    // A new render starts at 0: the kick is not dropped as if 100 s had to pass first.
    routes.onEvent(Event{EventKind::Kick, 1.0f, 0, 0.0});
    check(routes.pulsesLimited() == 0, "a pulse at time 0 after time 100 is a new clock, not a limited flash");
    check(routes.livePulses() == 2, "the brightness pulse of the old clock is replaced, the zoom pulse kept");
    store.clearModulation();
    routes.modulate(0.0f);
    // 0.4 alone; the old pulse summed in would give 0.8 (both under the range's top, so the clamp hides neither).
    check(near(store.effective(kBrightness) - store.base(kBrightness), 0.4f),
          "only the new clock's pulse plays on brightness, not the old one summed in");
    // The flash limit holds within the new clock: kicks every frame for one second start at most three.
    std::size_t started = 1;  // the one at 0
    for (int frame = 1; frame < 60; ++frame) {
        const std::uint32_t before = routes.pulsesLimited();
        routes.onEvent(Event{EventKind::Kick, 1.0f, 0, frame / 60.0});
        if (routes.pulsesLimited() == before) ++started;
    }
    check(started <= 3, "the flash limit holds on the new clock (" + std::to_string(started) + ")");
    check(started >= 3, "and re-arms each third of a second (" + std::to_string(started) + ")");
    // A seek back by a little is a new clock too, so a pulse there starts at once.
    const std::uint32_t before = routes.pulsesLimited();
    routes.onEvent(Event{EventKind::Kick, 1.0f, 0, 0.5});
    check(routes.pulsesLimited() == before, "a seek back re-arms the param's pulse");
    // An event with no real time never reaches a luminance param.
    routes.onEvent(Event{EventKind::Kick, 1.0f, 0, NAN});
    check(routes.pulsesLimited() == before + 1, "a luminance pulse at a non-finite time is dropped");
    routes.onEvent(Event{EventKind::Kick, 1.0f, 0, 0.6});
    check(routes.pulsesLimited() == before + 2, "and leaves the flash clock as it was");
}

// Two identical pipelines, fed the same frames, give bit-identical params.
std::vector<float> runPipeline() {
    ParamStore store(schema());
    EventBus bus;
    SoundEvents detect;
    EventRoutes routes(store);
    routes.attach(bus);
    routes.add({EventFilter::of({EventKind::Kick}), 1, kZoom, {ActionKind::Pulse, 0.4f, 0.15f}});
    routes.add({EventFilter::of({EventKind::Beat}), 2, kHue, {ActionKind::Nudge, 0.07f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Downbeat}), 1, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Snare}), 1, kBrightness, {ActionKind::Pulse, 0.5f, 0.2f}});
    store.input(kZoom, ParamInput::glideTo(2.0f, 3.0f));
    std::vector<float> out, all;
    EventQueue q;
    for (int frame = 0; frame < 600; ++frame) {
        GeodeSoundFrame f = quietFrame();
        f.kick = frame % 15 == 0 ? 0.9f : 0.0f;
        f.snare = frame % 30 == 7 ? 0.7f : 0.0f;
        f.beat = frame % 30 == 0 ? 1.0f : 0.0f;
        f.downbeat = frame % 120 == 0 ? 1.0f : 0.0f;
        const float dt = 1.0f / 60.0f;
        store.advance(dt);
        store.clearModulation();
        q.clear();
        detect.detect(f, frame / 60.0, dt, q);
        bus.dispatch(q);
        routes.modulate(dt);
        store.effectiveAll(all);
        out.insert(out.end(), all.begin(), all.end());
    }
    return out;
}

void testDeterminism() {
    const std::vector<float> a = runPipeline();
    const std::vector<float> b = runPipeline();
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    check(same, "two identical pipelines give bit-identical params over 600 frames");
    bool finite = true;
    for (float v : a) finite = finite && std::isfinite(v);
    check(finite, "every value stays finite");
}


// ---- the mutation pass (#413): each case names the mutant of the PR's code it kills ----------------------

// A seeded generator: raw mt19937 words only (distributions differ between standard libraries).
float unitOf(std::mt19937& g) { return static_cast<float>(g() % 1000001u) / 1000000.0f; }
float between(std::mt19937& g, float lo, float hi) { return lo + (hi - lo) * unitOf(g); }

void testSchemaEdges() {
    // Kills: dropping the constructor's non-finite default fix (the default stays NaN and so does every clamp).
    const ParamSchema nanDefault({{"x", ParamKind::Float, NAN, 0.2f, 0.8f, false}});
    check(near(nanDefault.def(0).defaultValue, 0.2f), "a NaN default becomes the range's min");
    check(near(nanDefault.clamp(0, NAN), 0.2f), "so a NaN value still clamps to a number");

    // Kills: idsUnique() returning true always, or skipping its empty-id or duplicate test.
    const std::vector<ParamDef> dup = {{"a", ParamKind::Float, 0.0f, 0.0f, 1.0f, false}, {"a", ParamKind::Float, 0.0f, 0.0f, 1.0f, false}};
    const std::vector<ParamDef> empty = {{"", ParamKind::Float, 0.0f, 0.0f, 1.0f, false}};
    check(!ParamSchema(dup).idsUnique(), "a repeated id is not unique");
    check(!ParamSchema(empty).idsUnique(), "an empty id is not a usable id");

    // Kills: wrap returning t instead of min + t, and dropping the negative fix-up (t += span).
    const ParamSchema angle({{"angle", ParamKind::Circular, 0.0f, -180.0f, 180.0f, false}});
    check(near(angle.clamp(0, 190.0f), -170.0f) && near(angle.clamp(0, -190.0f), 170.0f) && near(angle.clamp(0, 180.0f), -180.0f),
          "an angle on [-180, 180) wraps round its own circle");
    // Kills: dropping the `t >= span` guard (a tiny negative hue would come back as exactly max).
    const ParamSchema s = schema();
    const float tiny = s.clamp(kHue, -1e-9f);
    check(tiny >= 0.0f && tiny < 1.0f, "a hue a hair below 0 lands inside the half-open circle");

    // Kills: Int clamping to the raw ends (no ceil/floor), and half-up rounding (floor(x + 0.5)) for negatives.
    const ParamSchema steps({{"n", ParamKind::Int, 2.0f, 0.5f, 3.7f, false}});
    check(near(steps.clamp(0, -5.0f), 1.0f) && near(steps.clamp(0, 10.0f), 3.0f), "an int's range ends are whole numbers inside it");
    const ParamSchema signedInt({{"n", ParamKind::Int, 0.0f, -5.0f, 5.0f, false}});
    check(near(signedInt.clamp(0, 2.5f), 3.0f) && near(signedInt.clamp(0, -2.5f), -3.0f), "an int's halves round away from zero");

    // Kills: circularDelta's sign fix-ups dropped or swapped, and a non-circular delta that is not to - from.
    const ParamDef hue{"hue", ParamKind::Circular, 0.0f, 0.0f, 1.0f, false};
    check(near(ParamSchema::circularDelta(hue, 0.9f, 0.1f), 0.2f) && near(ParamSchema::circularDelta(hue, 0.1f, 0.9f), -0.2f),
          "the short way round a hue, both directions");
    check(near(ParamSchema::circularDelta(kDef, 1.0f, 3.0f), 2.0f), "a float's delta is plain subtraction");
}

void testStateEdges() {
    // Kills: `!(seconds > 0)` → `seconds <= 0` (a NaN duration would glide forever toward NaN).
    ParamCell c = restingCell(kDef);
    apply(c, ParamInput::glideTo(2.0f, NAN), kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 2.0f), "a glide with a NaN duration lands at once");

    // Kills: the Smooth ease ignored (linear would be at 1.5): smoothstep(0.25) = 3/16 - 2/64 = 0.15625.
    c = restingCell(kDef);
    apply(c, ParamInput::glideTo(3.0f, 1.0f, Origin::Preset, Ease::Smooth), kDef);
    advance(c, 0.25f, kDef);
    check(near(c.base, 1.3125f), "a smooth glide is eased in: 1 + 2 x 0.15625 a quarter of the way");

    // Kills: a retarget starting from the old target (or the new one) instead of where the base is now.
    c = restingCell(kDef);
    apply(c, ParamInput::glideTo(3.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    advance(c, 0.5f, kDef);
    apply(c, ParamInput::glideTo(1.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    check(near(c.base, 2.0f), "a new glide does not jump");
    advance(c, 0.5f, kDef);
    check(near(c.base, 1.5f), "it eases on from where the old one was");

    // Kills: `elapsed >= duration` → `>` (a glide at exactly its time would hang in Glide).
    c = restingCell(kDef);
    apply(c, ParamInput::glideTo(3.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    advance(c, 0.5f, kDef);
    advance(c, 0.5f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 3.0f), "a glide rests at exactly its duration");

    // Kills: advance without its dt guard (a negative dt runs a glide backwards, a NaN one poisons the base).
    c = restingCell(kDef);
    apply(c, ParamInput::glideTo(3.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    advance(c, 0.5f, kDef);
    advance(c, -0.25f, kDef);
    advance(c, NAN, kDef);
    check(c.phase == ParamPhase::Glide && near(c.base, 2.0f), "a negative or NaN dt leaves a glide where it is");

    // Kills: a circular glide without the wrap, or the long way round: 0.9 → 0.1 passes 0.95 then 0.05.
    const ParamDef hue{"hue", ParamKind::Circular, 0.9f, 0.0f, 1.0f, false};
    ParamCell h = restingCell(hue);
    apply(h, ParamInput::glideTo(0.1f, 1.0f, Origin::Preset, Ease::Linear), hue);
    advance(h, 0.25f, hue);
    const float quarter = h.base;
    advance(h, 0.5f, hue);
    check(near(quarter, 0.95f) && near(h.base, 0.05f), "a hue glide crosses the wrap and stays on the circle");

    // Kills: `trialLeft <= 0` → `< 0`, and a Try with no time of its own not falling back on kTrialSeconds.
    c = restingCell(kDef);
    apply(c, ParamInput::set(1.5f), kDef);
    apply(c, ParamInput::tryValue(2.8f), kDef);
    advance(c, kTrialSeconds * 0.5f, kDef);
    check(c.phase == ParamPhase::Trial, "an untouched trial shows for its time");
    advance(c, kTrialSeconds * 0.5f, kDef);
    check(c.phase == ParamPhase::Glide, "and springs back exactly when it is up");

    // Kills: trialSeconds accepting a negative or infinite time (a trial that never ends, or ends at once).
    for (float bad : {-1.0f, NAN, INFINITY}) {
        c = restingCell(kDef);
        apply(c, ParamInput::tryValue(2.8f, bad), kDef);
        advance(c, kTrialSeconds * 0.75f, kDef);
        const bool shown = c.phase == ParamPhase::Trial;
        advance(c, kTrialSeconds * 0.5f, kDef);
        check(shown && c.phase == ParamPhase::Glide, "a bad trial time falls back on the default (" + std::to_string(bad) + ")");
    }

    // Kills: the origin checks on Try and Drive removed (only a hand tries, only a timeline drives).
    c = restingCell(kDef);
    check(apply(c, ParamInput{ParamInput::Kind::Try, Origin::Engine, 2.5f, 0.0f, Ease::Smooth}, kDef) == Outcome::Rejected &&
              c.phase == ParamPhase::Rest && near(c.base, 1.0f),
          "the engine cannot start a trial");
    check(apply(c, ParamInput{ParamInput::Kind::Drive, Origin::User, 0.0f, 0.0f, Ease::Smooth}, kDef) == Outcome::Rejected &&
              c.phase == ParamPhase::Rest,
          "a hand cannot drive a param like a timeline");
    apply(c, ParamInput::hold(), kDef);
    check(apply(c, ParamInput{ParamInput::Kind::Try, Origin::Preset, 2.5f, 0.0f, Ease::Smooth}, kDef) == Outcome::Rejected &&
              c.phase == ParamPhase::Held,
          "a preset cannot start a trial under a finger");
    c = restingCell(kDef);
    apply(c, ParamInput::tryValue(2.5f), kDef);
    check(apply(c, ParamInput{ParamInput::Kind::Drive, Origin::Engine, 0.0f, 0.0f, Ease::Smooth}, kDef) == Outcome::Rejected &&
              c.phase == ParamPhase::Trial,
          "the engine cannot end a trial by driving it");

    // Kills: a Try mid-glide replacing the spring-back target with the base (it must spring back to the glide's target).
    c = restingCell(kDef);
    apply(c, ParamInput::glideTo(2.0f, 1.0f, Origin::Preset, Ease::Linear), kDef);
    advance(c, 0.5f, kDef);
    apply(c, ParamInput::tryValue(2.8f), kDef);
    apply(c, ParamInput::release(), kDef);
    advance(c, kSpringBackSeconds + 0.01f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 2.0f), "a trial during a preset's glide springs back to the preset's value");

    // Kills: Held → Try leaving trialHeld false (the dragged trial would time out under the finger).
    c = restingCell(kDef);
    apply(c, ParamInput::hold(), kDef);
    apply(c, ParamInput::set(1.7f), kDef);
    apply(c, ParamInput::tryValue(2.8f), kDef);
    advance(c, 10.0f, kDef);
    check(c.phase == ParamPhase::Trial && near(c.base, 2.8f), "a trial begun under a finger never times out");
    apply(c, ParamInput::release(), kDef);
    advance(c, kSpringBackSeconds + 0.01f, kDef);
    check(c.phase == ParamPhase::Rest && near(c.base, 1.7f), "and springs back to what the finger held");

    // Kills: a repeated Try not restarting the trial's timer.
    c = restingCell(kDef);
    apply(c, ParamInput::tryValue(2.8f, 1.0f), kDef);
    advance(c, 0.8f, kDef);
    apply(c, ParamInput::tryValue(2.6f, 1.0f), kDef);
    advance(c, 0.8f, kDef);
    check(c.phase == ParamPhase::Trial && near(c.base, 2.6f), "trying again restarts the trial's time");

    // Kills: Trial's first branch keyed on origin alone (a hand's GlideTo or Reset would move the tried value).
    c = restingCell(kDef);
    apply(c, ParamInput::set(1.5f), kDef);
    apply(c, ParamInput::tryValue(2.8f), kDef);
    check(apply(c, ParamInput::glideTo(2.0f, 1.0f, Origin::User), kDef) == Outcome::Rejected && near(c.base, 2.8f) && near(c.committed, 1.5f),
          "a hand's glide during a trial is rejected");
    check(apply(c, ParamInput::reset(0.5f), kDef) == Outcome::Applied && near(c.base, 2.8f) && near(c.committed, 1.0f),
          "a hand's reset during a trial moves what it springs back to, not what is shown");

    // Kills: Trial → Drive → Undrive springing back to the tried value instead of the kept one.
    c = restingCell(kDef);
    apply(c, ParamInput::set(1.5f), kDef);
    apply(c, ParamInput::tryValue(2.8f), kDef);
    apply(c, ParamInput::drive(), kDef);
    advance(c, 10.0f, kDef);
    const bool stillDriven = c.phase == ParamPhase::Driven;
    apply(c, ParamInput::undrive(), kDef);
    advance(c, kSpringBackSeconds + 0.01f, kDef);
    check(stillDriven && c.phase == ParamPhase::Rest && near(c.base, 1.5f), "a timeline that takes a trial gives back the kept value");

    // Kills: Held, Trial or Driven storing the raw input instead of the clamped value.
    c = restingCell(kDef);
    apply(c, ParamInput::hold(), kDef);
    apply(c, ParamInput::set(9.0f), kDef);
    check(near(c.base, 3.0f) && near(c.committed, 3.0f), "a finger's value is clamped");
    c = restingCell(kDef);
    apply(c, ParamInput::tryValue(9.0f), kDef);
    const float tried = c.base;
    apply(c, ParamInput::set(-9.0f), kDef);
    check(near(tried, 3.0f) && near(c.base, 0.3f), "a tried value is clamped");
    c = restingCell(kDef);
    apply(c, ParamInput::drive(), kDef);
    apply(c, ParamInput::set(9.0f, Origin::Timeline), kDef);
    const float driven = c.base;
    // Kills: a timeline's GlideTo starting a glide; and the Driven origin check reduced to "not User".
    apply(c, ParamInput::glideTo(0.5f, 1.0f, Origin::Timeline), kDef);
    check(near(driven, 3.0f) && near(c.base, 0.5f) && c.phase == ParamPhase::Driven, "a timeline's values are clamped and land at once");
    check(apply(c, ParamInput::set(2.0f, Origin::Engine), kDef) == Outcome::Rejected && near(c.base, 0.5f), "the engine cannot move a driven param");

    // Kills: Int (and Bool) springing back by interpolation.
    const ParamDef folds{"symmetry", ParamKind::Int, 6.0f, 1.0f, 12.0f, false};
    ParamCell f = restingCell(folds);
    apply(f, ParamInput::tryValue(11.0f), folds);
    apply(f, ParamInput::release(), folds);
    check(f.phase == ParamPhase::Rest && near(f.base, 6.0f), "an int trial snaps back at once");
    const ParamDef mirror{"mirror", ParamKind::Bool, 0.0f, 0.0f, 1.0f, false};
    ParamCell m = restingCell(mirror);
    apply(m, ParamInput::glideTo(1.0f, 1.0f), mirror);
    check(m.phase == ParamPhase::Rest && near(m.base, 1.0f), "a bool never glides");
}

// Kills: any path that stores an unclamped or non-finite value, and any path that lets a weaker origin move a
// held or driven base (seeded random inputs, dt and values, NaN and infinities among them).
void testStateFuzz() {
    const ParamDef hue{"hue", ParamKind::Circular, 0.0f, 0.0f, 1.0f, false};
    const ParamDef* defs[] = {&kDef, &hue};
    const float specials[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f};
    for (const ParamDef* def : defs) {
        std::mt19937 g(413u);
        ParamCell c = restingCell(*def);
        bool inRange = true, finite = true, fingerWins = true, timelineWins = true;
        for (int step = 0; step < 20000; ++step) {
            const std::uint32_t r = g();
            const float v = r % 7u == 0u ? specials[(r >> 8) % 5u] : between(g, -5.0f, 5.0f);
            const float s = r % 11u == 0u ? specials[(r >> 12) % 5u] : between(g, 0.0f, 1.0f);
            const auto origin = static_cast<Origin>((r >> 4) % 4u);
            ParamInput in;
            switch ((r >> 16) % 9u) {
                case 0: in = ParamInput::set(v, origin); break;
                case 1: in = ParamInput::glideTo(v, s, origin, (r >> 20) % 2u ? Ease::Linear : Ease::Smooth); break;
                case 2: in = ParamInput::hold(); break;
                case 3: in = ParamInput::release(); break;
                case 4: in = ParamInput::tryValue(v, s); break;
                case 5: in = ParamInput::drive(); break;
                case 6: in = ParamInput::undrive(); break;
                case 7: in = ParamInput::reset(s, origin); break;
                default: advance(c, r % 13u == 0u ? specials[(r >> 24) % 5u] : between(g, -0.05f, 0.2f), *def); break;
            }
            if ((r >> 16) % 9u != 8u) {
                const ParamPhase before = c.phase;
                const float baseBefore = c.base;
                apply(c, in, *def);
                const bool setLike = in.kind == ParamInput::Kind::Set || in.kind == ParamInput::Kind::GlideTo ||
                                     in.kind == ParamInput::Kind::Reset || in.kind == ParamInput::Kind::Drive;
                if (before == ParamPhase::Held && setLike && in.origin != Origin::User)
                    fingerWins = fingerWins && c.phase == ParamPhase::Held && c.base == baseBefore;
                const bool userLike = in.kind == ParamInput::Kind::Hold || in.kind == ParamInput::Kind::Try ||
                                      ((in.kind == ParamInput::Kind::Set || in.kind == ParamInput::Kind::GlideTo ||
                                        in.kind == ParamInput::Kind::Reset) && in.origin != Origin::Timeline);
                if (before == ParamPhase::Driven && userLike)
                    timelineWins = timelineWins && c.phase == ParamPhase::Driven && c.base == baseBefore;
            }
            finite = finite && std::isfinite(c.base) && std::isfinite(c.committed);
            const float lo = def->min - 1e-5f, hi = def->max + 1e-5f;
            inRange = inRange && c.base >= lo && c.base <= hi && c.committed >= lo && c.committed <= hi;
            if (def->kind == ParamKind::Circular) inRange = inRange && c.base < def->max && c.committed < def->max;
        }
        const std::string name = std::string(def->id);
        check(finite, name + ": the base and the committed value stay finite whatever comes in");
        check(inRange, name + ": the base and the committed value stay inside the range");
        check(fingerWins, name + ": nothing but the finger moves a held param");
        check(timelineWins, name + ": nothing but the timeline moves a driven param");
    }
}

void testStoreEdges() {
    // Kills: Add ignoring its weight.
    ParamStore store(schema());
    store.modulate(kZoom, Combine::Add, 1.0f, 0.5f);
    check(near(store.effective(kZoom), 1.5f), "an add counts by its weight");
    // Kills: Multiply's weight scaling the factor (amount x weight) instead of blending it toward 1.
    store.clearModulation();
    store.modulate(kZoom, Combine::Multiply, 3.0f, 0.5f);
    check(near(store.effective(kZoom), 2.0f), "a half-weight x3 is x2");
    // Kills: Toward's weighted mean, or its pull cap at 1, removed: (3 x 0.5 + 2 x 1) / 1.5 = 7/3, pulled whole.
    store.clearModulation();
    store.modulate(kZoom, Combine::Toward, 3.0f, 0.5f);
    store.modulate(kZoom, Combine::Toward, 2.0f, 1.0f);
    check(near(store.effective(kZoom), 7.0f / 3.0f), "two targets pull to their weighted mean, at most all the way");
    // Kills: Toward accepting a negative weight.
    store.clearModulation();
    store.modulate(kZoom, Combine::Toward, 3.0f, 0.5f);
    store.modulate(kZoom, Combine::Toward, 0.3f, -0.4f);
    check(near(store.effective(kZoom), 2.0f), "a negative toward weight is dropped");
    // Kills: circular Toward averaging raw targets (0.9 and 0.1 average to 0.5, the far side of the circle).
    store.clearModulation();
    store.modulate(kHue, Combine::Toward, 0.9f, 0.5f);
    store.modulate(kHue, Combine::Toward, 0.1f, 0.5f);
    const float h = store.effective(kHue);
    check(near(h, 0.0f, 1e-3f) || near(h, 1.0f, 1e-3f), "two hues either side of the wrap average across it");
    // Kills: the mute ignoring a trial under a finger.
    store.clearModulation();
    store.input(kZoom, ParamInput::tryValue(2.0f));
    store.modulate(kZoom, Combine::Add, 0.5f);
    const float loose = store.effective(kZoom);
    store.input(kZoom, ParamInput::hold());
    check(near(loose, 2.5f) && near(store.effective(kZoom), 2.0f), "a trial under a finger shows exactly the tried value");
    // Kills: setMuteWhileHeld(false) having no effect.
    store.setMuteWhileHeld(false);
    check(near(store.effective(kZoom), 2.5f), "with the mute off a held param is modulated");
    // Unknown ids read as nothing and modulate nothing.
    store.modulate(static_cast<ParamId>(99), Combine::Add, 1.0f);
    check(store.effective(static_cast<ParamId>(99)) == 0.0f && store.phase(static_cast<ParamId>(99)) == ParamPhase::Rest,
          "an unknown param reads as nothing");
}

// Kills: any Combine that overwrites instead of accumulating (the order of modulators would then matter).
void testModulationOrderProperty() {
    std::mt19937 g(0x413u);
    const ParamSchema s = schema();
    const ParamDef& hueDef = s.def(kHue);
    bool sameFloat = true, sameHue = true;
    for (int trial = 0; trial < 300; ++trial) {
        struct Mod {
            Combine c;
            float amount, weight;
        };
        std::vector<Mod> zoomMods, hueMods;
        for (int i = 0; i < 6; ++i) {
            const auto c = static_cast<Combine>(g() % 3u);
            const float amount = c == Combine::Multiply ? between(g, 0.5f, 1.5f) : c == Combine::Add ? between(g, -1.0f, 1.0f) : between(g, 0.3f, 3.0f);
            zoomMods.push_back({c, amount, between(g, 0.0f, 1.0f)});
            const auto hc = g() % 2u ? Combine::Add : Combine::Toward;
            hueMods.push_back({hc, hc == Combine::Add ? between(g, -0.4f, 0.4f) : unitOf(g) * 0.999f, between(g, 0.05f, 1.0f)});
        }
        const float zoomBase = between(g, 0.3f, 3.0f), hueBase = unitOf(g) * 0.999f;
        float zoom[2], hue[2];
        for (int pass = 0; pass < 2; ++pass) {
            ParamStore store(schema());
            store.input(kZoom, ParamInput::set(zoomBase));
            store.input(kHue, ParamInput::set(hueBase));
            for (int i = 0; i < 6; ++i) {
                const Mod& z = zoomMods[pass == 0 ? i : 5 - i];
                const Mod& hm = hueMods[pass == 0 ? i : 5 - i];
                store.modulate(kZoom, z.c, z.amount, z.weight);
                store.modulate(kHue, hm.c, hm.amount, hm.weight);
            }
            zoom[pass] = store.effective(kZoom);
            hue[pass] = store.effective(kHue);
        }
        sameFloat = sameFloat && near(zoom[0], zoom[1], 1e-4f);
        sameHue = sameHue && std::fabs(ParamSchema::circularDelta(hueDef, hue[0], hue[1])) <= 1e-4f;
    }
    check(sameFloat, "a float's modulators give the same value in any order");
    check(sameHue, "a hue's modulators give the same value in any order");
}

std::size_t countOf(const EventQueue& q, EventKind kind) {
    std::size_t n = 0;
    for (const Event& e : q) n += e.kind == kind ? 1 : 0;
    return n;
}

void testSoundEventEdges() {
    EventQueue q;
    // Kills: strengthOf without its clamp.
    {
        SoundEvents d;
        GeodeSoundFrame f = quietFrame();
        f.kick = 1.7f;
        d.detect(f, 0.0, 1.0f / 60.0f, q);
        check(q.size() == 1 && near(q[0].strength, 1.0f), "a trigger above 1 is an event of strength 1");
    }
    // Kills: an unseeded counter (kUnknown) raising nothing when its trigger fires.
    {
        SoundEvents d;
        GeodeSoundFrame f = quietFrame();
        f.beat = 1.0f;
        f.beatCount = 10.0f;
        q.clear();
        d.detect(f, 0.0, 1.0f / 60.0f, q);
        check(countOf(q, EventKind::Beat) == 1, "a beat on the very first frame is one Beat");
        // Kills: `max(steps, 1)` → `max(steps, 0)` (a trigger on a frame that left the count alone is lost).
        q.clear();
        d.detect(f, 0.1, 1.0f / 60.0f, q);
        check(countOf(q, EventKind::Beat) == 1, "a beat trigger with no count step is still one Beat");
        // Kills: a non-finite counter clearing the base (the next frame's two steps would read as one).
        f.beatCount = NAN;
        q.clear();
        d.detect(f, 0.2, 1.0f / 60.0f, q);
        const std::size_t onNan = countOf(q, EventKind::Beat);
        f.beatCount = 12.0f;
        q.clear();
        d.detect(f, 0.3, 1.0f / 60.0f, q);
        check(onNan == 1 && countOf(q, EventKind::Beat) == 2, "a NaN count keeps the last base: 10 → NaN → 12 is one beat, then two");
    }
    // Kills: the wrap test's `>` → `>=`, a wrap modulus off by one, and any lost or doubled beat across 4095 → 0:
    // every stall of k ≤ kMaxBurst beats, from every start near the wrap, raises exactly k Beats.
    bool exact = true;
    std::string firstBad;
    for (int start : {0, 100, 4087, 4088, 4089, 4090, 4091, 4092, 4093, 4094, 4095}) {
        for (int k = 1; k <= SoundEvents::kMaxBurst; ++k) {
            SoundEvents d;
            GeodeSoundFrame f = quietFrame();
            f.beatCount = static_cast<float>(start);
            q.clear();
            d.detect(f, 0.0, 1.0f / 60.0f, q);
            f.beat = 1.0f;
            f.beatCount = static_cast<float>((start + k) % 4096);
            q.clear();
            d.detect(f, 0.1, 1.0f / 60.0f, q);
            if (countOf(q, EventKind::Beat) != static_cast<std::size_t>(k) && exact) {
                exact = false;
                firstBad = std::to_string(start) + "+" + std::to_string(k) + " gave " + std::to_string(countOf(q, EventKind::Beat));
            }
        }
    }
    check(exact, "k beats between frames are k Beat events, across the 4096 wrap too " + firstBad);

    // Kills: reset() leaving a counter, the section state or the key behind.
    {
        SoundEvents d;
        GeodeSoundFrame f = quietFrame();
        f.beatCount = 4000.0f;
        f.keyRoot = 7.0f / 12.0f;  // G major: tonic G, fifths position 1
        f.keyHue = 1.0f / 12.0f;
        d.detect(f, 0.0, 1.0f / 60.0f, q);
        d.reset();
        f.beat = 1.0f;
        f.beatCount = 5.0f;
        f.sectionState = GEODE_SECTION_BUILD;
        f.keyRoot = 2.0f / 12.0f;  // D major: tonic D, fifths position 2
        f.keyHue = 2.0f / 12.0f;
        q.clear();
        d.detect(f, 1.0, 1.0f / 60.0f, q);
        check(q.size() == 1 && q[0].kind == EventKind::Beat, "after a reset the first frame only seeds: one Beat, no resync, no build, no key change");
    }
    // Kills: BuildEnd keyed on the new state; it fires on leaving a build for anything, carrying where it went.
    {
        SoundEvents d;
        GeodeSoundFrame f = quietFrame();
        f.sectionState = GEODE_SECTION_BUILD;
        d.detect(f, 0.0, 1.0f / 60.0f, q);
        f.sectionState = GEODE_SECTION_BREAKDOWN;
        q.clear();
        d.detect(f, 0.1, 1.0f / 60.0f, q);
        const bool ended = q.size() == 1 && q[0].kind == EventKind::BuildEnd && q[0].value == GEODE_SECTION_BREAKDOWN;
        q.clear();
        d.detect(f, 0.2, 1.0f / 60.0f, q);
        check(ended && q.empty(), "a build that breaks down ends once");
    }
    // Kills: a frame with no key clearing the last key (the next key would only seed, not change).
    {
        SoundEvents d;
        GeodeSoundFrame f = quietFrame();
        f.keyClarity = 0.7f;
        f.keyRoot = 7.0f / 12.0f;
        f.keyHue = 1.0f / 12.0f;  // G major
        d.detect(f, 0.0, 1.0f / 60.0f, q);
        f.keyHue = 0.5f;  // no key
        d.detect(f, 0.1, 1.0f / 60.0f, q);
        f.keyHue = 1.0f / 12.0f;  // G major again
        q.clear();
        d.detect(f, 0.2, 1.0f / 60.0f, q);
        const bool sameAgain = q.empty();
        f.keyRoot = 2.0f / 12.0f;  // D major
        f.keyHue = 2.0f / 12.0f;
        d.detect(f, 0.3, 1.0f / 60.0f, q);
        f.keyHue = 0.5f;
        d.detect(f, 0.4, 1.0f / 60.0f, q);
        f.keyRoot = 9.0f / 12.0f;  // A minor: relative major C, fifths position 0
        f.keyHue = 0.0f;
        q.clear();
        d.detect(f, 0.5, 1.0f / 60.0f, q);
        check(sameAgain && q.size() == 1 && q[0].kind == EventKind::KeyChange && q[0].value == 21,
              "a gap with no key neither repeats the old key nor hides the next change");
    }
    // Kills: any slip in keyOf's major/minor decode. Literal circle of fifths: C G D A E B F# C# G# D# A# F.
    const int fifths[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};  // the pitch class at each fifths position
    bool allKeys = true;
    for (int pos = 0; pos < 12; ++pos) {
        const int major = fifths[pos];
        // The minor key on this position is its relative major's sixth degree: three semitones below.
        const int minor = (major + 9) % 12;
        GeodeSoundFrame f{};
        f.keyHue = static_cast<float>(pos) / 12.0f;
        f.keyRoot = static_cast<float>(major) / 12.0f;
        allKeys = allKeys && keyOf(f) == major;
        f.keyRoot = static_cast<float>(minor) / 12.0f;
        allKeys = allKeys && keyOf(f) == 12 + minor;
    }
    check(allKeys, "all 24 keys read back from root and hue");
}

void testBusEdges() {
    // Kills: minStrength compared with `>` (a filter's floor is inclusive).
    EventBus bus;
    int loud = 0;
    bus.subscribe(EventFilter::of({EventKind::Kick}, 0.5f), [&](const Event&) { ++loud; });
    EventQueue q;
    q.push(Event{EventKind::Kick, 0.5f, 0, 0.0});
    bus.dispatch(q);
    check(loud == 1, "an event exactly at a filter's minimum strength is heard");

    // Kills: dispatch without its re-entrancy guard (a nested dispatch would deliver mid-frame).
    EventBus nest;
    int snares = 0;
    EventQueue inner;
    inner.push(Event{EventKind::Snare, 1.0f, 0, 0.0});
    nest.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { nest.dispatch(inner); });
    nest.subscribe(EventFilter::of({EventKind::Snare}), [&](const Event&) { ++snares; });
    nest.dispatch(q);
    check(snares == 0, "a handler cannot dispatch inside a dispatch");

    // Kills: unsubscribe not looking in the joining list (a subscriber added and removed in one frame would join).
    EventBus churn;
    int ghost = 0;
    churn.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) {
        const SubscriptionId id = churn.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { ++ghost; });
        check(churn.unsubscribe(id), "a subscriber added this frame can be removed this frame");
    });
    churn.dispatch(q);
    churn.dispatch(q);
    check(ghost == 0, "and then never hears anything");

    // Kills: joining subscribers appended at the end instead of placed by priority.
    EventBus late;
    std::vector<std::string> order;
    bool added = false;
    late.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) {
        order.push_back("low");
        if (!added) {
            added = true;
            late.subscribe(EventFilter::of({EventKind::Kick}), [&](const Event&) { order.push_back("high"); }, 10);
        }
    }, 1);
    late.dispatch(q);
    order.clear();
    late.dispatch(q);
    check((order == std::vector<std::string>{"high", "low"}), "a subscriber that joined mid-frame takes its priority's place");

    // Kills: subscribe accepting an empty handler.
    EventBus none;
    check(none.subscribe(EventFilter::any(), EventHandler{}) == kNoSubscription && none.subscribers() == 0, "an empty handler is not a subscriber");
}

// Two luminance params and an int on a range that does not start or end on whole numbers.
enum : ParamId { kFlash, kBloom, kPalette };
ParamSchema extraSchema() {
    return ParamSchema({
        {"flash", ParamKind::Float, 0.0f, 0.0f, 1.0f, true},
        {"bloom", ParamKind::Float, 0.0f, 0.0f, 1.0f, true},
        {"palette", ParamKind::Int, 3.0f, 0.5f, 3.7f, false},
    });
}

void testRouteEdges() {
    EventQueue q;
    const auto send = [&q](EventBus& bus, EventKind kind, double t, float strength = 1.0f) {
        q.clear();
        q.push(Event{kind, strength, 0, t});
        bus.dispatch(q);
    };

    // Kills: a route ignoring its filter. Unrouted events change nothing.
    {
        ParamStore store(schema());
        EventBus bus;
        EventRoutes routes(store);
        routes.attach(bus);
        routes.add({EventFilter::of({EventKind::Downbeat}), 1, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
        routes.add({EventFilter::of({EventKind::Drop}), 1, kZoom, {ActionKind::Pulse, 1.0f, 0.2f}});
        send(bus, EventKind::Kick, 0.0);
        send(bus, EventKind::Snare, 0.1);
        send(bus, EventKind::KeyChange, 0.2);
        store.clearModulation();
        routes.modulate(1.0f / 60.0f);
        check(near(store.committed(kFolds), 6.0f) && near(store.effective(kZoom), 1.0f) && routes.livePulses() == 0,
              "events no route answers change nothing");
    }
    // Kills: everyNth counting events the route does not match.
    {
        ParamStore store(schema());
        EventBus bus;
        EventRoutes routes(store);
        routes.attach(bus);
        routes.add({EventFilter::of({EventKind::Downbeat}), 2, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
        send(bus, EventKind::Downbeat, 0.0);
        send(bus, EventKind::Kick, 0.1);
        send(bus, EventKind::Downbeat, 0.2);
        check(near(store.committed(kFolds), 7.0f), "every 2nd downbeat is the 2nd downbeat, whatever came between");
    }
    // Kills: everyNth = 0 left as 0 (a division by zero) instead of meaning every one.
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Beat}), 0, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
        routes.onEvent(Event{EventKind::Beat, 1.0f, 0, 0.0});
        routes.onEvent(Event{EventKind::Beat, 1.0f, 0, 0.5});
        check(near(store.committed(kFolds), 8.0f), "every 0th is every one");
    }
    // Kills: attach() not leaving the bus it was on (a second attach would fire every route twice per event).
    {
        ParamStore store(schema());
        EventBus bus;
        EventRoutes routes(store);
        routes.attach(bus);
        routes.attach(bus);
        routes.add({EventFilter::of({EventKind::Phrase}), 1, kFolds, {ActionKind::Step, 1.0f, 0.0f}});
        send(bus, EventKind::Phrase, 0.0);
        check(bus.subscribers() == 1 && near(store.committed(kFolds), 7.0f), "a route fires its target once per event");
        // Kills: detach() not unsubscribing.
        routes.detach();
        send(bus, EventKind::Phrase, 1.0);
        check(bus.subscribers() == 0 && near(store.committed(kFolds), 7.0f), "detached routes hear nothing");
    }
    // Kills: an action going around the state machine (a driven param must not move under an event).
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Drop}), 1, kZoom, {ActionKind::Set, 2.5f, 0.0f}});
        store.input(kZoom, ParamInput::drive());
        store.input(kZoom, ParamInput::set(0.5f, Origin::Timeline));
        routes.onEvent(Event{EventKind::Drop, 1.0f, 0, 0.0});
        check(store.phase(kZoom) == ParamPhase::Driven && near(store.base(kZoom), 0.5f), "an event never moves a driven param");
    }
    // Kills: GlideTo landing at once (its seconds dropped).
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Drop}), 1, kZoom, {ActionKind::GlideTo, 2.0f, 1.0f}});
        routes.onEvent(Event{EventKind::Drop, 1.0f, 0, 0.0});
        const bool gliding = store.phase(kZoom) == ParamPhase::Glide && near(store.committed(kZoom), 2.0f) && near(store.base(kZoom), 1.0f);
        store.advance(1.0f);
        check(gliding && near(store.base(kZoom), 2.0f), "a glide action eases there over its seconds");
    }
    // Kills: Toggle only ever turning on.
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Drop}), 1, kMirror, {ActionKind::Toggle, 0.0f, 0.0f}});
        routes.onEvent(Event{EventKind::Drop, 1.0f, 0, 0.0});
        routes.onEvent(Event{EventKind::Drop, 1.0f, 0, 1.0});
        check(near(store.committed(kMirror), 0.0f), "two toggles are back where they started");
    }
    // Kills: Step without the negative fix-up (a step back from the min would clamp, not wrap to the max).
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Beat}), 1, kFolds, {ActionKind::Step, -1.0f, 0.0f}});
        store.input(kFolds, ParamInput::set(1.0f));
        routes.onEvent(Event{EventKind::Beat, 1.0f, 0, 0.0});
        check(near(store.committed(kFolds), 12.0f), "a step back from the first wraps to the last");
    }
    // Kills: Step wrapping on the raw range ends instead of its whole numbers: [0.5, 3.7] holds 1, 2, 3.
    {
        ParamStore store(extraSchema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Beat}), 1, kPalette, {ActionKind::Step, 1.0f, 0.0f}});
        routes.onEvent(Event{EventKind::Beat, 1.0f, 0, 0.0});
        const float wrapped = store.committed(kPalette);
        routes.onEvent(Event{EventKind::Beat, 1.0f, 0, 0.5});
        check(near(wrapped, 1.0f) && near(store.committed(kPalette), 2.0f), "steps go 3 → 1 → 2 on a range of whole numbers 1..3");
    }
    // Kills: a pulse that does not decay as exp(-age / seconds): one time constant on it is 1/e of itself.
    // Also kills the default time constant (0.1 s) changing for a pulse with none of its own.
    for (float seconds : {0.1f, 0.0f}) {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Onset}), 1, kZoom, {ActionKind::Pulse, 1.0f, seconds}});
        routes.onEvent(Event{EventKind::Onset, 1.0f, 0, 0.0});
        store.clearModulation();
        routes.modulate(0.1f);
        const float start = store.effective(kZoom) - store.base(kZoom);
        store.clearModulation();
        routes.modulate(0.0f);
        const float later = store.effective(kZoom) - store.base(kZoom);
        check(near(start, 1.0f) && near(later, 0.36788f, 1e-3f), "a pulse falls to 1/e after one time constant (" + std::to_string(seconds) + ")");
    }
    // Kills: the flash limit's spacing in the wrong unit, or the limit clock moving on dropped pulses: kicks at
    // 0, 0.25, 0.5, 0.75 and 1.0 s start pulses at 0, 0.5 and 1.0 only (each at least 1/3 s after the last one).
    {
        ParamStore store(extraSchema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Kick}), 1, kFlash, {ActionKind::Pulse, 1.0f, 0.5f}});
        for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) routes.onEvent(Event{EventKind::Kick, 1.0f, 0, t});
        check(routes.livePulses() == 3 && routes.pulsesLimited() == 2, "a kick every 0.25 s flashes every 0.5 s");
    }
    // Kills: one flash clock shared by every param (the limit is per param).
    {
        ParamStore store(extraSchema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Kick}), 1, kFlash, {ActionKind::Pulse, 1.0f, 0.5f}});
        routes.add({EventFilter::of({EventKind::Kick}), 1, kBloom, {ActionKind::Pulse, 1.0f, 0.5f}});
        routes.onEvent(Event{EventKind::Kick, 1.0f, 0, 0.0});
        check(routes.livePulses() == 2 && routes.pulsesLimited() == 0, "two brightness-like params each take the kick");
    }
    // Kills: the limit applied to every param, and the pulse cap not holding at kMaxPulses.
    {
        ParamStore store(schema());
        EventRoutes routes(store);
        routes.add({EventFilter::of({EventKind::Hat}), 1, kZoom, {ActionKind::Pulse, 0.1f, 1.0f}});
        for (int i = 0; i < 10; ++i) routes.onEvent(Event{EventKind::Hat, 1.0f, 0, i * 0.01});
        const bool unlimited = routes.livePulses() == 10 && routes.pulsesLimited() == 0;
        for (int i = 10; i < 80; ++i) routes.onEvent(Event{EventKind::Hat, 1.0f, 0, i * 0.01});
        check(unlimited && routes.livePulses() == EventRoutes::kMaxPulses, "a non-luminance param is never rate-limited, and pulses are capped");
    }
}

// Seeded random sound frames through the whole chain: the same seed gives bit-identical params, every value
// stays in its range, and the flash limit holds over the whole run.
std::vector<float> runSeeded(std::uint32_t seed, std::uint32_t& limitedOut, bool& inRange) {
    ParamStore store(schema());
    EventBus bus;
    SoundEvents detect;
    EventRoutes routes(store);
    routes.attach(bus);
    routes.add({EventFilter::of({EventKind::Kick}), 1, kBrightness, {ActionKind::Pulse, 0.8f, 0.05f}});
    routes.add({EventFilter::of({EventKind::Beat}), 1, kHue, {ActionKind::Nudge, 0.13f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Downbeat}), 3, kFolds, {ActionKind::Step, 5.0f, 0.0f}});
    routes.add({EventFilter::of({EventKind::Snare}), 1, kZoom, {ActionKind::Pulse, 2.0f, 0.3f}});
    routes.add({EventFilter::of({EventKind::Drop}), 1, kMirror, {ActionKind::Toggle, 0.0f, 0.0f}});
    std::mt19937 g(seed);
    const ParamSchema s = schema();
    std::vector<float> out, all;
    EventQueue q;
    float beats = 0.0f, bars = 0.0f;
    inRange = true;
    for (int frame = 0; frame < 1200; ++frame) {
        GeodeSoundFrame f = quietFrame();
        const std::uint32_t r = g();
        f.kick = r % 4u == 0u ? unitOf(g) : 0.0f;
        f.snare = r % 9u == 0u ? unitOf(g) : 0.0f;
        f.drop = r % 97u == 0u ? 1.0f : 0.0f;
        const float step = static_cast<float>((r >> 8) % 3u);  // 0, 1 or 2 beats this frame
        beats = std::fmod(beats + step, 4096.0f);
        if (step > 0.0f && (r >> 12) % 4u == 0u) bars = std::fmod(bars + 1.0f, 4096.0f);
        f.beatCount = beats;
        f.barCount = bars;
        f.beat = step > 0.0f ? 1.0f : 0.0f;
        f.downbeat = f.beat > 0.0f && (r >> 12) % 4u == 0u ? 1.0f : 0.0f;
        const float dt = 1.0f / 60.0f;
        store.advance(dt);
        store.clearModulation();
        q.clear();
        detect.detect(f, frame / 60.0, dt, q);
        bus.dispatch(q);
        routes.modulate(dt);
        store.effectiveAll(all);
        for (std::size_t i = 0; i < all.size(); ++i) {
            const ParamDef& d = s.def(static_cast<ParamId>(i));
            inRange = inRange && std::isfinite(all[i]) && all[i] >= d.min && all[i] <= d.max;
        }
        out.insert(out.end(), all.begin(), all.end());
    }
    limitedOut = routes.pulsesLimited();
    return out;
}

void testSeededDeterminism() {
    std::uint32_t limitedA = 0, limitedB = 0;
    bool rangeA = false, rangeB = false;
    const std::vector<float> a = runSeeded(0x413u, limitedA, rangeA);
    const std::vector<float> b = runSeeded(0x413u, limitedB, rangeB);
    bool same = a.size() == b.size() && limitedA == limitedB;
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i] == b[i];
    check(same, "one seed, one result: bit-identical params over 1200 random frames");
    check(rangeA, "every effective value stays inside its range");
    // A kick on about a quarter of 1200 frames (20 s) is ~300 kicks; at most 3 a second (~60) may start.
    check(limitedA > 200, "the flash limit drops most of a random kick stream (" + std::to_string(limitedA) + ")");
}

}  // namespace

int main() {
    testSchema();
    testTransitionTable();
    testGlide();
    testTrial();
    testModulation();
    testWireIds();
    testSoundEvents();
    testQueue();
    testBus();
    testEventRoutes();
    testOriginChecks();
    testBadGlides();
    testTimeGoingBack();
    testDeterminism();
    testSchemaEdges();
    testStateEdges();
    testStateFuzz();
    testStoreEdges();
    testModulationOrderProperty();
    testSoundEventEdges();
    testBusEdges();
    testRouteEdges();
    testSeededDeterminism();
    if (failures == 0) std::printf("signal_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
