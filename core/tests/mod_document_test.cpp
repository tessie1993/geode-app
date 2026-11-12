// Host-side checks for the user's modulation document (core/viz/ModDocument, core/viz/ModCatalog and the
// handle-free half of api/geode_modulation_api.h): text installs have saved still reads, every forgiving
// read lands on a default or inside a range, each edit clamps, and the pickers list what the engine runs.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Modulation document host tests"); no GL.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_modulation_api.h"
#include "viz/ModCatalog.hpp"
#include "viz/ModDocument.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ReactionText.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

int ordinal(LfoTarget t) { return static_cast<int>(t); }

bool holds(const std::vector<int>& list, LfoTarget t) {
    for (int v : list) {
        if (v == ordinal(t)) return true;
    }
    return false;
}

// One wave slot and one envelope as nothing saved reads.
const char* const kDefaultLfo =
    "{\"enabled\":false,\"source\":\"LFO\",\"target\":\"NONE\",\"wave\":\"SINE\",\"rateSeconds\":2,\"depth\":0.3,"
    "\"polarity\":\"BIPOLAR\",\"curve\":\"LINEAR\",\"beats\":0,\"knob\":0,\"moment\":-1}";
const char* const kDefaultAdsr =
    "{\"enabled\":false,\"targets\":[],\"attack\":0.05,\"decay\":0.25,\"sustain\":0.5,\"release\":0.35,\"amount\":0.5,"
    "\"band\":\"BASS\",\"gateThreshold\":0.25,\"sustainTrack\":false,\"retrigger\":true,\"knob\":0,\"moment\":-1}";

std::string defaultLfos() { return std::string("[") + kDefaultLfo + "," + kDefaultLfo + "," + kDefaultLfo + "]"; }
std::string defaultAdsrs() { return std::string("[") + kDefaultAdsr + "," + kDefaultAdsr + "]"; }

// Three waves as installs have them saved: a float written as the double it widens to, a whole number
// without a fraction.
const char* const kSavedLfos =
    "[{\"enabled\":true,\"source\":\"BASS\",\"target\":\"ZOOM\",\"wave\":\"TRIANGLE\",\"rateSeconds\":4.5,"
    "\"depth\":0.30000001192092896,\"polarity\":\"POSITIVE\",\"curve\":\"SMOOTH\",\"beats\":8,\"knob\":0,\"moment\":-1},"
    "{\"enabled\":false,\"source\":\"LFO\",\"target\":\"NONE\",\"wave\":\"SINE\",\"rateSeconds\":2,"
    "\"depth\":0.30000001192092896,\"polarity\":\"BIPOLAR\",\"curve\":\"LINEAR\",\"beats\":0,\"knob\":0,\"moment\":-1},"
    "{\"enabled\":true,\"source\":\"LFO\",\"target\":\"MACRO\",\"wave\":\"RANDOM_GLIDE\",\"rateSeconds\":0.05000000074505806,"
    "\"depth\":0.75,\"polarity\":\"NEGATIVE\",\"curve\":\"EXPONENTIAL\",\"beats\":0,\"knob\":3,\"moment\":4}]";

// Two envelopes as installs have them saved.
const char* const kSavedAdsrs =
    "[{\"enabled\":true,\"targets\":[\"ZOOM\",\"BLOOM\"],\"attack\":0.05000000074505806,\"decay\":0.25,\"sustain\":0.5,"
    "\"release\":0.3499999940395355,\"amount\":0.5,\"band\":\"BASS\",\"gateThreshold\":0.25,\"sustainTrack\":false,"
    "\"retrigger\":true,\"knob\":0,\"moment\":-1},"
    "{\"enabled\":false,\"targets\":[],\"attack\":0.20000000298023224,\"decay\":1.5,\"sustain\":1,\"release\":2,"
    "\"amount\":1.5,\"band\":\"WIDTH\",\"gateThreshold\":0.05000000074505806,\"sustainTrack\":true,\"retrigger\":false,"
    "\"knob\":0,\"moment\":2}]";

// Routes as installs have them saved.
const char* const kSavedReactions =
    "reactions/2\n"
    "true|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35\n"
    "true|SNARE_2|WARP|FOLLOW|0.6|SMOOTH|0.03|0.45\n"
    "false|KICK_4|ROTATION|STEP|-0.2|LINEAR|0.12|0.12";

void savedWavesRead() {
    ModDocument doc;
    doc.readLfos(kSavedLfos);
    const LfoConfig& a = doc.lfos[0];
    check(a.enabled && a.source == ModSource::Bass && a.target == LfoTarget::Zoom && a.wave == LfoWave::Triangle,
          "a saved wave keeps its switch, source, target and shape");
    check(a.rateSeconds == 4.5f && a.depth == 0.3f, "a saved wave keeps its period and depth");
    check(a.polarity == ModPolarity::Positive && a.curve == ModCurve::Smooth && a.beats == 8.0f,
          "a saved wave keeps its direction, curve and beat timing");
    check(a.knob == 0 && a.moment == -1, "a saved wave with no knob and no moment has neither");
    const LfoConfig& c = doc.lfos[2];
    check(c.target == LfoTarget::Macro && c.knob == 3 && c.moment == 4 && c.wave == LfoWave::RandomGlide,
          "a wave on a knob and a moment keeps both");
    check(c.rateSeconds == 0.05f && c.depth == 0.75f && c.polarity == ModPolarity::Negative, "the shortest period reads");

    const std::string text = doc.lfosText();
    const std::string want =
        std::string("[{\"enabled\":true,\"source\":\"BASS\",\"target\":\"ZOOM\",\"wave\":\"TRIANGLE\",\"rateSeconds\":4.5,"
                    "\"depth\":0.3,\"polarity\":\"POSITIVE\",\"curve\":\"SMOOTH\",\"beats\":8,\"knob\":0,\"moment\":-1},") +
        kDefaultLfo +
        ",{\"enabled\":true,\"source\":\"LFO\",\"target\":\"MACRO\",\"wave\":\"RANDOM_GLIDE\",\"rateSeconds\":0.05,"
        "\"depth\":0.75,\"polarity\":\"NEGATIVE\",\"curve\":\"EXPONENTIAL\",\"beats\":0,\"knob\":3,\"moment\":4}]";
    check(text == want, "the waves write back as: " + text);
    ModDocument again;
    again.readLfos(text);
    check(again.lfosText() == text, "what is written reads back to the same text");
}

void olderWavesRead() {
    // A slot from before the period was saved: a frequency, and a tempo lock nothing reads any more.
    ModDocument doc;
    doc.readLfos("[{\"enabled\":true,\"source\":\"LFO\",\"target\":\"ROTATION\",\"wave\":\"SINE\",\"rateHz\":0.25,"
                 "\"depth\":0.5,\"tempoSync\":true},"
                 "{\"enabled\":true,\"target\":\"WARP\",\"rateHz\":0},"
                 "{\"enabled\":true,\"target\":\"TWIST\",\"rateHz\":100}]");
    check(doc.lfos[0].rateSeconds == 4.0f && doc.lfos[0].target == LfoTarget::Rotation && doc.lfos[0].enabled,
          "a quarter of a cycle a second is a four second period");
    check(doc.lfos[0].beats == 0.0f && doc.lfos[0].moment == -1 && doc.lfos[0].knob == 0, "an older slot runs free");
    check(doc.lfos[1].rateSeconds == LfoConfig::kDefaultRateSeconds, "no frequency is the default period");
    check(doc.lfos[2].rateSeconds == LfoConfig::kMinRateSeconds, "a frequency too fast lands on the shortest period");
    check(doc.lfos[1].source == ModSource::Lfo && doc.lfos[1].wave == LfoWave::Sine && doc.lfos[1].depth == 0.3f,
          "fields an older slot never had take their defaults");

    ModDocument both;
    both.readLfos("[{\"rateSeconds\":7,\"rateHz\":0.5}]");
    check(both.lfos[0].rateSeconds == 7.0f, "a saved period wins over a frequency");
    ModDocument neither;
    neither.readLfos("[{\"enabled\":true}]");
    check(neither.lfos[0].rateSeconds == LfoConfig::kDefaultRateSeconds && neither.lfos[0].enabled, "no rate at all is the default");
}

void forgivingWaves() {
    const std::string defaults = defaultLfos();
    for (const char* text : {"", "not json", "{\"enabled\":true}", "[", "null", "42", "[1,\"two\",null]"}) {
        ModDocument doc;
        doc.readLfos(text);
        check(doc.lfosText() == defaults, std::string("text that holds no slot reads as the defaults: ") + text);
    }
    ModDocument fresh;
    check(fresh.lfosText() == defaults, "a new document writes the defaults: " + fresh.lfosText());

    ModDocument one;
    one.readLfos("[{\"enabled\":true,\"target\":\"ZOOM\"}]");
    check(one.lfos[0].enabled && one.lfos[0].target == LfoTarget::Zoom, "a short list fills its own slots");
    check(!one.lfos[1].enabled && one.lfos[2].target == LfoTarget::None, "a short list leaves the rest default");

    ModDocument many;
    many.readLfos(std::string("[") + kDefaultLfo + "," + kDefaultLfo + "," + kDefaultLfo +
                  ",{\"enabled\":true,\"target\":\"ZOOM\"}]");
    check(many.lfosText() == defaults, "slots past the last are ignored");

    ModDocument odd;
    odd.readLfos("[{\"enabled\":\"TRUE\",\"source\":\"THEREMIN\",\"target\":\"PULSE\",\"wave\":7,\"rateSeconds\":500,"
                 "\"depth\":7,\"polarity\":\"SIDEWAYS\",\"curve\":null,\"beats\":3,\"knob\":-4,\"moment\":99},"
                 "{\"rateSeconds\":\"4.5\",\"depth\":-2,\"beats\":16,\"knob\":\"12\",\"moment\":\"9\"},"
                 "{\"rateSeconds\":\"fast\",\"depth\":\"\",\"beats\":64,\"knob\":1000000,\"moment\":-7}]");
    const LfoConfig& a = odd.lfos[0];
    check(a.enabled, "a quoted switch reads");
    check(a.source == ModSource::Lfo && a.target == LfoTarget::None && a.wave == LfoWave::Sine &&
              a.polarity == ModPolarity::Bipolar && a.curve == ModCurve::Linear,
          "a name nothing carries is the default");
    check(a.rateSeconds == LfoConfig::kMaxRateSeconds && a.depth == 1.0f, "a number past its range lands on the edge");
    check(a.beats == 0.0f && a.knob == 0 && a.moment == -1, "a period, knob or moment nothing offers is none");
    const LfoConfig& b = odd.lfos[1];
    check(b.rateSeconds == 4.5f && b.depth == 0.0f && b.beats == 16.0f && b.knob == 12 && b.moment == 9,
          "quoted numbers read; a depth below zero is zero");
    const LfoConfig& c = odd.lfos[2];
    check(c.rateSeconds == LfoConfig::kDefaultRateSeconds && c.depth == 0.3f && c.beats == 0.0f && c.knob == 0 && c.moment == -1,
          "text that is no number is the default");
}

void savedEnvelopesRead() {
    ModDocument doc;
    doc.readAdsrs(kSavedAdsrs);
    const AdsrConfig& a = doc.adsrs[0];
    check(a.enabled && a.targets.size() == 2 && a.targets[0] == LfoTarget::Zoom && a.targets[1] == LfoTarget::Bloom,
          "a saved envelope keeps its targets in order");
    check(a.attack == 0.05f && a.decay == 0.25f && a.sustain == 0.5f && a.release == 0.35f && a.amount == 0.5f,
          "a saved envelope keeps its stages");
    check(a.band == EnvBand::Bass && a.gateThreshold == 0.25f && !a.sustainTrack && a.retrigger && a.moment == -1,
          "a saved envelope keeps its gate");
    const AdsrConfig& b = doc.adsrs[1];
    check(!b.enabled && b.targets.empty() && b.attack == 0.2f && b.decay == 1.5f && b.sustain == 1.0f && b.release == 2.0f,
          "the second envelope reads");
    check(b.amount == 1.5f && b.band == EnvBand::Width && b.gateThreshold == 0.05f && b.sustainTrack && !b.retrigger && b.moment == 2,
          "its band, gate and moment read");

    const std::string text = doc.adsrsText();
    const std::string want =
        "[{\"enabled\":true,\"targets\":[\"ZOOM\",\"BLOOM\"],\"attack\":0.05,\"decay\":0.25,\"sustain\":0.5,\"release\":0.35,"
        "\"amount\":0.5,\"band\":\"BASS\",\"gateThreshold\":0.25,\"sustainTrack\":false,\"retrigger\":true,\"knob\":0,"
        "\"moment\":-1},"
        "{\"enabled\":false,\"targets\":[],\"attack\":0.2,\"decay\":1.5,\"sustain\":1,\"release\":2,\"amount\":1.5,"
        "\"band\":\"WIDTH\",\"gateThreshold\":0.05,\"sustainTrack\":true,\"retrigger\":false,\"knob\":0,\"moment\":2}]";
    check(text == want, "the envelopes write back as: " + text);
    ModDocument again;
    again.readAdsrs(text);
    check(again.adsrsText() == text, "what is written reads back to the same text");
}

void forgivingEnvelopes() {
    const std::string defaults = defaultAdsrs();
    for (const char* text : {"", "garbage", "{}", "[[],7]"}) {
        ModDocument doc;
        doc.readAdsrs(text);
        check(doc.adsrsText() == defaults, std::string("text that holds no envelope reads as the defaults: ") + text);
    }
    ModDocument fresh;
    check(fresh.adsrsText() == defaults, "a new document writes the defaults: " + fresh.adsrsText());

    // An envelope from before the gate, the knob and the moment were saved, with targets no build knows.
    ModDocument doc;
    doc.readAdsrs("[{\"enabled\":true,\"targets\":[\"ZOOM\",\"PULSE\",\"NONE\",7,\"ZOOM\",\"TWIST\"],\"attack\":0,\"decay\":9,"
                  "\"sustain\":-1,\"release\":0.001,\"amount\":4,\"band\":\"SUB\"}]");
    const AdsrConfig& a = doc.adsrs[0];
    check(a.targets.size() == 2 && a.targets[0] == LfoTarget::Zoom && a.targets[1] == LfoTarget::Twist,
          "a target nothing carries, nothing, and a repeat are dropped");
    check(a.attack == 0.005f && a.decay == 1.5f && a.sustain == 0.0f && a.release == 0.02f && a.amount == 1.5f,
          "each stage lands inside its range");
    check(a.band == EnvBand::Bass && a.gateThreshold == 0.25f && !a.sustainTrack && a.retrigger && a.knob == 0 && a.moment == -1,
          "fields an older envelope never had take their defaults");
    check(!doc.adsrs[1].enabled && doc.adsrs[1].targets.empty(), "a missing envelope is the default");
}

void savedReactionsRead() {
    ModDocument doc;
    check(doc.reactionsText().empty() && doc.handMask() == 0u, "a new document is Auto");
    doc.readReactions(kSavedReactions, hand::kAll);
    check(doc.hand.user && doc.hand.count == 3 && doc.handMask() == hand::kAll, "saved routes are the user's");
    check(doc.hand.routes[0].source == ReactSource::Kick && doc.hand.routes[0].target == LfoTarget::ColorShift &&
              doc.hand.routes[0].mode == ReactMode::Jump && doc.hand.routes[0].amount == 0.35f,
          "a saved route keeps its source, target, mode and amount");
    check(!doc.hand.routes[2].enabled && doc.hand.routes[2].amount == -0.2f, "a route switched off stays, switched off");
    check(doc.reactionsText() == kSavedReactions, "the routes write back as saved: " + doc.reactionsText());

    ModDocument masked;
    masked.readReactions(kSavedReactions, hand::bit(Moment::Kick) | 0x80000000u);
    check(masked.handMask() == hand::bit(Moment::Kick), "the mask keeps only bits a moment has");

    for (const char* text : {"", "[{\"source\":\"KICK\"}]", "reactions/1\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3"}) {
        ModDocument none;
        none.readReactions(text, hand::kAll);
        check(!none.hand.user && none.reactionsText().empty() && none.handMask() == 0u,
              std::string("text that is no route list is Auto, whatever the mask: ") + text);
    }

    ModDocument empty;
    empty.readReactions("reactions/2", hand::kAll);
    check(empty.hand.user && empty.hand.count == 0 && empty.reactionsText() == "reactions/2",
          "an empty list is the user's: nothing plays");
}

void waveEdits() {
    ModDocument doc;
    doc.setLfo(0, static_cast<int>(LfoField::Source), static_cast<float>(ModSource::Bass));
    check(doc.lfos[0].source == ModSource::Bass && doc.lfos[0].polarity == ModPolarity::Positive,
          "a band only rises, so picking one turns the wave up only");
    doc.setLfo(0, static_cast<int>(LfoField::Source), static_cast<float>(ModSource::StereoPan));
    check(doc.lfos[0].polarity == ModPolarity::Bipolar, "left and right goes both ways");
    doc.setLfo(0, static_cast<int>(LfoField::Polarity), static_cast<float>(ModPolarity::Negative));
    check(doc.lfos[0].polarity == ModPolarity::Negative && doc.lfos[0].source == ModSource::StereoPan,
          "the direction is still the user's to set");

    doc.setLfo(0, static_cast<int>(LfoField::Source), 99.0f);
    doc.setLfo(0, static_cast<int>(LfoField::Wave), -1.0f);
    doc.setLfo(0, static_cast<int>(LfoField::Target), 1000.0f);
    doc.setLfo(0, static_cast<int>(LfoField::Curve), std::nanf(""));
    check(doc.lfos[0].source == ModSource::StereoPan && doc.lfos[0].wave == LfoWave::Sine &&
              doc.lfos[0].target == LfoTarget::None && doc.lfos[0].curve == ModCurve::Linear,
          "an ordinal outside its enum changes nothing");

    doc.setLfo(1, static_cast<int>(LfoField::RateSeconds), 1000.0f);
    check(doc.lfos[1].rateSeconds == LfoConfig::kMaxRateSeconds, "a period lands inside its range");
    doc.setLfo(1, static_cast<int>(LfoField::RateSeconds), std::nanf(""));
    check(doc.lfos[1].rateSeconds == LfoConfig::kMaxRateSeconds, "a period that is no number changes nothing");
    doc.setLfo(1, static_cast<int>(LfoField::Depth), -3.0f);
    check(doc.lfos[1].depth == 0.0f, "a depth lands inside its range");
    doc.setLfo(1, static_cast<int>(LfoField::Beats), 4.0f);
    check(doc.lfos[1].beats == 4.0f, "an offered beat period is taken");
    doc.setLfo(1, static_cast<int>(LfoField::Beats), 5.0f);
    check(doc.lfos[1].beats == 0.0f, "a beat period nothing offers is free timing");
    doc.setLfo(1, static_cast<int>(LfoField::Enabled), 1.0f);
    doc.setLfo(1, static_cast<int>(LfoField::Knob), 7.0f);
    doc.setLfo(1, static_cast<int>(LfoField::Moment), static_cast<float>(Moment::Drop));
    check(doc.lfos[1].enabled && doc.lfos[1].knob == 7 && doc.lfos[1].moment == static_cast<int>(Moment::Drop),
          "the switch, the knob and the moment set");
    doc.setLfo(1, static_cast<int>(LfoField::Moment), -1.0f);
    check(doc.lfos[1].moment == -1, "no moment runs free");

    const std::string before = doc.lfosText();
    doc.setLfo(-1, 0, 1.0f);
    doc.setLfo(LfoEngine::kSlots, 0, 1.0f);
    doc.setLfo(0, -1, 1.0f);
    doc.setLfo(0, kLfoFieldCount, 1.0f);
    check(doc.lfosText() == before, "an unknown slot or field changes nothing");
}

void envelopeEdits() {
    ModDocument doc;
    const int target = static_cast<int>(AdsrField::Target);
    doc.setAdsr(0, target, static_cast<float>(ordinal(LfoTarget::Zoom)));
    doc.setAdsr(0, target, static_cast<float>(ordinal(LfoTarget::Bloom)));
    check(doc.adsrs[0].targets.size() == 2, "a target the envelope does not move is added");
    doc.setAdsr(0, target, static_cast<float>(ordinal(LfoTarget::Zoom)));
    check(doc.adsrs[0].targets.size() == 1 && doc.adsrs[0].targets[0] == LfoTarget::Bloom, "one it moves is taken away");
    doc.setAdsr(0, target, 0.0f);
    doc.setAdsr(0, target, 5000.0f);
    check(doc.adsrs[0].targets.size() == 1, "nothing, and an ordinal outside the enum, are no target");

    doc.setAdsr(1, static_cast<int>(AdsrField::Attack), 0.0f);
    doc.setAdsr(1, static_cast<int>(AdsrField::Release), 50.0f);
    doc.setAdsr(1, static_cast<int>(AdsrField::Amount), 50.0f);
    doc.setAdsr(1, static_cast<int>(AdsrField::GateThreshold), 0.0f);
    check(doc.adsrs[1].attack == 0.005f && doc.adsrs[1].release == 2.0f && doc.adsrs[1].amount == 1.5f &&
              doc.adsrs[1].gateThreshold == 0.05f,
          "each number lands inside its range");
    doc.setAdsr(1, static_cast<int>(AdsrField::Band), static_cast<float>(EnvBand::Treble));
    doc.setAdsr(1, static_cast<int>(AdsrField::Band), 44.0f);
    doc.setAdsr(1, static_cast<int>(AdsrField::Retrigger), 0.0f);
    doc.setAdsr(1, static_cast<int>(AdsrField::SustainTrack), 1.0f);
    check(doc.adsrs[1].band == EnvBand::Treble && !doc.adsrs[1].retrigger && doc.adsrs[1].sustainTrack,
          "the band and the switches set; a band outside the enum changes nothing");

    const std::string before = doc.adsrsText();
    doc.setAdsr(AdsrEngine::kCount, 0, 1.0f);
    doc.setAdsr(0, target + 1, 1.0f);
    check(doc.adsrsText() == before, "an unknown envelope or field changes nothing");
}

void reactionEdits() {
    // A style that deals nothing of its own, so what plays is exactly the user's.
    const StyleDna none{};
    const std::string style;
    ModDocument doc;
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::RouteAdd), 0, 0, 0.0f);
    check(doc.hand.user && doc.hand.count == 1 && doc.handMask() == hand::kAll, "adding a route makes the list the user's");
    check(doc.hand.routes[0].enabled && doc.hand.routes[0].target != LfoTarget::None, "the added route plays and moves something");

    const int set = static_cast<int>(ReactionEdit::RouteSet);
    doc.editReactions(none, style, set, 0, static_cast<int>(RouteField::Amount), 99.0f);
    check(doc.hand.routes[0].amount == reactiontext::kMaxAmount, "a route's amount lands inside the matrix's range");
    doc.editReactions(none, style, set, 0, static_cast<int>(RouteField::Target), static_cast<float>(ordinal(LfoTarget::Twist)));
    check(doc.hand.routes[0].target == LfoTarget::Twist, "a route's target sets");
    doc.editReactions(none, style, set, 0, static_cast<int>(RouteField::Target), 0.0f);
    check(doc.hand.routes[0].target == LfoTarget::Twist, "a route never moves nothing");
    doc.editReactions(none, style, set, 0, static_cast<int>(RouteField::ReleaseSeconds), -4.0f);
    check(doc.hand.routes[0].releaseSeconds == 0.0f, "a fall time is never negative");

    const std::string before = doc.reactionsText();
    doc.editReactions(none, style, set, 5, 0, 1.0f);
    doc.editReactions(none, style, set, 0, kRouteFieldCount, 1.0f);
    doc.editReactions(none, style, kReactionEditCount, 0, 0, 1.0f);
    doc.editReactions(none, style, -1, 0, 0, 1.0f);
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::RouteRemove), 3, 0, 0.0f);
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::RecipeToggle), kEventRecipeCount, 0, 0.0f);
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::MusicToggle), kMomentCount, 0, 0.0f);
    check(doc.reactionsText() == before, "an edit that names nothing changes nothing");

    ModDocument read;
    read.readReactions(before, doc.handMask());
    check(read.hand.count == 1 && read.hand.routes[0].target == LfoTarget::Twist &&
              read.hand.routes[0].amount == reactiontext::kMaxAmount,
          "an edited list reads back from its text");

    for (int i = 0; i < ReactionEngine::kRoutes + 4; ++i) {
        doc.editReactions(none, style, static_cast<int>(ReactionEdit::RouteAdd), 0, 0, 0.0f);
    }
    check(doc.hand.count == ReactionEngine::kRoutes, "the matrix's room is the limit");
    float playing[ReactionEngine::kRoutes * kRouteValues] = {};
    const int wrote = doc.playingValues(none, style, playing, ReactionEngine::kRoutes * kRouteValues);
    check(wrote == ReactionEngine::kRoutes * kRouteValues, "every route that plays is listed");
    check(playing[static_cast<int>(RouteField::Target)] == static_cast<float>(ordinal(LfoTarget::Twist)),
          "a listed route carries its target's ordinal");

    doc.editReactions(none, style, static_cast<int>(ReactionEdit::RouteRemove), 0, 0, 0.0f);
    check(doc.hand.count == ReactionEngine::kRoutes - 1 && doc.hand.routes[0].target != LfoTarget::Twist,
          "removing a route closes the gap");
    doc.editReactions(none, style, static_cast<int>(ReactionEdit::Clear), 0, 0, 0.0f);
    check(!doc.hand.user && doc.reactionsText().empty() && doc.handMask() == 0u, "clearing is Auto again");

    check(doc.recipeState(none, style, -1) == 0u && doc.recipeState(none, style, kEventRecipeCount) == 0u,
          "an unknown recipe has no state");
    float music[kMomentCount * kMusicValues] = {};
    check(doc.musicValues(none, music, kMomentCount * kMusicValues) == kMomentCount * kMusicValues, "every moment has a row");
    check(music[0] == 0.0f && music[1] == 0.0f, "nothing is the hand's and nothing plays while Auto on such a style");
}

void values() {
    ModDocument doc;
    doc.readLfos(kSavedLfos);
    float lfo[kLfoValues] = {};
    check(doc.lfoValues(0, "", lfo, kLfoValues) == kLfoValues, "a wave slot writes all its values");
    check(lfo[static_cast<int>(LfoField::Enabled)] == 1.0f &&
              lfo[static_cast<int>(LfoField::Source)] == static_cast<float>(ModSource::Bass) &&
              lfo[static_cast<int>(LfoField::Target)] == static_cast<float>(ordinal(LfoTarget::Zoom)) &&
              lfo[static_cast<int>(LfoField::RateSeconds)] == 4.5f && lfo[static_cast<int>(LfoField::Beats)] == 8.0f &&
              lfo[static_cast<int>(LfoField::Moment)] == -1.0f,
          "a wave's values are its fields, in field order");
    check(doc.lfoValues(1, "", lfo, kLfoValues) == kLfoValues && lfo[kLfoFieldCount] == 1.0f,
          "a wave aimed at nothing has nothing to miss");
    check(doc.lfoValues(0, "", lfo, 3) == 3, "a short buffer gets what fits");
    check(doc.lfoValues(LfoEngine::kSlots, "", lfo, kLfoValues) == 0 && doc.lfoValues(-1, "", lfo, kLfoValues) == 0 &&
              doc.lfoValues(0, "", nullptr, kLfoValues) == 0,
          "an unknown slot or no buffer writes nothing");

    doc.readAdsrs(kSavedAdsrs);
    float adsr[kAdsrValues + 8] = {};
    check(doc.adsrValues(0, adsr, kAdsrValues + 8) == kAdsrValues + 2, "an envelope writes its fields, then its targets");
    check(adsr[static_cast<int>(AdsrField::Release)] == 0.35f && adsr[static_cast<int>(AdsrField::Retrigger)] == 1.0f &&
              adsr[kAdsrValues] == static_cast<float>(ordinal(LfoTarget::Zoom)) &&
              adsr[kAdsrValues + 1] == static_cast<float>(ordinal(LfoTarget::Bloom)),
          "an envelope's values are its fields in field order, its targets after them");
    check(doc.adsrValues(1, adsr, kAdsrValues + 8) == kAdsrValues, "an envelope with no target writes none");
    check(doc.adsrValues(AdsrEngine::kCount, adsr, kAdsrValues) == 0, "an unknown envelope writes nothing");
}

std::vector<int> offeredTo(const std::string& style, int picker) {
    std::vector<int> out(static_cast<size_t>(modtargets::kTargetCount) + 1);
    out.resize(static_cast<size_t>(ModDocument::targetsOffered(style, picker, out.data(), static_cast<int>(out.size()))));
    return out;
}

void pickers() {
    // No style: only what moves every style, which is what a picker must never lose.
    const std::vector<int> first = offeredTo("", 0);
    check(!first.empty() && first[0] == 0, "a wave's picker starts with nothing");
    check(holds(first, LfoTarget::Zoom) && holds(first, LfoTarget::Macro), "a wave may move a param and a knob");
    check(!holds(first, LfoTarget::Lfo1Rate) && holds(first, LfoTarget::Lfo2Rate) && holds(first, LfoTarget::Lfo3Depth),
          "a wave steers only a later wave");
    const std::vector<int> last = offeredTo("", LfoEngine::kSlots - 1);
    check(!holds(last, LfoTarget::Lfo1Rate) && !holds(last, LfoTarget::Lfo3Rate) && holds(last, LfoTarget::Zoom),
          "the last wave steers none");
    const std::vector<int> route = offeredTo("", modtargets::kPickerReaction);
    check(!holds(route, LfoTarget::None) && !holds(route, LfoTarget::Macro) && !holds(route, LfoTarget::Lfo2Rate) &&
              holds(route, LfoTarget::Zoom),
          "a route moves scene params only");
    for (const std::vector<int>* list : {&first, &last, &route}) {
        check(!holds(*list, LfoTarget::Retired5) && !holds(*list, LfoTarget::Retired25), "a retired target is never listed");
        for (size_t i = 1; i < list->size(); ++i) check((*list)[i - 1] < (*list)[i], "a picker lists in catalog order");
    }
    check(offeredTo("", LfoEngine::kSlots).empty() && offeredTo("", -7).empty(), "an unknown picker lists nothing");
    int two[2] = {-1, -1};
    check(ModDocument::targetsOffered("", 0, two, 2) == 2 && two[0] == 0, "a short buffer gets the first that fit");

    ModDocument doc;
    doc.readAdsrs(kSavedAdsrs);
    std::vector<int> env(static_cast<size_t>(modtargets::kTargetCount));
    env.resize(static_cast<size_t>(doc.adsrTargetsOffered(0, "", env.data(), static_cast<int>(env.size()))));
    check(holds(env, LfoTarget::Zoom) && holds(env, LfoTarget::Bloom), "an envelope lists what it already moves");
    check(holds(env, LfoTarget::Lfo1Rate) && holds(env, LfoTarget::Macro) && !holds(env, LfoTarget::None),
          "an envelope may steer any wave and a knob, never nothing");
    check(doc.adsrTargetsOffered(AdsrEngine::kCount, "", env.data(), 1) == 0, "an unknown envelope lists nothing");
}

bool hasLine(const std::string& text, const std::string& line) {
    return text.rfind(line + "\n", 0) == 0 || text.find("\n" + line + "\n") != std::string::npos;
}

void catalog() {
    const std::string& text = modcatalog::text();
    check(text.rfind("catalog|1\n", 0) == 0, "the catalog opens with its version");
    for (const char* line : {
             "count|waves|3",
             "count|envelopes|2",
             "count|routes|12",
             "range|lfo.rateSeconds|2|0.05|60",
             "range|lfo.depth|0.3|0|1",
             "range|adsr.attack|0.05|0.005|1",
             "range|adsr.release|0.35|0.02|2",
             "range|music.strength|1|0|2",
             "beats|0|0|Free",
             "beats|1|1|1 beat",
             "beats|6|32|32 beats",
             "target|0|NONE|None",
             "target|5|RETIRED_5|",
             "target|15|COLOR_SHIFT|Hue shift",
             "source|0|LFO|Steady wave|0",
             "source|1|BASS|Bass band|1",
             "source|8|STEREO_PAN|L/R movement|0",
             "polarity|2|NEGATIVE|Down only",
             "curve|3|SMOOTH|S-curve",
             "wave|5|RANDOM_GLIDE|Random glide",
             "band|3|RMS|Level",
             "mode|0|FOLLOW|Follow",
             "group|0|LEVELS|Levels",
             "recipe|0|KICK_PUNCH|Kick punch|When the kick hits, push in",
         }) {
        check(hasLine(text, line), std::string("the catalog has the row: ") + line);
    }
    check(&modcatalog::text() == &text, "the catalog is built once");

    // Every name the document writes is one it reads.
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        check(modcatalog::targetNamed(modtargets::name(static_cast<LfoTarget>(i))) == i, "a target reads by the name it saves as");
    }
    for (int i = 0; i < modcatalog::kSourceCount; ++i) {
        check(modcatalog::sourceNamed(modtargets::sourceName(static_cast<ModSource>(i))) == i, "a source reads by its name");
    }
    for (int i = 0; i < modcatalog::kWaveCount; ++i) {
        check(modcatalog::waveNamed(modtargets::waveName(static_cast<LfoWave>(i))) == i, "a shape reads by its name");
    }
    for (int i = 0; i < modcatalog::kBandCount; ++i) {
        check(modcatalog::bandNamed(modcatalog::bandName(static_cast<EnvBand>(i))) == i, "a band reads by its name");
    }
    check(modcatalog::targetNamed("") == -1 && modcatalog::targetNamed("zoom") == -1 && modcatalog::bandNamed("SUB") == -1,
          "a name nothing carries is no ordinal");
    check(modcatalog::beatChoice(0) == 0.0f && modcatalog::beatChoice(3) == 4.0f && modcatalog::beatChoice(99) == 0.0f,
          "the beat periods are the picker's");
}

// Customize › Music's moment and action rows, byte-equal to the copy the shell's JVM tests read
// (MusicMomentsTest): the Kotlin enums' order is checked against it there.
void musicRows(const char* fixture) {
    std::string rows;
    std::istringstream lines(modcatalog::text());
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("moment|", 0) == 0 || line.rfind("music|", 0) == 0) rows += line + "\n";
    }
    std::ifstream in(fixture, std::ios::binary);
    std::stringstream want;
    want << in.rdbuf();
    check(in.good() || in.eof(), std::string("the music fixture reads: ") + fixture);
    check(rows == want.str(), "the music rows equal " + std::string(fixture) + ":\n" + rows);

    // Every action once, light before motion, so one divider splits the menu.
    unsigned seen = 0;
    int groupChanges = 0;
    std::string group;
    std::istringstream music(rows);
    for (std::string line; std::getline(music, line);) {
        if (line.rfind("music|", 0) != 0) continue;
        const int ordinal = std::stoi(line.substr(6));
        check(ordinal >= 0 && ordinal < kMusicActionCount && (seen & (1u << ordinal)) == 0, "an action is listed once: " + line);
        if (ordinal >= 0 && ordinal < kMusicActionCount) seen |= 1u << ordinal;
        const std::string g = line.substr(line.rfind('|') + 1);
        check(g == "LIGHT" || g == "MOTION", "an action is light or motion: " + line);
        if (!group.empty() && g != group) ++groupChanges;
        if (group.empty()) check(g == "LIGHT", "the menu opens on the light actions");
        group = g;
    }
    check(seen == (1u << kMusicActionCount) - 1u, "the menu lists every action");
    check(groupChanges == 1, "each group is one run");
}

void abi() {
    const std::string defaults = defaultLfos();
    char big[4096];
    check(geode_mod_lfos_read(nullptr, big, sizeof big) == static_cast<int>(defaults.size()) && defaults == big,
          "no saved waves reads as the defaults");
    check(geode_mod_lfos_read(kSavedLfos, nullptr, 0) > 0, "a call with no buffer still says the length");
    char small[8];
    std::memset(small, 'x', sizeof small);
    check(geode_mod_lfos_read(nullptr, small, sizeof small) == static_cast<int>(defaults.size()) &&
              std::strlen(small) == sizeof small - 1 && defaults.compare(0, sizeof small - 1, small) == 0,
          "a short buffer gets the start, terminated, and the length the whole text needs");
    check(geode_mod_adsrs_read("nonsense", big, sizeof big) == static_cast<int>(defaultAdsrs().size()) && defaultAdsrs() == big,
          "broken envelopes read as the defaults");

    check(geode_mod_reactions_read(kSavedReactions, big, sizeof big) == static_cast<int>(std::strlen(kSavedReactions)) &&
              std::strcmp(big, kSavedReactions) == 0,
          "saved routes read as saved");
    check(geode_mod_reactions_read("[]", big, sizeof big) == 0 && big[0] == '\0', "text that is no route list is Auto");
    check(geode_mod_reactions_read(nullptr, big, sizeof big) == 0, "no saved routes is Auto");

    const int handAll = static_cast<int>(hand::kAll);
    check(geode_mod_hand_load(nullptr, 0, 0) == 0 && geode_mod_hand_load(nullptr, 1, 5) == 0, "no routes saved: the mask is 0");
    check(geode_mod_hand_load(kSavedReactions, 0, 0) == handAll, "routes saved before the mask existed are a whole list of the user's");
    check(geode_mod_hand_load(kSavedReactions, 1, 5) == 5, "routes with a saved mask keep it");
    check(geode_mod_hand_load(kSavedReactions, 1, -1) == handAll, "a damaged saved mask stays inside the mask bits and never reads as a drop");
    check(geode_mod_hand_load("[]", 1, 5) == -1 && geode_mod_hand_load("reactions/1|junk", 0, 0) == -1,
          "routes that no longer read are dropped, whatever mask sat beside them");

    geode_mod_lfo_edit(nullptr, 2, GEODE_MOD_LFO_DEPTH, 5.0f, big, sizeof big);
    float lfo[GEODE_MOD_LFO_VALUES] = {};
    check(geode_mod_lfo_values(big, 2, nullptr, lfo, GEODE_MOD_LFO_VALUES) == GEODE_MOD_LFO_VALUES &&
              lfo[GEODE_MOD_LFO_DEPTH] == 1.0f && lfo[GEODE_MOD_LFO_RATE_SECONDS] == 2.0f,
          "an edit hands back the waves with the field set, inside its range");
    const std::string edited = big;
    check(geode_mod_lfo_edit(edited.c_str(), 9, GEODE_MOD_LFO_DEPTH, 0.5f, big, sizeof big) == static_cast<int>(edited.size()) &&
              edited == big,
          "an edit that names no slot hands back what came in");

    geode_mod_adsr_edit(nullptr, 1, GEODE_MOD_ADSR_TARGET, static_cast<float>(ordinal(LfoTarget::Warp)), big, sizeof big);
    float adsr[GEODE_MOD_ADSR_VALUES + 4] = {};
    check(geode_mod_adsr_values(big, 1, adsr, GEODE_MOD_ADSR_VALUES + 4) == GEODE_MOD_ADSR_VALUES + 1 &&
              adsr[GEODE_MOD_ADSR_VALUES] == static_cast<float>(ordinal(LfoTarget::Warp)),
          "an envelope's target edit adds the target");
    int offered[4] = {};
    check(geode_mod_adsr_targets_offered(big, 1, nullptr, offered, 4) == 4, "an envelope's picker fills a short buffer");
    check(geode_mod_targets_offered(nullptr, GEODE_MOD_PICKER_REACTION, offered, 4) == 4 && offered[0] != 0,
          "the route editor's picker never lists nothing");
    check(std::string(geode_mod_catalog_text()) == modcatalog::text(), "the catalog crosses as it is built");
}

}  // namespace

int main(int argc, char** argv) {
    savedWavesRead();
    olderWavesRead();
    forgivingWaves();
    savedEnvelopesRead();
    forgivingEnvelopes();
    savedReactionsRead();
    waveEdits();
    envelopeEdits();
    reactionEdits();
    values();
    pickers();
    catalog();
    musicRows(argc > 1 ? argv[1] : "core/tests/mod_catalog_music.txt");
    abi();
    if (failures) {
        std::printf("%d modulation document check(s) failed\n", failures);
        return 1;
    }
    std::printf("modulation document checks passed\n");
    return 0;
}
