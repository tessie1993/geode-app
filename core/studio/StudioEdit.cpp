#include "studio/StudioEdit.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <utility>

#include "util/Json.hpp"
#include "viz/Catalogue.hpp"

namespace geode::studio {

namespace {

using json::Value;

constexpr std::array<std::string_view, 5> kLaneKinds = {"visual", "media", "text", "overlay", "audio"};
constexpr std::array<std::string_view, 4> kBlends = {"NORMAL", "ADD", "SCREEN", "MULTIPLY"};
constexpr std::array<std::string_view, 7> kColours = {"RED", "AMBER", "LIME", "CYAN", "BLUE", "VIOLET", "WHITE"};
constexpr std::array<std::string_view, 4> kEases = {"IN", "OUT", "IN_OUT", "SMOOTH"};

template <typename E, size_t N>
std::optional<E> named(const std::array<std::string_view, N>& names, std::string_view name) {
    for (size_t i = 0; i < N; ++i) {
        if (names[i] == name) return static_cast<E>(i);
    }
    return std::nullopt;
}

template <typename E, size_t N>
std::string_view nameOf(const std::array<std::string_view, N>& names, E value) {
    return names[static_cast<size_t>(value)];
}

// Whole text as a number, the way a saved field written as text still reads as one.
std::optional<double> numberIn(const std::string& text) {
    if (text.empty()) return std::nullopt;
    const char* start = text.c_str();
    char* end = nullptr;
    const double d = std::strtod(start, &end);
    if (end != start + text.size()) return std::nullopt;
    return d;
}

std::optional<double> numberOf(const Value* v) {
    if (!v) return std::nullopt;
    if (v->kind() == Value::Kind::Number) return v->number();
    if (v->kind() == Value::Kind::String) return numberIn(v->string());
    return std::nullopt;
}

std::optional<long long> longOf(const Value* v) {
    const auto d = numberOf(v);
    if (!d || !std::isfinite(*d) || std::fabs(*d) >= 9.2e18) return std::nullopt;
    return static_cast<long long>(*d);
}

std::optional<bool> boolOf(const Value* v) {
    if (!v) return std::nullopt;
    if (v->kind() == Value::Kind::Bool) return v->boolValue();
    if (v->kind() != Value::Kind::String) return std::nullopt;
    std::string lower = v->string();
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    if (lower == "true") return true;
    if (lower == "false") return false;
    return std::nullopt;
}

// A field read as text: a string as it is, a boolean or a whole number as it prints, JSON null as "null".
std::optional<std::string> stringOf(const Value* v) {
    if (!v) return std::nullopt;
    switch (v->kind()) {
        case Value::Kind::String: return v->string();
        case Value::Kind::Null: return std::string("null");
        case Value::Kind::Bool: return std::string(v->boolValue() ? "true" : "false");
        case Value::Kind::Number: {
            const double d = v->number();
            if (!std::isfinite(d) || d != std::trunc(d) || std::fabs(d) >= 9.2e18) return std::nullopt;
            return std::to_string(static_cast<long long>(d));
        }
        case Value::Kind::Array:
        case Value::Kind::Object: return std::nullopt;
    }
    return std::nullopt;
}

// Reads fields off JSON objects and remembers the first failure, so a reader stays a straight run of fields and
// says once at the end whether the document was readable.
class Reader {
public:
    bool ok() const { return ok_; }
    void fail() { ok_ = false; }

    std::string str(const Value& o, const char* key) {
        auto s = stringOf(o.get(key));
        if (!s) fail();
        return s.value_or(std::string());
    }

    std::string optStr(const Value& o, const char* key, const char* fallback) {
        return stringOf(o.get(key)).value_or(std::string(fallback));
    }

    std::optional<std::string> strOrNull(const Value& o, const char* key) {
        const Value* v = o.get(key);
        if (!v || v->isNull()) return std::nullopt;
        auto s = stringOf(v);
        if (!s) fail();
        return s;
    }

    long long lng(const Value& o, const char* key) {
        const auto n = longOf(o.get(key));
        if (!n) fail();
        return n.value_or(0);
    }

    long long optLng(const Value& o, const char* key, long long fallback) {
        return longOf(o.get(key)).value_or(fallback);
    }

    int optInt(const Value& o, const char* key, int fallback) {
        const auto n = longOf(o.get(key));
        if (!n) return fallback;
        return static_cast<int>(std::clamp<long long>(*n, INT_MIN, INT_MAX));
    }

    // A float, refused when it does not fit one: JSON could not carry it back out.
    float real(const Value& o, const char* key) { return toFloat(numberOf(o.get(key))); }

    float optReal(const Value& o, const char* key, double fallback) {
        const auto d = numberOf(o.get(key));
        return toFloat(d ? d : std::optional<double>(fallback));
    }

    bool flag(const Value& o, const char* key) {
        const auto b = boolOf(o.get(key));
        if (!b) fail();
        return b.value_or(false);
    }

    bool optFlag(const Value& o, const char* key, bool fallback) { return boolOf(o.get(key)).value_or(fallback); }

    const Value* obj(const Value& o, const char* key) {
        const Value* v = optObj(o, key);
        if (!v) fail();
        return v;
    }

    static const Value* optObj(const Value& o, const char* key) {
        const Value* v = o.get(key);
        return v && v->kind() == Value::Kind::Object ? v : nullptr;
    }

    // Every element of the array at key read with `read`; a missing array is empty, an element that is no
    // object fails the document.
    template <typename T, typename Read>
    std::vector<T> items(const Value& o, const char* key, Read read) {
        std::vector<T> out;
        const Value* v = o.get(key);
        if (!v || v->kind() != Value::Kind::Array) return out;
        out.reserve(v->array().size());
        for (const Value& item : v->array()) {
            if (item.kind() != Value::Kind::Object) {
                fail();
                return out;
            }
            out.push_back(read(item));
        }
        return out;
    }

private:
    float toFloat(std::optional<double> d) {
        if (!d) {
            fail();
            return 0.0f;
        }
        const auto f = static_cast<float>(*d);
        if (!std::isfinite(f)) fail();
        return f;
    }

    bool ok_ = true;
};

BeatCut readBeatCut(Reader& r, const Value& o) {
    BeatCut b;
    const Value* ids = o.get("sceneIds");
    if (ids && ids->kind() == Value::Kind::Array) {
        for (const Value& id : ids->array()) {
            auto s = stringOf(&id);
            if (!s) r.fail();
            b.sceneIds.push_back(s.value_or(std::string()));
        }
    }
    b.everyBeats = std::max(1, r.optInt(o, "everyBeats", kBeatsPerCut));
    return b;
}

Content readContent(Reader& r, const Value& o) {
    const std::string type = r.str(o, "type");
    if (type == "scene") {
        SceneContent c;
        c.sceneId = viz::catalogue::resolveId(r.str(o, "sceneId"));
        c.presetId = r.strOrNull(o, "presetId");
        c.milkPath = r.strOrNull(o, "milkPath");
        if (const Value* b = Reader::optObj(o, "beatCut")) c.beatCut = readBeatCut(r, *b);
        return c;
    }
    // A project the dropped video editor saved may still carry the clip's grade under "edit": nothing renders a
    // video clip any more, so it is not read.
    if (type == "video") return VideoContent{r.str(o, "uri")};
    if (type == "still") return StillContent{r.str(o, "uri"), r.optReal(o, "kenBurns", 0.0)};
    if (type == "text") return TextContent{r.str(o, "text"), r.strOrNull(o, "styleId")};
    if (type == "overlay") {
        OverlayContent c;
        c.uri = r.str(o, "uri");
        c.blend = named<Blend>(kBlends, r.optStr(o, "blend", "")).value_or(Blend::Screen);
        c.opacity = r.optReal(o, "opacity", 1.0);
        return c;
    }
    if (type == "audio") return AudioContent{r.str(o, "uri"), r.optReal(o, "gainDb", 0.0)};
    r.fail();
    return VideoContent{};
}

Clip readClip(Reader& r, const Value& o) {
    Clip c;
    c.id = r.str(o, "id");
    if (const Value* content = r.obj(o, "content")) c.content = readContent(r, *content);
    c.startMs = r.lng(o, "startMs");
    c.durationMs = r.lng(o, "durationMs");
    c.sourceInMs = r.optLng(o, "sourceInMs", 0);
    c.sourceDurationMs = r.optLng(o, "sourceDurationMs", 0);
    c.label = r.optStr(o, "label", "");
    c.enabled = r.optFlag(o, "enabled", true);
    if (const Value* t = Reader::optObj(o, "transition")) {
        c.transition = Transition{r.str(*t, "id"), r.optLng(*t, "durationMs", kTransitionMs), r.optFlag(*t, "onBeat", false)};
    }
    return c;
}

Lane readLane(Reader& r, const Value& o) {
    Lane l;
    l.id = r.str(o, "id");
    const auto kind = named<LaneKind>(kLaneKinds, r.str(o, "kind"));
    if (!kind) r.fail();
    l.kind = kind.value_or(LaneKind::Visual);
    l.name = r.optStr(o, "name", "");
    l.muted = r.optFlag(o, "muted", false);
    l.locked = r.optFlag(o, "locked", false);
    l.clips = r.items<Clip>(o, "clips", [&r](const Value& c) { return readClip(r, c); });
    std::stable_sort(l.clips.begin(), l.clips.end(), [](const Clip& a, const Clip& b) { return a.startMs < b.startMs; });
    return l;
}

MarkerOrigin readOrigin(Reader& r, const Value& o) {
    const std::string type = r.optStr(o, "type", "manual");
    if (type == "manual") return ManualOrigin{};
    if (type == "tapped") return TappedOrigin{r.lng(o, "rawAtMs"), r.lng(o, "latencyMs")};
    if (type == "detected") return DetectedOrigin{r.optReal(o, "confidence", 0.0), r.optReal(o, "strength", 0.0)};
    r.fail();
    return ManualOrigin{};
}

Marker readMarker(Reader& r, const Value& o) {
    Marker m;
    m.id = r.str(o, "id");
    m.atMs = r.lng(o, "atMs");
    m.name = r.optStr(o, "name", "");
    m.colour = named<MarkerColour>(kColours, r.optStr(o, "colour", "")).value_or(MarkerColour::Cyan);
    m.note = r.optStr(o, "note", "");
    if (const Value* origin = Reader::optObj(o, "origin")) m.origin = readOrigin(r, *origin);
    return m;
}

KeyValue readValue(Reader& r, const Value& o) {
    const std::string kind = r.str(o, "kind");
    if (kind == "scalar") return Scalar{r.real(o, "value")};
    if (kind == "vector2") return Vector2{r.real(o, "x"), r.real(o, "y")};
    if (kind == "colour") return Colour{r.real(o, "r"), r.real(o, "g"), r.real(o, "b"), r.optReal(o, "a", 1.0)};
    if (kind == "toggle") return Toggle{r.flag(o, "on")};
    if (kind == "choice") {
        const long long index = r.lng(o, "index");
        if (index < INT_MIN || index > INT_MAX) r.fail();
        return Choice{static_cast<int>(std::clamp<long long>(index, INT_MIN, INT_MAX))};
    }
    r.fail();
    return Scalar{};
}

Interpolation readInterpolation(Reader& r, const Value& o) {
    const std::string type = r.str(o, "type");
    if (type == "hold") return Hold{};
    if (type == "linear") return Linear{};
    if (type == "ease") return Eased{named<EaseShape>(kEases, r.optStr(o, "shape", "")).value_or(EaseShape::InOut)};
    if (type == "custom") return Bezier{r.real(o, "c1x"), r.real(o, "c1y"), r.real(o, "c2x"), r.real(o, "c2y")};
    r.fail();
    return Linear{};
}

Keyframe readKey(Reader& r, const Value& o) {
    Keyframe k;
    k.id = r.str(o, "id");
    k.atMs = r.lng(o, "atMs");
    if (const Value* v = r.obj(o, "value")) k.value = readValue(r, *v);
    if (const Value* i = Reader::optObj(o, "interpolation")) k.interpolation = readInterpolation(r, *i);
    return k;
}

Track readTrack(Reader& r, const Value& o) {
    Track t;
    t.paramId = r.str(o, "paramId");
    t.clipId = r.strOrNull(o, "clipId");
    t.enabled = r.optFlag(o, "enabled", true);
    t.keys = r.items<Keyframe>(o, "keys", [&r](const Value& k) { return readKey(r, k); });
    std::stable_sort(t.keys.begin(), t.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.atMs < b.atMs; });
    return t;
}

// One track per parameter and scope: a later one replaces an earlier one and takes its place at the end.
std::vector<Track> oneTrackEach(std::vector<Track> read) {
    std::vector<Track> out;
    out.reserve(read.size());
    for (Track& t : read) {
        std::erase_if(out, [&t](const Track& kept) { return kept.paramId == t.paramId && kept.clipId == t.clipId; });
        out.push_back(std::move(t));
    }
    return out;
}

// Compact JSON. Text goes out as UTF-8 with only the quote, the backslash and control characters escaped.
class Writer {
public:
    void open(char c) {
        separate();
        out_ += c;
        fresh_ = true;
    }
    void close(char c) {
        out_ += c;
        fresh_ = false;
    }
    void key(std::string_view k) {
        separate();
        quote(k);
        out_ += ':';
        fresh_ = true;
    }
    void string(std::string_view v) {
        separate();
        quote(v);
    }
    void text(std::string_view k, std::string_view v) {
        key(k);
        string(v);
    }
    void optText(std::string_view k, const std::optional<std::string>& v) {
        if (v) text(k, *v);
    }
    void whole(std::string_view k, long long v) {
        key(k);
        separate();
        out_ += std::to_string(v);
    }
    // Nine significant digits give back the same float when read.
    void real(std::string_view k, float v) {
        key(k);
        separate();
        std::array<char, 32> buf{};
        std::snprintf(buf.data(), buf.size(), "%.9g", static_cast<double>(std::isfinite(v) ? v : 0.0f));
        out_ += buf.data();
    }
    void flag(std::string_view k, bool v) {
        key(k);
        separate();
        out_ += v ? "true" : "false";
    }
    std::string take() { return std::move(out_); }

private:
    void separate() {
        if (!fresh_) out_ += ',';
        fresh_ = false;
    }
    void quote(std::string_view s) {
        out_ += '"';
        for (const char c : s) {
            const auto u = static_cast<unsigned char>(c);
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                case '\b': out_ += "\\b"; break;
                case '\f': out_ += "\\f"; break;
                default:
                    if (u < 0x20) {
                        std::array<char, 16> esc{};
                        std::snprintf(esc.data(), esc.size(), "\\u%04x", static_cast<unsigned>(u));
                        out_ += esc.data();
                    } else {
                        out_ += c;
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    bool fresh_ = true;
};

void writeContent(Writer& w, const Content& content) {
    w.key("content");
    w.open('{');
    if (const auto* c = std::get_if<SceneContent>(&content)) {
        w.text("type", "scene");
        w.text("sceneId", c->sceneId);
        w.optText("presetId", c->presetId);
        w.optText("milkPath", c->milkPath);
        if (c->beatCut) {
            w.key("beatCut");
            w.open('{');
            w.key("sceneIds");
            w.open('[');
            for (const std::string& id : c->beatCut->sceneIds) w.string(id);
            w.close(']');
            w.whole("everyBeats", c->beatCut->everyBeats);
            w.close('}');
        }
    } else if (const auto* v = std::get_if<VideoContent>(&content)) {
        w.text("type", "video");
        w.text("uri", v->uri);
    } else if (const auto* s = std::get_if<StillContent>(&content)) {
        w.text("type", "still");
        w.text("uri", s->uri);
        w.real("kenBurns", s->kenBurns);
    } else if (const auto* t = std::get_if<TextContent>(&content)) {
        w.text("type", "text");
        w.text("text", t->text);
        w.optText("styleId", t->styleId);
    } else if (const auto* o = std::get_if<OverlayContent>(&content)) {
        w.text("type", "overlay");
        w.text("uri", o->uri);
        w.text("blend", nameOf(kBlends, o->blend));
        w.real("opacity", o->opacity);
    } else if (const auto* a = std::get_if<AudioContent>(&content)) {
        w.text("type", "audio");
        w.text("uri", a->uri);
        w.real("gainDb", a->gainDb);
    }
    w.close('}');
}

void writeClip(Writer& w, const Clip& c) {
    w.open('{');
    w.text("id", c.id);
    w.whole("startMs", c.startMs);
    w.whole("durationMs", c.durationMs);
    w.whole("sourceInMs", c.sourceInMs);
    w.whole("sourceDurationMs", c.sourceDurationMs);
    w.text("label", c.label);
    w.flag("enabled", c.enabled);
    if (c.transition) {
        w.key("transition");
        w.open('{');
        w.text("id", c.transition->id);
        w.whole("durationMs", c.transition->durationMs);
        w.flag("onBeat", c.transition->onBeat);
        w.close('}');
    }
    writeContent(w, c.content);
    w.close('}');
}

void writeLane(Writer& w, const Lane& l) {
    w.open('{');
    w.text("id", l.id);
    w.text("kind", nameOf(kLaneKinds, l.kind));
    w.text("name", l.name);
    w.flag("muted", l.muted);
    w.flag("locked", l.locked);
    w.key("clips");
    w.open('[');
    for (const Clip& c : l.clips) writeClip(w, c);
    w.close(']');
    w.close('}');
}

void writeMarker(Writer& w, const Marker& m) {
    w.open('{');
    w.text("id", m.id);
    w.whole("atMs", m.atMs);
    w.text("name", m.name);
    w.text("colour", nameOf(kColours, m.colour));
    w.text("note", m.note);
    w.key("origin");
    w.open('{');
    if (const auto* t = std::get_if<TappedOrigin>(&m.origin)) {
        w.text("type", "tapped");
        w.whole("rawAtMs", t->rawAtMs);
        w.whole("latencyMs", t->latencyMs);
    } else if (const auto* d = std::get_if<DetectedOrigin>(&m.origin)) {
        w.text("type", "detected");
        w.real("confidence", d->confidence);
        w.real("strength", d->strength);
    } else {
        w.text("type", "manual");
    }
    w.close('}');
    w.close('}');
}

void writeValue(Writer& w, const KeyValue& value) {
    w.key("value");
    w.open('{');
    if (const auto* s = std::get_if<Scalar>(&value)) {
        w.text("kind", "scalar");
        w.real("value", s->value);
    } else if (const auto* v = std::get_if<Vector2>(&value)) {
        w.text("kind", "vector2");
        w.real("x", v->x);
        w.real("y", v->y);
    } else if (const auto* c = std::get_if<Colour>(&value)) {
        w.text("kind", "colour");
        w.real("r", c->r);
        w.real("g", c->g);
        w.real("b", c->b);
        w.real("a", c->a);
    } else if (const auto* t = std::get_if<Toggle>(&value)) {
        w.text("kind", "toggle");
        w.flag("on", t->on);
    } else if (const auto* ch = std::get_if<Choice>(&value)) {
        w.text("kind", "choice");
        w.whole("index", ch->index);
    }
    w.close('}');
}

void writeInterpolation(Writer& w, const Interpolation& interpolation) {
    w.key("interpolation");
    w.open('{');
    if (std::holds_alternative<Hold>(interpolation)) {
        w.text("type", "hold");
    } else if (const auto* e = std::get_if<Eased>(&interpolation)) {
        w.text("type", "ease");
        w.text("shape", nameOf(kEases, e->shape));
    } else if (const auto* b = std::get_if<Bezier>(&interpolation)) {
        w.text("type", "custom");
        w.real("c1x", b->c1x);
        w.real("c1y", b->c1y);
        w.real("c2x", b->c2x);
        w.real("c2y", b->c2y);
    } else {
        w.text("type", "linear");
    }
    w.close('}');
}

void writeTrack(Writer& w, const Track& t) {
    w.open('{');
    w.text("paramId", t.paramId);
    w.optText("clipId", t.clipId);
    w.flag("enabled", t.enabled);
    w.key("keys");
    w.open('[');
    for (const Keyframe& k : t.keys) {
        w.open('{');
        w.text("id", k.id);
        w.whole("atMs", k.atMs);
        writeValue(w, k.value);
        writeInterpolation(w, k.interpolation);
        w.close('}');
    }
    w.close(']');
    w.close('}');
}

}  // namespace

std::optional<Edit> readEdit(const std::string& json) {
    const auto doc = json::parse(json);
    if (!doc || doc->kind() != Value::Kind::Object) return std::nullopt;
    Reader r;
    if (r.optInt(*doc, "version", kEditVersion) > kEditVersion) return std::nullopt;
    Edit edit;
    if (const Value* timeline = r.obj(*doc, "timeline")) {
        edit.lanes = r.items<Lane>(*timeline, "lanes", [&r](const Value& l) { return readLane(r, l); });
        edit.durationMs = r.optLng(*timeline, "durationMs", 0);
    }
    edit.markers = r.items<Marker>(*doc, "markers", [&r](const Value& m) { return readMarker(r, m); });
    std::stable_sort(edit.markers.begin(), edit.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.atMs < b.atMs; });
    edit.tracks = oneTrackEach(r.items<Track>(*doc, "keyframes", [&r](const Value& t) { return readTrack(r, t); }));
    if (!r.ok()) return std::nullopt;
    return edit;
}

std::optional<Kind> readKind(const std::string& json) {
    const auto doc = json::parse(json);
    if (!doc || doc->kind() != Value::Kind::Object) return std::nullopt;
    Reader r;
    const std::string type = r.str(*doc, "type");
    Kind kind;
    if (type == "loop") {
        kind.loop = true;
        kind.targetLengthMs = r.optLng(*doc, "targetLengthMs", kLoopTargetMs);
        kind.loopLengthMs = r.optLng(*doc, "loopLengthMs", kLoopMs);
    } else if (type != "standard") {
        return std::nullopt;
    }
    if (!r.ok()) return std::nullopt;
    return kind;
}

std::string writeEdit(const Edit& edit) {
    Writer w;
    w.open('{');
    w.whole("version", kEditVersion);
    w.key("timeline");
    w.open('{');
    w.whole("durationMs", edit.durationMs);
    w.key("lanes");
    w.open('[');
    for (const Lane& l : edit.lanes) writeLane(w, l);
    w.close(']');
    w.close('}');
    w.key("markers");
    w.open('[');
    for (const Marker& m : edit.markers) writeMarker(w, m);
    w.close(']');
    w.key("keyframes");
    w.open('[');
    for (const Track& t : edit.tracks) writeTrack(w, t);
    w.close(']');
    w.close('}');
    return w.take();
}

std::string writeKind(const Kind& kind) {
    Writer w;
    w.open('{');
    if (kind.loop) {
        w.text("type", "loop");
        w.whole("targetLengthMs", kind.targetLengthMs);
        w.whole("loopLengthMs", kind.loopLengthMs);
    } else {
        w.text("type", "standard");
    }
    w.close('}');
    return w.take();
}

Edit newEdit(const std::optional<Song>& song, const std::optional<Look>& look, const IdSource& ids) {
    Edit edit;
    if (song) {
        Lane audio;
        audio.id = ids();
        audio.kind = LaneKind::Audio;
        audio.name = song->title;
        if (song->durationMs >= kMinClipMs) {
            Clip clip;
            clip.id = ids();
            clip.content = AudioContent{song->uri, 0.0f};
            clip.durationMs = song->durationMs;
            clip.sourceDurationMs = song->durationMs;
            clip.label = song->title;
            audio.clips.push_back(std::move(clip));
        }
        edit.lanes.push_back(std::move(audio));
        edit.durationMs = song->durationMs;
    }
    if (look) {
        const long long lengthMs = std::max(song ? song->durationMs : 0LL, look->renderMs);
        Lane visual;
        visual.id = ids();
        visual.kind = LaneKind::Visual;
        if (lengthMs > 0) {
            Clip clip;
            clip.id = ids();
            clip.content = SceneContent{look->sceneId, std::nullopt, look->milkPath, std::nullopt};
            clip.durationMs = lengthMs;
            visual.clips.push_back(std::move(clip));
        }
        edit.lanes.insert(edit.lanes.begin(), std::move(visual));
        edit.durationMs = std::max(edit.durationMs, lengthMs);
    }
    return edit;
}

std::string randomId() {
    std::random_device device;
    std::array<uint8_t, 16> bytes{};
    for (size_t i = 0; i < bytes.size(); i += 4) {
        const uint32_t word = device();
        for (size_t j = 0; j < 4; ++j) bytes[i + j] = static_cast<uint8_t>(word >> (8 * j));
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += kHex[bytes[i] >> 4];
        out += kHex[bytes[i] & 0x0F];
    }
    return out;
}

}  // namespace geode::studio
