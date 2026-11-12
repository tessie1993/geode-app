#include "viz/frame/WebRtcRule.hpp"

#include <cctype>

#include "viz/frame/LinkShare.hpp"

namespace geode::viz::frame::webrtc {

namespace {

constexpr int kSps = 7;
constexpr int kPps = 8;

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Splits on single spaces, dropping empties.
std::vector<std::string_view> fields(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        const size_t b = i;
        while (i < s.size() && s[i] != ' ') ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

// A non-negative decimal of at most nine digits; -1 otherwise.
int number(std::string_view s) {
    if (s.empty() || s.size() > 9) return -1;
    int n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        n = n * 10 + (c - '0');
    }
    return n;
}

int hexByte(std::string_view s) {
    if (s.size() != 2) return -1;
    int n = 0;
    for (char c : s) {
        n <<= 4;
        if (c >= '0' && c <= '9') n |= c - '0';
        else if (c >= 'a' && c <= 'f') n |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') n |= c - 'A' + 10;
        else return -1;
    }
    return n;
}

// Calls f(line) for each line of an SDP, without its CR or LF.
template <typename F>
void forEachLine(std::string_view sdp, F&& f) {
    size_t i = 0;
    while (i < sdp.size()) {
        size_t j = sdp.find('\n', i);
        if (j == std::string_view::npos) j = sdp.size();
        std::string_view line = sdp.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        f(line);
        i = j + 1;
    }
}

// The value of key=value in a ';'-separated fmtp parameter list; empty when absent.
std::string_view param(std::string_view params, std::string_view key) {
    size_t i = 0;
    while (i <= params.size()) {
        size_t j = params.find(';', i);
        if (j == std::string_view::npos) j = params.size();
        std::string_view kv = params.substr(i, j - i);
        while (!kv.empty() && kv.front() == ' ') kv.remove_prefix(1);
        const size_t eq = kv.find('=');
        if (eq != std::string_view::npos && equalsNoCase(kv.substr(0, eq), key)) return kv.substr(eq + 1);
        i = j + 1;
    }
    return {};
}

// The candidate's fields after "candidate:" (foundation, component, transport, priority, address, port, typ, type).
std::vector<std::string_view> candidateFields(std::string_view c) {
    if (startsWith(c, "a=")) c.remove_prefix(2);
    if (!startsWith(c, "candidate:")) return {};
    c.remove_prefix(10);
    return fields(c);
}

// Copies sdp line by line, leaving out each a=candidate line keep refuses; line breaks kept as CRLF.
template <typename Keep>
std::string filterCandidates(std::string_view sdp, Keep&& keep) {
    std::string out;
    out.reserve(sdp.size());
    forEachLine(sdp, [&](std::string_view line) {
        if (line.empty()) return;
        if (startsWith(line, "a=candidate:") && !keep(line)) return;
        out.append(line);
        out.append("\r\n");
    });
    return out;
}

struct Section {
    std::string_view kind;    // "video", "audio", ...
    bool portZero = false;
    std::string_view mid;
    std::string_view direction = "sendrecv";
    std::vector<int> payloads;   // the m-line's order: the browser's preference
    std::vector<std::pair<int, std::string_view>> rtpmap;
    std::vector<std::pair<int, std::string_view>> fmtp;
};

std::string_view lookup(const std::vector<std::pair<int, std::string_view>>& table, int pt) {
    for (const auto& [k, v] : table)
        if (k == pt) return v;
    return {};
}

}  // namespace

bool hasNal(const uint8_t* annexB, size_t n, int type) {
    bool found = false;
    forEachNal(annexB, n, [&](int t, const uint8_t*, size_t) { found = found || t == type; });
    return found;
}

H264 profileOf(const uint8_t* annexB, size_t n) {
    H264 h;
    forEachNal(annexB, n, [&](int t, const uint8_t* nal, size_t size) {
        if (h.ok() || t != kSps || size < 4) return;
        h.profile = nal[1];
        h.constraints = nal[2];
        h.level = nal[3];
    });
    return h;
}

std::vector<uint8_t> accessUnit(const uint8_t* config, size_t configSize, const uint8_t* frame, size_t frameSize, bool key) {
    std::vector<uint8_t> out;
    if (!frame || frameSize == 0) return out;
    const bool prepend = key && config && configSize > 0 && !hasNal(frame, frameSize, kSps) && hasNal(config, configSize, kSps) &&
                         hasNal(config, configSize, kPps);
    out.reserve(frameSize + (prepend ? configSize : 0));
    if (prepend) out.insert(out.end(), config, config + configSize);
    out.insert(out.end(), frame, frame + frameSize);
    return out;
}

bool compatible(int offeredProfile, int offeredConstraints, H264 ours) {
    (void)offeredConstraints;   // every profile the browser names decodes Constrained Baseline; see the header
    const bool offeredKnown = offeredProfile == 66 || offeredProfile == 77 || offeredProfile == 100;
    if (!offeredKnown || !ours.ok()) return false;
    switch (ours.profile) {
        case 66: return true;
        case 77: return offeredProfile == 77 || offeredProfile == 100;
        case 100: return offeredProfile == 100;
        default: return false;
    }
}

Choice choose(std::string_view offer, H264 ours) {
    Choice c;
    if (offer.empty() || offer.size() > kMaxOfferBytes) return c;
    bool ufrag = false, pwd = false, fingerprint = false;
    std::vector<Section> sections;
    forEachLine(offer, [&](std::string_view line) {
        if (startsWith(line, "a=ice-ufrag:")) ufrag = true;
        if (startsWith(line, "a=ice-pwd:")) pwd = true;
        if (startsWith(line, "a=fingerprint:")) fingerprint = true;
        if (startsWith(line, "m=")) {
            const auto f = fields(line.substr(2));
            Section s;
            if (!f.empty()) s.kind = f[0];
            s.portZero = f.size() > 1 && f[1] == "0";
            for (size_t i = 3; i < f.size(); ++i) {
                const int pt = number(f[i]);
                if (pt >= 0 && pt <= 127) s.payloads.push_back(pt);
            }
            sections.push_back(std::move(s));
            return;
        }
        if (sections.empty()) return;
        Section& s = sections.back();
        if (startsWith(line, "a=mid:")) s.mid = line.substr(6);
        else if (line == "a=sendrecv" || line == "a=recvonly" || line == "a=sendonly" || line == "a=inactive") s.direction = line.substr(2);
        else if (startsWith(line, "a=rtpmap:") || startsWith(line, "a=fmtp:")) {
            const bool map = startsWith(line, "a=rtpmap:");
            std::string_view rest = line.substr(map ? 9 : 7);
            const size_t sp = rest.find(' ');
            if (sp == std::string_view::npos) return;
            const int pt = number(rest.substr(0, sp));
            if (pt < 0) return;
            (map ? s.rtpmap : s.fmtp).emplace_back(pt, rest.substr(sp + 1));
        }
    });
    if (!ufrag || !pwd || !fingerprint) return c;
    const auto receives = [](const Section& s) {
        return !s.portZero && !s.mid.empty() && (s.direction == "recvonly" || s.direction == "sendrecv");
    };
    const Section* video = nullptr;
    const Section* audio = nullptr;
    for (const Section& s : sections) {
        if (s.kind == "video" && !video && receives(s)) video = &s;
        if (s.kind == "audio" && !audio && receives(s)) audio = &s;
    }
    if (!video) {
        c.refusal = kNoVideo;
        return c;
    }
    int best = -1;
    bool bestExact = false;
    for (int pt : video->payloads) {
        const std::string_view map = lookup(video->rtpmap, pt);
        if (!equalsNoCase(map.substr(0, map.find('/')), "H264")) continue;
        const std::string_view fmtp = lookup(video->fmtp, pt);
        if (param(fmtp, "packetization-mode") != "1") continue;
        const std::string_view id = param(fmtp, "profile-level-id");
        if (id.size() != 6) continue;
        const int profile = hexByte(id.substr(0, 2));
        const int constraints = hexByte(id.substr(2, 2));
        const int level = hexByte(id.substr(4, 2));
        if (profile < 0 || constraints < 0 || level < 0 || !compatible(profile, constraints, ours)) continue;
        const bool exact = profile == ours.profile;
        if (best < 0 || (exact && !bestExact)) {
            best = pt;
            bestExact = exact;
        }
    }
    if (best < 0) {
        c.refusal = kNoH264;
        return c;
    }
    c.refusal = kAccepted;
    c.videoMid = std::string(video->mid);
    c.videoPt = best;
    c.videoFmtp = std::string(lookup(video->fmtp, best));
    if (audio) {
        for (int pt : audio->payloads) {
            if (equalsNoCase(lookup(audio->rtpmap, pt), "opus/48000/2")) {
                c.audioMid = std::string(audio->mid);
                c.audioPt = pt;
                break;
            }
        }
    }
    return c;
}

uint32_t parseIpv4(std::string_view text) {
    uint32_t a = 0;
    int parts = 0;
    size_t i = 0;
    for (;;) {
        const size_t dot = text.find('.', i);
        const std::string_view part = text.substr(i, dot == std::string_view::npos ? std::string_view::npos : dot - i);
        // Plain decimal octets only: no sign, no leading zero, nothing a resolver would read another way.
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part[0] == '0')) return 0;
        const int n = number(part);
        if (n < 0 || n > 255) return 0;
        a = (a << 8) | static_cast<uint32_t>(n);
        if (++parts > 4) return 0;
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return parts == 4 ? a : 0;
}

bool keepRemote(std::string_view candidate, uint32_t bound, int prefix) {
    const auto f = candidateFields(candidate);
    if (f.size() < 8 || f[1] != "1" || !equalsNoCase(f[2], "udp") || f[6] != "typ") return false;
    const uint32_t address = parseIpv4(f[4]);
    if (address == 0 || number(f[5]) <= 0 || number(f[5]) > 65535) return false;
    return link::admit(address, bound, prefix, 0, link::kWantLink) == link::kBrowser;
}

std::string filterOffer(std::string_view sdp, uint32_t bound, int prefix) {
    return filterCandidates(sdp, [&](std::string_view line) { return keepRemote(line, bound, prefix); });
}

bool keepLocal(std::string_view candidate, uint32_t bound) {
    const auto f = candidateFields(candidate);
    if (f.size() < 8 || !equalsNoCase(f[2], "udp") || f[6] != "typ" || f[7] != "host") return false;
    return bound != 0 && parseIpv4(f[4]) == bound;
}

std::string filterAnswer(std::string_view sdp, uint32_t bound) {
    return filterCandidates(sdp, [&](std::string_view line) { return keepLocal(line, bound); });
}

uint32_t rtpTimestamp(int64_t ptsUs, int64_t firstPtsUs, uint32_t start, uint32_t clockRate) {
    const int64_t since = ptsUs - firstPtsUs;
    // Floor division, so a frame before the first (a reordered one) steps back instead of rounding toward it.
    const int64_t scaled = since * static_cast<int64_t>(clockRate);
    int64_t ticks = scaled / 1'000'000;
    if (scaled % 1'000'000 != 0 && scaled < 0) --ticks;
    return start + static_cast<uint32_t>(static_cast<uint64_t>(ticks));
}

}  // namespace geode::viz::frame::webrtc
