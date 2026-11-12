#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace geode::viz::frame::webrtc {

// "Show on a computer" over WebRTC: the rules core/cast/WebRtcLink (the one file that drives libdatachannel)
// follows, pure and host-tested (core/tests/webrtc_rule_test.cpp). Owner, 2026-11-12: "Use webrtc protocol. To
// have smooth play", "Let browser buffer for 0.5 sec", "make connection secure"; the TV keeps HLS.
//
// The browser offers (receive-only video and audio), the phone answers, over one POST to the token-protected
// local server (LinkShare's Resource::Rtc). No STUN or TURN server, no internet: the phone's answer carries one
// host candidate, its own Wi-Fi or Ethernet address; the browser's candidates are kept only when they are IPv4
// literals on that subnet. Browsers hide their host address behind an mDNS name (".local"); such a candidate is
// dropped unread, so nothing is ever resolved, and the browser's address is learnt from its first connectivity
// check instead (ICE's peer-reflexive candidate). The picture is the cast encoder's own H.264, handed over as it
// is (no second render, no second encode); the sound is Opus, which every browser's WebRTC plays (AAC it does
// not). What travels is DTLS-SRTP: encrypted and authenticated against the fingerprint in the answer. The answer
// itself goes over plain http, so this guards against a listener on the Wi-Fi, not against someone who can
// rewrite the traffic between the browser and the phone.

// Browsers on WebRTC at once (the encoder's output is copied to each).
inline constexpr int kMaxPeers = 3;
// An offer larger than this is refused unread (a browser's is a few kilobytes).
inline constexpr size_t kMaxOfferBytes = 16 * 1024;
// How long the answer waits for the phone's own candidate to be gathered (one local address: milliseconds).
inline constexpr int64_t kGatherTimeoutMs = 2'000;
// The browser's receive buffer the page asks for (RTCRtpReceiver.jitterBufferTarget, ms; owner: half a second).
inline constexpr int kJitterTargetMs = 500;
// The fmtp the phone answers Opus with: the encoder's stereo stream, in-band FEC welcome.
inline constexpr std::string_view kOpusFmtp = "minptime=10;useinbandfec=1;stereo=1;sprop-stereo=1";
inline constexpr uint32_t kVideoClockRate = 90'000;
inline constexpr uint32_t kAudioClockRate = 48'000;

// ---- H.264 as the cast encoder writes it (Annex B: 00 00 01 or 00 00 00 01 before each NAL unit) ----

struct H264 {
    int profile = 0;       // profile_idc: 66 Baseline, 77 Main, 100 High
    int constraints = 0;   // the constraint_set flags byte
    int level = 0;         // level_idc
    bool ok() const { return profile > 0; }
};

// Calls f(type, nal, size) for each NAL unit (header byte included, start code not).
template <typename F>
void forEachNal(const uint8_t* p, size_t n, F&& f);

bool hasNal(const uint8_t* annexB, size_t n, int type);
// The profile, constraints and level of the first SPS (type 7) in annexB; !ok() when there is none.
H264 profileOf(const uint8_t* annexB, size_t n);
// What one video frame sends: a keyframe the encoder wrote without its SPS and PPS gets the codec config (the
// SPS and PPS MediaCodec hands over once, at the start) in front, so a browser that joins at any keyframe can
// decode it; anything else is sent as it is.
std::vector<uint8_t> accessUnit(const uint8_t* config, size_t configSize, const uint8_t* frame, size_t frameSize, bool key);

// Whether a browser that offers H.264 with this profile can decode the phone's stream. Constrained Baseline is
// inside every profile; a Baseline stream from MediaCodec uses none of Baseline's extras (FMO, ASO, redundant
// slices), so it is treated as Constrained Baseline. Main needs Main or High; High needs High. The level is not
// compared: the answer echoes the browser's own fmtp, and browsers decode past the level they advertise.
bool compatible(int offeredProfile, int offeredConstraints, H264 ours);

// ---- The offer ----

enum Refusal : int {
    kAccepted = 0,
    kMalformed = 1,   // too big, no ICE credentials or no fingerprint
    kNoVideo = 2,     // no video section the phone may send into
    kNoH264 = 3,      // no packetization-mode=1 H.264 the stream fits
};

struct Choice {
    Refusal refusal = kMalformed;
    std::string videoMid;
    int videoPt = -1;
    std::string videoFmtp;   // the browser's own fmtp line for videoPt, echoed in the answer
    std::string audioMid;    // empty when the browser offered no Opus: video alone
    int audioPt = -1;
};

// Reads a browser's offer: the video section (receive-only or send-receive, port not 0) and the H.264 payload
// the stream fits, the browser's first compatible one, a profile equal to ours first; and its Opus payload if
// it has one.
Choice choose(std::string_view offer, H264 ours);

// ---- Candidates: the LAN only ----
//
// Addresses are IPv4 in host order, as LinkShare's admit reads them; prefix is the bound address's prefix length.

// A remote candidate ("candidate:..." or "a=candidate:...") is kept when it is UDP, component 1, and its
// address an IPv4 literal on the bound site-local subnet. An mDNS name, IPv6, TCP, or any other address: dropped.
bool keepRemote(std::string_view candidate, uint32_t bound, int prefix);
// The offer with every a=candidate line keepRemote refuses taken out (the rest byte for byte).
std::string filterOffer(std::string_view sdp, uint32_t bound, int prefix);
// A local candidate is kept only when it is a UDP host candidate at exactly the bound address.
bool keepLocal(std::string_view candidate, uint32_t bound);
// The answer with every a=candidate line keepLocal refuses taken out.
std::string filterAnswer(std::string_view sdp, uint32_t bound);
// a.b.c.d to host order; 0 when it is not exactly a dotted IPv4 literal.
uint32_t parseIpv4(std::string_view text);

// ---- Timing ----

// The RTP timestamp of a frame at ptsUs: start plus the time since the peer's first frame, on clockRate, wrapping.
uint32_t rtpTimestamp(int64_t ptsUs, int64_t firstPtsUs, uint32_t start, uint32_t clockRate);

// A peer's picture starts at a keyframe: until it has sent one, every other frame is held back (a decoder
// fed from the middle of a group shows garbage until the next one). Returns whether to send this frame.
inline bool admitFrame(bool& started, bool key) {
    if (key) started = true;
    return started;
}

// ---- template ----

template <typename F>
void forEachNal(const uint8_t* p, size_t n, F&& f) {
    if (!p) return;
    size_t i = 0;
    // The first start code.
    auto startAt = [&](size_t at, size_t& len) {
        if (at + 3 <= n && p[at] == 0 && p[at + 1] == 0 && p[at + 2] == 1) { len = 3; return true; }
        if (at + 4 <= n && p[at] == 0 && p[at + 1] == 0 && p[at + 2] == 0 && p[at + 3] == 1) { len = 4; return true; }
        return false;
    };
    size_t len = 0;
    while (i < n && !startAt(i, len)) ++i;
    while (i < n) {
        const size_t begin = i + len;
        size_t j = begin;
        size_t next = 0;
        while (j < n && !startAt(j, next)) ++j;
        size_t end = j;
        // A four-byte start code's leading zero belongs to it, not to this unit; trailing zeros likewise.
        while (end > begin && p[end - 1] == 0) --end;
        if (end > begin) f(p[begin] & 0x1F, p + begin, end - begin);
        i = j;
        len = next;
    }
}

}  // namespace geode::viz::frame::webrtc
