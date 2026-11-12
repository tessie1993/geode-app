// Host-side checks for a Studio project's documents (core/studio/StudioEdit, geode_studio_*): the kind and the
// edit project.json keeps. Built and run by CI ("Native host tests") with the system compiler; no device.
//
// What is pinned: a saved edit reads back to the same document, in order (clips by start, markers and keys by
// time, one track per parameter and scope), with the defaults an older file leaves out; a newer file, an unknown
// tag or a missing required field is refused so the store quarantines it instead of half-reading it; retired
// style ids open on their replacement; a new project holds its song, and its look in front of it, at the right
// lengths, with fresh ids.
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>

#include "api/geode_studio_api.h"
#include "studio/StudioEdit.hpp"

using namespace geode::studio;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The saved text of an edit read and written once: what the store keeps.
std::string canonical(const std::string& json) {
    const auto edit = readEdit(json);
    return edit ? writeEdit(*edit) : std::string("<unreadable>");
}

std::string withTimeline(const std::string& lanes, const std::string& rest = "") {
    return R"({"version":1,"timeline":{"durationMs":93000,"lanes":)" + lanes + "}" + rest + "}";
}

const char* kFull = R"({
  "version": 1,
  "timeline": {"durationMs": 141000, "lanes": [
    {"id": "v", "kind": "visual", "name": "Looks", "muted": false, "locked": true, "clips": [
      {"id": "c2", "startMs": 5000, "durationMs": 4000, "content": {"type": "scene", "sceneId": "pool",
        "presetId": "p1", "beatCut": {"sceneIds": ["silk", "pool"], "everyBeats": 8}},
        "transition": {"id": "fade", "durationMs": 400, "onBeat": true}},
      {"id": "c1", "startMs": 0, "durationMs": 5000, "sourceInMs": 0, "sourceDurationMs": 0, "label": "intro",
        "enabled": false, "content": {"type": "scene", "sceneId": "silk", "milkPath": "milk/a.milk"}}]},
    {"id": "m", "kind": "media", "clips": [
      {"id": "c3", "startMs": 0, "durationMs": 1000, "sourceInMs": 250, "sourceDurationMs": 9000,
        "content": {"type": "video", "uri": "content://v/1", "edit": {"grade": 1}}},
      {"id": "c4", "startMs": 1000, "durationMs": 1000, "content": {"type": "still", "uri": "content://s/1", "kenBurns": 0.25}}]},
    {"id": "t", "kind": "text", "clips": [
      {"id": "c5", "startMs": 0, "durationMs": 2000, "content": {"type": "text", "text": "Hi \"there\"\n", "styleId": "bold"}}]},
    {"id": "o", "kind": "overlay", "clips": [
      {"id": "c6", "startMs": 0, "durationMs": 2000, "content": {"type": "overlay", "uri": "content://o/1", "blend": "ADD", "opacity": 0.42}}]},
    {"id": "a", "kind": "audio", "name": "Song", "clips": [
      {"id": "c7", "startMs": 0, "durationMs": 141000, "content": {"type": "audio", "uri": "content://a/1", "gainDb": -3.5}}]}]},
  "markers": [
    {"id": "k3", "atMs": 9000, "name": "Drop", "colour": "RED", "note": "big", "origin": {"type": "detected", "confidence": 0.9, "strength": 0.5}},
    {"id": "k1", "atMs": 1000, "origin": {"type": "tapped", "rawAtMs": 1080, "latencyMs": 80}},
    {"id": "k2", "atMs": 1000, "colour": "VIOLET"}],
  "keyframes": [
    {"paramId": "glow", "keys": [
      {"id": "f2", "atMs": 2000, "value": {"kind": "scalar", "value": 0.75}, "interpolation": {"type": "hold"}},
      {"id": "f1", "atMs": 0, "value": {"kind": "scalar", "value": 0.25}, "interpolation": {"type": "ease", "shape": "SMOOTH"}}]},
    {"paramId": "pos", "clipId": "c1", "enabled": false, "keys": [
      {"id": "f3", "atMs": 0, "value": {"kind": "vector2", "x": 0.5, "y": -0.5},
        "interpolation": {"type": "custom", "c1x": 0.42, "c1y": 0, "c2x": 0.58, "c2y": 1}}]},
    {"paramId": "tint", "keys": [
      {"id": "f4", "atMs": 0, "value": {"kind": "colour", "r": 1, "g": 0.5, "b": 0}},
      {"id": "f5", "atMs": 10, "value": {"kind": "toggle", "on": true}, "interpolation": {"type": "linear"}},
      {"id": "f6", "atMs": 20, "value": {"kind": "choice", "index": 3}}]}]
})";

void aSavedEditReadsBackTheSame() {
    const auto edit = readEdit(kFull);
    check(edit.has_value(), "the full edit reads");
    if (!edit) return;
    check(edit->durationMs == 141000, "timeline length");
    check(edit->lanes.size() == 5, "five lanes");
    check(edit->lanes[0].locked && !edit->lanes[0].muted && edit->lanes[0].name == "Looks", "lane flags and name");
    const std::string once = writeEdit(*edit);
    check(canonical(once) == once, "written text reads back to the same text");

    const Lane& looks = edit->lanes[0];
    check(looks.clips[0].id == "c1" && looks.clips[1].id == "c2", "clips are kept in start order");
    const auto* scene = std::get_if<SceneContent>(&looks.clips[1].content);
    check(scene && scene->presetId == std::optional<std::string>("p1") && scene->beatCut &&
              scene->beatCut->everyBeats == 8 && scene->beatCut->sceneIds.size() == 2,
          "a scene clip keeps its preset and beat-cut");
    check(looks.clips[1].transition && looks.clips[1].transition->durationMs == 400 &&
              looks.clips[1].transition->onBeat,
          "a transition keeps its length and beat");
    check(!looks.clips[0].enabled && looks.clips[0].label == "intro", "clip label and enabled");
    const auto* video = std::get_if<VideoContent>(&edit->lanes[1].clips[0].content);
    check(video && video->uri == "content://v/1" && once.find("grade") == std::string::npos,
          "a video clip keeps its file and drops the old grade");
    check(edit->lanes[1].clips[0].sourceInMs == 250 && edit->lanes[1].clips[0].sourceDurationMs == 9000,
          "source in and length");
    const auto* text = std::get_if<TextContent>(&edit->lanes[2].clips[0].content);
    check(text && text->text == "Hi \"there\"\n" && text->styleId == std::optional<std::string>("bold"),
          "text keeps quotes and newlines");
    const auto* overlay = std::get_if<OverlayContent>(&edit->lanes[3].clips[0].content);
    check(overlay && overlay->blend == Blend::Add && overlay->opacity == 0.42f, "overlay blend and opacity");
    const auto* audio = std::get_if<AudioContent>(&edit->lanes[4].clips[0].content);
    check(audio && audio->gainDb == -3.5f, "audio gain");

    check(edit->markers.size() == 3 && edit->markers[0].id == "k1" && edit->markers[1].id == "k2" &&
              edit->markers[2].id == "k3",
          "markers in time order, ties in the order saved");
    const auto* tapped = std::get_if<TappedOrigin>(&edit->markers[0].origin);
    check(tapped && tapped->rawAtMs == 1080 && tapped->latencyMs == 80, "a tapped marker keeps its raw time");
    check(std::holds_alternative<ManualOrigin>(edit->markers[1].origin) &&
              edit->markers[1].colour == MarkerColour::Violet,
          "no origin is manual");
    const auto* detected = std::get_if<DetectedOrigin>(&edit->markers[2].origin);
    check(detected && detected->confidence == 0.9f && edit->markers[2].colour == MarkerColour::Red &&
              edit->markers[2].note == "big",
          "a detected marker");

    check(edit->tracks.size() == 3, "three tracks");
    const Track& glow = edit->tracks[0];
    check(glow.keys[0].id == "f1" && glow.keys[1].id == "f2", "keys in time order");
    const auto* eased = std::get_if<Eased>(&glow.keys[0].interpolation);
    check(eased && eased->shape == EaseShape::Smooth, "an eased key keeps its shape");
    check(std::holds_alternative<Hold>(glow.keys[1].interpolation), "a hold key");
    const Track& pos = edit->tracks[1];
    check(pos.clipId == std::optional<std::string>("c1") && !pos.enabled, "a clip track keeps its scope");
    const auto* bezier = std::get_if<Bezier>(&pos.keys[0].interpolation);
    check(bezier && bezier->c1x == 0.42f && bezier->c2y == 1.0f, "a custom curve keeps its handles");
    const auto* colour = std::get_if<Colour>(&edit->tracks[2].keys[0].value);
    check(colour && colour->a == 1.0f && colour->g == 0.5f, "a colour without alpha is opaque");
    check(std::holds_alternative<Linear>(edit->tracks[2].keys[0].interpolation), "no interpolation is linear");
    const auto* toggle = std::get_if<Toggle>(&edit->tracks[2].keys[1].value);
    const auto* choice = std::get_if<Choice>(&edit->tracks[2].keys[2].value);
    check(toggle && toggle->on && choice && choice->index == 3, "toggle and choice keys");
}

void floatsSurviveTheTrip() {
    const auto edit = readEdit(canonical(kFull));
    check(edit.has_value(), "the canonical text reads");
    if (!edit) return;
    const auto* overlay = std::get_if<OverlayContent>(&edit->lanes[3].clips[0].content);
    check(overlay && overlay->opacity == 0.42f, "0.42 is the same float after a save");
    const auto* bezier = std::get_if<Bezier>(&edit->tracks[1].keys[0].interpolation);
    check(bezier && bezier->c2x == 0.58f, "0.58 is the same float after a save");
}

void olderFilesTakeTheDefaults() {
    const auto edit = readEdit(withTimeline(
        R"([{"id":"l","kind":"overlay","clips":[{"id":"c","startMs":0,"durationMs":77,
            "transition":{"id":"cut"},
            "content":{"type":"overlay","uri":"u","blend":"DODGE"}}]},
           {"id":"s","kind":"visual","clips":[{"id":"d","startMs":0,"durationMs":77,
            "content":{"type":"scene","sceneId":"silk","beatCut":{"everyBeats":0}}}]}])",
        R"(,"markers":[{"id":"m","atMs":5,"colour":"PINK","origin":{}}],
           "keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"scalar","value":1},
             "interpolation":{"type":"ease","shape":"BOUNCE"}}]}])"));
    check(edit.has_value(), "an edit with gaps reads");
    if (!edit) return;
    const Clip& c = edit->lanes[0].clips[0];
    check(c.transition && c.transition->durationMs == kTransitionMs && !c.transition->onBeat,
          "a transition without a length takes the default");
    check(c.enabled && c.label.empty() && c.sourceDurationMs == 0, "clip defaults");
    const auto* overlay = std::get_if<OverlayContent>(&c.content);
    check(overlay && overlay->blend == Blend::Screen && overlay->opacity == 1.0f, "an unknown blend is screen");
    const auto* scene = std::get_if<SceneContent>(&edit->lanes[1].clips[0].content);
    check(scene && scene->beatCut && scene->beatCut->everyBeats == 1 && scene->beatCut->sceneIds.empty(),
          "a beat-cut steps at least every beat");
    check(edit->markers[0].colour == MarkerColour::Cyan && std::holds_alternative<ManualOrigin>(edit->markers[0].origin),
          "an unknown colour is cyan, an untyped origin manual");
    const auto* eased = std::get_if<Eased>(&edit->tracks[0].keys[0].interpolation);
    check(eased && eased->shape == EaseShape::InOut, "an unknown ease is in-out");
    check(edit->tracks[0].enabled && !edit->tracks[0].clipId, "track defaults");

    const auto bare = readEdit(R"({"timeline":{}})");
    check(bare && bare->lanes.empty() && bare->markers.empty() && bare->tracks.empty() && bare->durationMs == 0,
          "an edit without a version, lanes, markers or keys is empty");
}

void textFieldsReadLikeTheOldFiles() {
    const auto edit = readEdit(withTimeline(
        R"([{"id":7,"kind":"audio","muted":"TRUE","clips":[{"id":"c","startMs":"1500","durationMs":174000.0,
            "enabled":"false","label":null,"content":{"type":"audio","uri":"u","gainDb":"-1.5"}}]}])"));
    check(edit.has_value(), "numbers and booleans written as text read");
    if (!edit) return;
    const Lane& lane = edit->lanes[0];
    check(lane.id == "7" && lane.muted, "a numeric id reads as text, \"TRUE\" as true");
    check(lane.clips[0].startMs == 1500 && lane.clips[0].durationMs == 174000 && !lane.clips[0].enabled,
          "times written as text or with a fraction read as whole milliseconds");
    check(lane.clips[0].label == "null", "a null label reads as the text null, as it always did");
    const auto* audio = std::get_if<AudioContent>(&lane.clips[0].content);
    check(audio && audio->gainDb == -1.5f, "a gain written as text");
}

void tracksAreOnePerParameterAndScope() {
    const auto edit = readEdit(withTimeline("[]", R"(,"keyframes":[
        {"paramId":"glow","keys":[{"id":"a","atMs":0,"value":{"kind":"scalar","value":1}}]},
        {"paramId":"glow","clipId":"c","keys":[]},
        {"paramId":"heat","keys":[]},
        {"paramId":"glow","keys":[{"id":"b","atMs":0,"value":{"kind":"scalar","value":2}}]}])"));
    check(edit.has_value(), "tracks read");
    if (!edit) return;
    check(edit->tracks.size() == 3, "a repeated parameter and scope is one track");
    check(edit->tracks[0].paramId == "glow" && edit->tracks[0].clipId == std::optional<std::string>("c"),
          "a clip-scoped track is its own track");
    check(edit->tracks[1].paramId == "heat", "order follows the last time each track was read");
    check(edit->tracks[2].paramId == "glow" && !edit->tracks[2].clipId && edit->tracks[2].keys[0].id == "b",
          "the later track wins");
}

void retiredStylesOpenOnTheirReplacement() {
    const auto edit = readEdit(withTimeline(
        R"([{"id":"v","kind":"visual","clips":[{"id":"c","startMs":0,"durationMs":93,
            "content":{"type":"scene","sceneId":"water"}}]}])"));
    const auto* scene = edit ? std::get_if<SceneContent>(&edit->lanes[0].clips[0].content) : nullptr;
    check(scene && scene->sceneId == "pool", "water opens on pool");
}

void unreadableEditsAreRefused() {
    const char* refused[] = {
        "",
        "not json",
        "[]",
        R"({"version":2,"timeline":{}})",
        R"({"version":1})",
        R"({"timeline":5})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"sideways"}]}})",
        R"({"timeline":{"lanes":[{"kind":"audio"}]}})",
        R"({"timeline":{"lanes":[5]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","durationMs":1,"content":{"type":"audio","uri":"u"}}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","startMs":0,"durationMs":1}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","startMs":0,"durationMs":1,"content":{"type":"hologram"}}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","startMs":0,"durationMs":1,"content":{"type":"text"}}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"visual","clips":[{"id":"c","startMs":0,"durationMs":1,"content":{"type":"scene","sceneId":"silk","beatCut":{"sceneIds":[{}]}}}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","startMs":"soon","durationMs":1,"content":{"type":"audio","uri":"u"}}]}]}})",
        R"({"timeline":{"lanes":[{"id":"l","kind":"audio","clips":[{"id":"c","startMs":0,"durationMs":1,"transition":{},"content":{"type":"audio","uri":"u"}}]}]}})",
        R"({"timeline":{},"markers":[{"id":"m","atMs":0,"origin":{"type":"guessed"}}]})",
        R"({"timeline":{},"markers":[{"id":"m","atMs":0,"origin":{"type":"tapped","rawAtMs":3}}]})",
        R"({"timeline":{},"markers":[{"id":"m"}]})",
        R"({"timeline":{},"keyframes":[{"keys":[]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"quaternion"}}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"scalar"}}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"toggle","on":"maybe"}}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"scalar","value":1e300}}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"scalar","value":1},"interpolation":{"type":"bounce"}}]}]})",
        R"({"timeline":{},"keyframes":[{"paramId":"p","keys":[{"id":"k","atMs":0,"value":{"kind":"scalar","value":1},"interpolation":{"type":"custom","c1x":0,"c1y":0,"c2x":1}}]}]})",
    };
    int index = 0;
    for (const char* text : refused) {
        if (readEdit(text)) std::printf("  refused case %d read: %s\n", index, text);
        check(!readEdit(text), "an unreadable edit is refused");
        ++index;
    }
}

void textIsEscapedAndKeptAsUtf8() {
    Edit edit;
    Marker m;
    m.id = "m";
    m.name = "Drop \xF0\x9F\x94\xA5 \"now\"\\\x01";
    edit.markers.push_back(m);
    const std::string text = writeEdit(edit);
    check(text.find("\\\"now\\\"") != std::string::npos && text.find("\\u0001") != std::string::npos,
          "quotes and control characters are escaped");
    check(text.find("\xF0\x9F\x94\xA5") != std::string::npos, "an emoji stays as UTF-8");
    const auto back = readEdit(text);
    check(back && back->markers[0].name == m.name, "the name reads back the same");
    const auto escaped = readEdit(R"({"timeline":{},"markers":[{"id":"m","atMs":0,"name":"🔥"}]})");
    check(escaped && escaped->markers[0].name == "\xF0\x9F\x94\xA5", "an escaped emoji reads as UTF-8");
}

void kindsReadAndWrite() {
    check(writeKind(Kind{}) == R"({"type":"standard"})", "the standard kind's text");
    const auto loop = readKind(R"({"type":"loop"})");
    check(loop && loop->loop && loop->targetLengthMs == 3'600'000 && loop->loopLengthMs == 20'000,
          "a loop without lengths takes an hour of a 20 s loop");
    const auto set = readKind(R"({"type":"loop","targetLengthMs":7200000,"loopLengthMs":17400})");
    check(set && writeKind(*set) == R"({"type":"loop","targetLengthMs":7200000,"loopLengthMs":17400})",
          "a loop keeps its lengths");
    check(!readKind(R"({"type":"spiral"})") && !readKind("{}") && !readKind("nope"), "an unknown kind is refused");
}

IdSource counter() {
    auto next = std::make_shared<int>(0);
    return [next] { return "id" + std::to_string(++*next); };
}

void newProjectsHoldTheirSong() {
    const Edit empty = newEdit(std::nullopt, std::nullopt, counter());
    check(empty.lanes.empty() && empty.durationMs == 0, "a recording without a song has an empty timeline");

    const Edit song = newEdit(Song{"content://a/1", "Night", 174'000}, std::nullopt, counter());
    check(song.lanes.size() == 1 && song.lanes[0].kind == LaneKind::Audio && song.lanes[0].name == "Night" &&
              song.durationMs == 174'000,
          "the song's audio lane, named after it, sets the length");
    const Clip& clip = song.lanes[0].clips.at(0);
    const auto* audio = std::get_if<AudioContent>(&clip.content);
    check(audio && audio->uri == "content://a/1" && clip.startMs == 0 && clip.durationMs == 174'000 &&
              clip.sourceDurationMs == 174'000 && clip.label == "Night",
          "the song is one clip over its whole length");
    check(song.lanes[0].id == "id1" && clip.id == "id2", "ids come from the id source");

    const Edit unknown = newEdit(Song{"u", "", 0}, std::nullopt, counter());
    check(unknown.lanes.size() == 1 && unknown.lanes[0].clips.empty(), "a song of unknown length gets an empty lane");
    const Edit sliver = newEdit(Song{"u", "", kMinClipMs - 1}, std::nullopt, counter());
    check(sliver.lanes[0].clips.empty(), "a song shorter than the shortest clip gets no clip");
    const Edit shortest = newEdit(Song{"u", "", kMinClipMs}, std::nullopt, counter());
    check(shortest.lanes[0].clips.size() == 1, "a song exactly the shortest clip long gets one");

    const Edit look = newEdit(Song{"u", "t", 93'000}, Look{"pool", std::string("milk/x.milk"), 141'000}, counter());
    check(look.lanes.size() == 2 && look.lanes[0].kind == LaneKind::Visual && look.lanes[1].kind == LaneKind::Audio,
          "the look's lane is in front of the song's");
    const auto* scene = std::get_if<SceneContent>(&look.lanes[0].clips.at(0).content);
    check(scene && scene->sceneId == "pool" && scene->milkPath == std::optional<std::string>("milk/x.milk") &&
              !scene->presetId && !scene->beatCut,
          "the look clip draws the style on stage");
    check(look.lanes[0].clips[0].durationMs == 141'000 && look.durationMs == 141'000 &&
              look.lanes[0].clips[0].sourceDurationMs == 0,
          "a render longer than the song stretches the look and the timeline");
    const Edit longer = newEdit(Song{"u", "t", 141'000}, Look{"silk", std::nullopt, 77'000}, counter());
    check(longer.lanes[0].clips[0].durationMs == 141'000 && longer.durationMs == 141'000,
          "a song longer than the render sets the look's length");
    const Edit none = newEdit(Song{"u", "t", 0}, Look{"silk", std::nullopt, 0}, counter());
    check(none.lanes.size() == 2 && none.lanes[0].clips.empty(), "a look of no length is an empty visual lane");

    const auto read = readEdit(writeEdit(look));
    check(read && writeEdit(*read) == writeEdit(look), "a new project's edit saves and reads back");
}

void randomIdsAreUuids() {
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        const std::string id = randomId();
        const bool shaped = id.size() == 36 && id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-' &&
                            id[14] == '4' && std::strchr("89ab", id[19]) != nullptr;
        check(shaped, "a random id is a version 4 UUID");
        seen.insert(id);
    }
    check(seen.size() == 64, "random ids do not repeat");
}

std::string textOf(geode_studio_text* made) {
    const std::unique_ptr<geode_studio_text, decltype(&geode_studio_text_destroy)> text(made,
                                                                                       &geode_studio_text_destroy);
    return text ? std::string(geode_studio_text_get(text.get())) : std::string("<null>");
}

void cAbi() {
    check(textOf(geode_studio_edit_read(nullptr)) ==
              R"({"version":1,"timeline":{"durationMs":0,"lanes":[]},"markers":[],"keyframes":[]})",
          "no edit is the empty edit");
    check(textOf(geode_studio_edit_read("{broken")) == "<null>", "a broken edit is null");
    check(textOf(geode_studio_edit_read(kFull)) == canonical(kFull), "the C ABI reads like the C++ one");
    check(textOf(geode_studio_kind_read(nullptr)) == R"({"type":"standard"})", "no kind is standard");
    check(textOf(geode_studio_kind_read(R"({"type":"x"})")) == "<null>", "an unknown kind is null");
    check(geode_studio_min_clip_ms() == 33, "the shortest clip is 33 ms");
    check(std::strcmp(geode_studio_text_get(nullptr), "") == 0, "no text reads as empty");
    geode_studio_text_destroy(nullptr);

    const auto made = readEdit(textOf(geode_studio_edit_new("content://a/2", "Song", 77'000, "silk", nullptr, 93'000)));
    check(made && made->lanes.size() == 2 && made->durationMs == 93'000, "a new edit over a song with a look");
    check(made && made->lanes[0].id != made->lanes[1].id && made->lanes[0].id.size() == 36, "fresh UUIDs");
    const auto bare = readEdit(textOf(geode_studio_edit_new(nullptr, nullptr, 0, nullptr, nullptr, 141'000)));
    check(bare && bare->lanes.empty(), "a new edit without a song or a look is empty");
}

}  // namespace

int main() {
    aSavedEditReadsBackTheSame();
    floatsSurviveTheTrip();
    olderFilesTakeTheDefaults();
    textFieldsReadLikeTheOldFiles();
    tracksAreOnePerParameterAndScope();
    retiredStylesOpenOnTheirReplacement();
    unreadableEditsAreRefused();
    textIsEscapedAndKeptAsUtf8();
    kindsReadAndWrite();
    newProjectsHoldTheirSong();
    randomIdsAreUuids();
    cAbi();
    if (failures == 0) std::printf("studio_edit_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
