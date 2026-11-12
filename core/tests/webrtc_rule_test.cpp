// Host-side checks for core/viz/frame/WebRtcRule: "Show on a computer" over WebRTC, the rules core/cast/WebRtcLink
// follows. Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml).
//
// What is pinned: the H.264 the encoder writes (NAL units across 3- and 4-byte start codes, the SPS's profile, the
// SPS and PPS put in front of a keyframe that lacks them, and only then); which browser profiles can play which
// stream; reading a browser's offer (its receive-only video section, the first compatible packetization-mode=1
// H.264, a profile equal to ours first, the browser's fmtp echoed, Opus when offered, refusals for a missing
// video section, no H.264 the stream fits, no ICE credentials, a fingerprint missing or an offer too big); the
// LAN-only candidate rule both ways (mDNS names, IPv6, TCP, another subnet, a public address and a malformed line
// dropped; the phone's own host candidate kept, nothing else); the RTP clock (90 kHz and 48 kHz from the PTS,
// wrapping, a frame before the first); and that a peer's picture starts at a keyframe.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "viz/frame/WebRtcRule.hpp"

namespace rtc = geode::viz::frame::webrtc;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

constexpr uint32_t ip(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return a << 24 | b << 16 | c << 8 | d; }

const uint32_t kBound = ip(192, 168, 1, 40);

// An SPS (High, constraints 0, level 4.0), a PPS, and an IDR slice, in Annex B.
const std::vector<uint8_t> kConfigHigh = {0, 0, 0, 1, 0x67, 100, 0x00, 0x28, 0xAC, 0xD9, 0, 0, 0, 1, 0x68, 0xEB, 0xE3, 0xCB};
const std::vector<uint8_t> kIdr = {0, 0, 0, 1, 0x65, 0x88, 0x84, 0x21, 0xA0};
const std::vector<uint8_t> kSlice = {0, 0, 1, 0x41, 0x9A, 0x02, 0x10};

// A browser's offer, shaped like Chrome's: receive-only video and audio, one bundle, mDNS host candidates.
std::string offer(const std::string& videoPayloads, const std::string& videoAttrs, bool withAudio = true,
                  const std::string& videoDirection = "a=recvonly", const std::string& extra = "") {
    std::string s =
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=msid-semantic: WMS\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF " + videoPayloads + "\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=candidate:1 1 udp 2113937151 3f6c2a1e-1111-4222-8333-944455556666.local 54400 typ host generation 0\r\n"
        "a=candidate:2 1 udp 2113937151 192.168.1.77 54401 typ host generation 0\r\n"
        "a=candidate:3 1 udp 1677729535 203.0.113.9 54402 typ srflx raddr 0.0.0.0 rport 0\r\n"
        "a=ice-ufrag:ab12\r\n"
        "a=ice-pwd:0123456789abcdefghijklmn\r\n"
        "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n" + videoDirection + "\r\n"
        "a=rtcp-mux\r\n" + videoAttrs;
    if (withAudio) {
        s +=
            "m=audio 9 UDP/TLS/RTP/SAVPF 111 0\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=mid:1\r\n"
            "a=recvonly\r\n"
            "a=rtcp-mux\r\n"
            "a=rtpmap:111 opus/48000/2\r\n"
            "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
            "a=rtpmap:0 PCMU/8000\r\n";
    }
    return s + extra;
}

// Chrome's usual H.264 list: Baseline, Constrained Baseline, Main, High; VP8 first.
const std::string kChromeVideo =
    "a=rtpmap:96 VP8/90000\r\n"
    "a=rtpmap:102 H264/90000\r\n"
    "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f\r\n"
    "a=rtpmap:106 H264/90000\r\n"
    "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f\r\n"
    "a=rtpmap:108 H264/90000\r\n"
    "a=fmtp:108 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
    "a=rtpmap:112 H264/90000\r\n"
    "a=fmtp:112 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f\r\n"
    "a=rtpmap:127 H264/90000\r\n"
    "a=fmtp:127 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640c1f\r\n";
const std::string kChromePayloads = "96 102 106 108 112 127";

void testNal() {
    int count = 0, types = 0;
    std::vector<uint8_t> both = kConfigHigh;
    both.insert(both.end(), kSlice.begin(), kSlice.end());
    rtc::forEachNal(both.data(), both.size(), [&](int t, const uint8_t* nal, size_t n) {
        ++count;
        types = types * 100 + t;
        check(n > 0 && (nal[0] & 0x1F) == t, "each unit starts at its header");
    });
    check(count == 3 && types == 70801, "SPS, PPS and a slice across 4- and 3-byte start codes");
    check(rtc::hasNal(kConfigHigh.data(), kConfigHigh.size(), 7) && !rtc::hasNal(kIdr.data(), kIdr.size(), 7), "hasNal");
    const rtc::H264 h = rtc::profileOf(kConfigHigh.data(), kConfigHigh.size());
    check(h.ok() && h.profile == 100 && h.constraints == 0 && h.level == 0x28, "the SPS's profile, constraints, level");
    check(!rtc::profileOf(kIdr.data(), kIdr.size()).ok() && !rtc::profileOf(nullptr, 0).ok(), "no SPS: no profile");
    const uint8_t junk[] = {1, 2, 3, 4, 5};
    check(!rtc::profileOf(junk, sizeof junk).ok(), "no start code: nothing");

    const auto unit = rtc::accessUnit(kConfigHigh.data(), kConfigHigh.size(), kIdr.data(), kIdr.size(), true);
    check(unit.size() == kConfigHigh.size() + kIdr.size() && std::equal(kConfigHigh.begin(), kConfigHigh.end(), unit.begin()),
          "a keyframe without its SPS gets the config in front");
    std::vector<uint8_t> full = kConfigHigh;
    full.insert(full.end(), kIdr.begin(), kIdr.end());
    check(rtc::accessUnit(kConfigHigh.data(), kConfigHigh.size(), full.data(), full.size(), true) == full,
          "a keyframe that carries its own SPS is sent as it is");
    check(rtc::accessUnit(kConfigHigh.data(), kConfigHigh.size(), kSlice.data(), kSlice.size(), false) == kSlice,
          "a delta frame is sent as it is");
    check(rtc::accessUnit(nullptr, 0, kIdr.data(), kIdr.size(), true) == kIdr, "no config yet: the frame alone");
    check(rtc::accessUnit(kConfigHigh.data(), kConfigHigh.size(), nullptr, 0, true).empty(), "no frame: nothing");
}

void testCompatible() {
    const rtc::H264 cb{66, 0xC0, 31}, main{77, 0x40, 31}, high{100, 0, 40};
    for (int p : {66, 77, 100}) check(rtc::compatible(p, 0, cb), "Constrained Baseline plays on every profile");
    check(!rtc::compatible(66, 0xE0, main) && rtc::compatible(77, 0, main) && rtc::compatible(100, 0, main), "Main: Main or High");
    check(!rtc::compatible(66, 0xE0, high) && !rtc::compatible(77, 0, high) && rtc::compatible(100, 0, high), "High: High only");
    check(!rtc::compatible(244, 0, high) && !rtc::compatible(100, 0, rtc::H264{}) && !rtc::compatible(100, 0, rtc::H264{122, 0, 40}),
          "an unknown profile, or no stream profile: no");
}

void testChoose() {
    const rtc::H264 high{100, 0, 40}, cb{66, 0xC0, 31}, main{77, 0x40, 31};
    rtc::Choice c = rtc::choose(offer(kChromePayloads, kChromeVideo), high);
    check(c.refusal == rtc::kAccepted && c.videoPt == 127 && c.videoMid == "0", "High: Chrome's 640c1f");
    check(c.videoFmtp == "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640c1f", "the browser's fmtp, echoed");
    check(c.audioPt == 111 && c.audioMid == "1", "and its Opus");
    c = rtc::choose(offer(kChromePayloads, kChromeVideo), cb);
    check(c.refusal == rtc::kAccepted && c.videoPt == 102, "Constrained Baseline: the first mode-1 Baseline");
    c = rtc::choose(offer(kChromePayloads, kChromeVideo), main);
    check(c.videoPt == 112, "Main: Main before High");

    // Firefox-like: Constrained Baseline only; a High stream has nothing to go to.
    const std::string firefox =
        "a=rtpmap:126 H264/90000\r\na=fmtp:126 profile-level-id=42e01f;level-asymmetry-allowed=1;packetization-mode=1\r\n"
        "a=rtpmap:97 H264/90000\r\na=fmtp:97 profile-level-id=42e01f;level-asymmetry-allowed=1\r\n";
    check(rtc::choose(offer("126 97", firefox), high).refusal == rtc::kNoH264, "High against Constrained Baseline only: refused");
    c = rtc::choose(offer("126 97", firefox), cb);
    check(c.refusal == rtc::kAccepted && c.videoPt == 126, "packetization-mode 0 (or unset) is never chosen");
    check(rtc::choose(offer("96", "a=rtpmap:96 VP8/90000\r\n"), high).refusal == rtc::kNoH264, "no H.264: refused");
    check(rtc::choose(offer("127", "a=rtpmap:127 h264/90000\r\na=fmtp:127 packetization-mode=1;profile-level-id=640C1F\r\n"), high)
                  .videoPt == 127,
          "codec names and hex in any case");
    check(rtc::choose(offer("127", "a=rtpmap:127 H264/90000\r\na=fmtp:127 packetization-mode=1;profile-level-id=64zz1f\r\n"), high)
                  .refusal == rtc::kNoH264,
          "a malformed profile-level-id: skipped");

    c = rtc::choose(offer(kChromePayloads, kChromeVideo, false), high);
    check(c.refusal == rtc::kAccepted && c.audioPt < 0 && c.audioMid.empty(), "no audio section: video alone");
    check(rtc::choose(offer(kChromePayloads, kChromeVideo, true, "a=sendonly"), high).refusal == rtc::kNoVideo,
          "a browser that only sends video: refused");
    check(rtc::choose(offer(kChromePayloads, kChromeVideo, true, "a=inactive"), high).refusal == rtc::kNoVideo, "inactive: refused");
    check(rtc::choose(offer(kChromePayloads, kChromeVideo, true, "a=sendrecv"), high).refusal == rtc::kAccepted, "sendrecv: fine");

    std::string noUfrag = offer(kChromePayloads, kChromeVideo);
    noUfrag.erase(noUfrag.find("a=ice-ufrag:"), std::string("a=ice-ufrag:ab12\r\n").size());
    check(rtc::choose(noUfrag, high).refusal == rtc::kMalformed, "no ICE user fragment: malformed");
    std::string noPrint = offer(kChromePayloads, kChromeVideo);
    noPrint.erase(noPrint.find("a=fingerprint:"), noPrint.find("a=setup:") - noPrint.find("a=fingerprint:"));
    check(rtc::choose(noPrint, high).refusal == rtc::kMalformed, "no fingerprint: malformed (DTLS could not be checked)");
    check(rtc::choose("", high).refusal == rtc::kMalformed, "empty: malformed");
    check(rtc::choose(offer(kChromePayloads, kChromeVideo, true, "a=recvonly", std::string(rtc::kMaxOfferBytes, 'x')), high).refusal ==
              rtc::kMalformed,
          "too big: malformed, unread");
    const std::string lf = [] {
        std::string s = offer(kChromePayloads, kChromeVideo);
        std::string out;
        for (char ch : s)
            if (ch != '\r') out += ch;
        return out;
    }();
    check(rtc::choose(lf, high).videoPt == 127, "bare LF line ends read the same");
}

void testCandidates() {
    check(rtc::parseIpv4("192.168.1.77") == ip(192, 168, 1, 77) && rtc::parseIpv4("10.0.0.1") == ip(10, 0, 0, 1), "dotted IPv4");
    for (const char* bad : {"192.168.1", "192.168.1.77.5", "192.168.01.77", "256.1.1.1", "1..1.1", "", "a.b.c.d", "0x7f.0.0.1",
                            "3232235853", "::1", "192.168.1.77 "})
        check(rtc::parseIpv4(bad) == 0, "anything but four plain octets: 0");

    const int p = 24;
    check(rtc::keepRemote("a=candidate:2 1 udp 2113937151 192.168.1.77 54401 typ host generation 0", kBound, p), "a LAN host: kept");
    check(rtc::keepRemote("candidate:2 1 UDP 2113937151 192.168.1.77 54401 typ prflx", kBound, p), "without a=, any case");
    check(!rtc::keepRemote("a=candidate:1 1 udp 2113937151 3f6c2a1e-1111.local 54400 typ host", kBound, p),
          "an mDNS name: dropped, never resolved");
    check(!rtc::keepRemote("a=candidate:3 1 udp 1677729535 203.0.113.9 54402 typ srflx raddr 0.0.0.0 rport 0", kBound, p),
          "a public address: dropped");
    check(!rtc::keepRemote("a=candidate:4 1 udp 2113937151 192.168.2.77 54401 typ host", kBound, p), "another subnet: dropped");
    check(!rtc::keepRemote("a=candidate:5 1 tcp 1518280447 192.168.1.77 9 typ host tcptype active", kBound, p), "TCP: dropped");
    check(!rtc::keepRemote("a=candidate:6 1 udp 2122262783 fe80::1 54403 typ host", kBound, p), "IPv6: dropped");
    check(!rtc::keepRemote("a=candidate:7 2 udp 2113937151 192.168.1.77 54404 typ host", kBound, p), "an RTCP component: dropped");
    check(!rtc::keepRemote("a=candidate:8 1 udp 2113937151 192.168.1.77 0 typ host", kBound, p), "port 0: dropped");
    check(!rtc::keepRemote("a=candidate:9 1 udp", kBound, p) && !rtc::keepRemote("a=mid:0", kBound, p), "malformed: dropped");

    const std::string filtered = rtc::filterOffer(offer(kChromePayloads, kChromeVideo), kBound, p);
    check(has(filtered, "a=candidate:2 1 udp 2113937151 192.168.1.77 54401 typ host"), "the offer keeps its LAN candidate");
    check(!has(filtered, ".local") && !has(filtered, "203.0.113.9"), "and loses the mDNS name and the public one");
    check(has(filtered, "a=ice-ufrag:ab12\r\n") && has(filtered, "a=rtpmap:127 H264/90000\r\n") && has(filtered, "m=audio"),
          "everything else unchanged");

    const std::string answer =
        "v=0\r\na=ice-ufrag:x\r\nm=video 9 UDP/TLS/RTP/SAVPF 127\r\n"
        "a=candidate:1 1 UDP 2122317823 192.168.1.40 50000 typ host\r\n"
        "a=candidate:2 1 UDP 2122317823 10.8.0.2 50001 typ host\r\n"
        "a=candidate:3 1 UDP 1686052607 203.0.113.9 50002 typ srflx raddr 192.168.1.40 rport 50000\r\n"
        "a=candidate:4 1 TCP 2122317823 192.168.1.40 9 typ host tcptype passive\r\n"
        "a=end-of-candidates\r\n";
    const std::string kept = rtc::filterAnswer(answer, kBound);
    check(has(kept, "192.168.1.40 50000 typ host"), "the answer keeps the phone's own host candidate");
    check(!has(kept, "10.8.0.2") && !has(kept, "203.0.113.9") && !has(kept, "TCP"),
          "and nothing else: no VPN address, no reflexive one, no TCP");
    check(has(kept, "a=end-of-candidates\r\n") && has(kept, "m=video"), "the rest stays");
    check(!rtc::keepLocal("a=candidate:1 1 UDP 2122317823 192.168.1.40 50000 typ host", 0), "no bound address: nothing kept");
}

void testTiming() {
    check(rtc::rtpTimestamp(1'000'000, 1'000'000, 5, rtc::kVideoClockRate) == 5, "the first frame is the start");
    check(rtc::rtpTimestamp(1'033'333, 1'000'000, 0, rtc::kVideoClockRate) == 2999, "a 30 fps frame later: 2999 ticks (floor)");
    check(rtc::rtpTimestamp(2'000'000, 1'000'000, 0, rtc::kVideoClockRate) == 90'000, "a second: 90 000");
    check(rtc::rtpTimestamp(1'020'000, 1'000'000, 0, rtc::kAudioClockRate) == 960, "20 ms of Opus: 960");
    check(rtc::rtpTimestamp(2'000'000, 1'000'000, 0xFFFFFFF0u, rtc::kVideoClockRate) == 90'000u - 16u, "it wraps");
    check(rtc::rtpTimestamp(999'989, 1'000'000, 100, rtc::kVideoClockRate) == 99, "a frame before the first steps back");
    // Three hours at 90 kHz: no overflow on the way.
    const int64_t threeHoursUs = int64_t{3} * 3600 * 1'000'000;
    check(rtc::rtpTimestamp(threeHoursUs, 0, 0, rtc::kVideoClockRate) == static_cast<uint32_t>(int64_t{3} * 3600 * 90'000),
          "three hours on the clock");

    bool started = false;
    check(!rtc::admitFrame(started, false), "no picture before the first keyframe");
    check(rtc::admitFrame(started, true) && rtc::admitFrame(started, false), "from the keyframe on, every frame");
}

}  // namespace

int main() {
    testNal();
    testCompatible();
    testChoose();
    testCandidates();
    testTiming();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("webrtc_rule_test: all passed\n");
    return 0;
}
