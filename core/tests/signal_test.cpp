// Host test of core/signal's params and events: the schema's one clamp, every cell of the param state
// machine's transition table, glides, trials and their spring back, the timeline taking and giving back a
// param, modulation that sums before one clamp (so a late stage can still pull back an early one), the
// sound-frame event detector, the bus's order and re-entrancy, and event routes (every Nth, steps,
// toggles, the finger still winning, and the flash limit on luminance pulses). Then two identical
// pipelines over 600 frames give bit-identical params.
// Built by CI (.github/workflows/android.yml, "Native host tests") from core/signal alone.
#include <cmath>
#include <cstdio>
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
    testDeterminism();
    if (failures == 0) std::printf("signal_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
