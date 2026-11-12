#include "cast/WebRtcLink.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <random>
#include <stdexcept>
#include <utility>

#include <rtc/rtc.hpp>

#include "api/geode_api.h"

namespace geode::cast {

namespace rule = viz::frame::webrtc;

static_assert(rule::kMaxPeers == GEODE_WEBRTC_MAX_PEERS && rule::kGatherTimeoutMs == GEODE_WEBRTC_GATHER_MS &&
              rule::kJitterTargetMs == GEODE_WEBRTC_JITTER_TARGET_MS && rule::kMaxOfferBytes == GEODE_WEBRTC_OFFER_MAX);

struct WebRtcLink::Peer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video;
    std::shared_ptr<rtc::Track> audio;
    std::shared_ptr<rtc::RtpPacketizationConfig> videoRtp;
    std::shared_ptr<rtc::RtpPacketizationConfig> audioRtp;
    std::atomic<int> state{static_cast<int>(rtc::PeerConnection::State::New)};
    bool started = false;            // the link's lock: a keyframe has gone out
    // The link's lock: each track's first PTS sent. Each track keeps its own clock (the video's PTS and the Opus
    // encoder's are separate time bases); the browser lines them up by the sender reports, which map each
    // track's RTP time to the wall clock at the moment it is sent.
    int64_t videoOriginUs = INT64_MIN;
    int64_t audioOriginUs = INT64_MIN;

    // Callbacks first, so none fires into a peer being freed; then the connection.
    void shut() {
        if (!pc) return;
        try {
            pc->resetCallbacks();
            pc->close();
        } catch (const std::exception&) {
        }
    }
};

namespace {

constexpr const char* kCname = "beatrealms";
constexpr const char* kStreamId = "beatrealms";

uint32_t randomSsrc() {
    static std::mutex m;
    static std::mt19937 gen{std::random_device{}()};
    std::lock_guard<std::mutex> l(m);
    uint32_t v = 0;
    while (v == 0) v = static_cast<uint32_t>(gen());
    return v;
}

bool live(const WebRtcLink::Peer& p) {
    const auto s = static_cast<rtc::PeerConnection::State>(p.state.load());
    return s != rtc::PeerConnection::State::Failed && s != rtc::PeerConnection::State::Closed;
}

}  // namespace

WebRtcLink::WebRtcLink(std::string boundDotted, uint32_t bound, int prefix)
    : boundDotted_(std::move(boundDotted)), bound_(bound), prefix_(prefix) {}

WebRtcLink::~WebRtcLink() { close(); }

void WebRtcLink::close() {
    std::vector<std::shared_ptr<Peer>> gone;
    {
        std::lock_guard<std::mutex> l(lock_);
        closed_ = true;
        gone.swap(peers_);
    }
    for (auto& p : gone) p->shut();
}

void WebRtcLink::prune() {
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (live(**it)) {
            ++it;
        } else {
            (*it)->shut();
            it = peers_.erase(it);
        }
    }
}

int WebRtcLink::answer(std::string_view offer, std::string& out) {
    rule::H264 profile;
    {
        std::lock_guard<std::mutex> l(lock_);
        if (closed_) return GEODE_WEBRTC_REFUSED_FAILED;
        prune();
        if (static_cast<int>(peers_.size()) >= rule::kMaxPeers) return GEODE_WEBRTC_REFUSED_FULL;
        profile = profile_;
    }
    if (!profile.ok()) return GEODE_WEBRTC_REFUSED_NOT_READY;
    const rule::Choice choice = rule::choose(offer, profile);
    switch (choice.refusal) {
        case rule::kAccepted: break;
        case rule::kNoVideo: return GEODE_WEBRTC_REFUSED_NO_VIDEO;
        case rule::kNoH264: return GEODE_WEBRTC_REFUSED_NO_H264;
        default: return GEODE_WEBRTC_REFUSED_MALFORMED;
    }
    // The browser's candidates off the LAN, and its mDNS names, never reach the stack: nothing is resolved.
    const std::string filtered = rule::filterOffer(offer, bound_, prefix_);

    auto peer = std::make_shared<Peer>();
    std::mutex gatherLock;
    std::condition_variable gathered;
    bool complete = false;
    try {
        rtc::Configuration config;
        config.iceServers.clear();          // no STUN, no TURN: the LAN only
        config.bindAddress = boundDotted_;  // the server's Wi-Fi or Ethernet address, never a VPN's or cellular's
        config.enableIceTcp = false;
        peer->pc = std::make_shared<rtc::PeerConnection>(config);

        rtc::Description::Video videoMedia(choice.videoMid, rtc::Description::Direction::SendOnly);
        videoMedia.addH264Codec(choice.videoPt, choice.videoFmtp);
        const uint32_t videoSsrc = randomSsrc();
        videoMedia.addSSRC(videoSsrc, std::string(kCname), std::string(kStreamId), std::string("video"));
        peer->video = peer->pc->addTrack(videoMedia);
        peer->videoRtp = std::make_shared<rtc::RtpPacketizationConfig>(videoSsrc, kCname, static_cast<uint8_t>(choice.videoPt),
                                                                       rtc::H264RtpPacketizer::ClockRate);
        auto videoChain = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, peer->videoRtp);
        videoChain->addToChain(std::make_shared<rtc::RtcpSrReporter>(peer->videoRtp));
        videoChain->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        peer->video->setMediaHandler(videoChain);

        if (choice.audioPt >= 0) {
            rtc::Description::Audio audioMedia(choice.audioMid, rtc::Description::Direction::SendOnly);
            audioMedia.addOpusCodec(choice.audioPt, std::string(rule::kOpusFmtp));
            const uint32_t audioSsrc = randomSsrc();
            audioMedia.addSSRC(audioSsrc, std::string(kCname), std::string(kStreamId), std::string("audio"));
            peer->audio = peer->pc->addTrack(audioMedia);
            peer->audioRtp = std::make_shared<rtc::RtpPacketizationConfig>(audioSsrc, kCname, static_cast<uint8_t>(choice.audioPt),
                                                                           rtc::OpusRtpPacketizer::DefaultClockRate);
            auto audioChain = std::make_shared<rtc::OpusRtpPacketizer>(peer->audioRtp);
            audioChain->addToChain(std::make_shared<rtc::RtcpSrReporter>(peer->audioRtp));
            peer->audio->setMediaHandler(audioChain);
        }

        std::weak_ptr<Peer> weak = peer;
        peer->pc->onStateChange([weak](rtc::PeerConnection::State s) {
            if (auto p = weak.lock()) p->state.store(static_cast<int>(s));
        });
        peer->pc->onGatheringStateChange([&gatherLock, &gathered, &complete](rtc::PeerConnection::GatheringState s) {
            if (s != rtc::PeerConnection::GatheringState::Complete) return;
            std::lock_guard<std::mutex> l(gatherLock);
            complete = true;
            gathered.notify_all();
        });
        // An offer: the stack answers it and gathers the phone's candidate by itself.
        peer->pc->setRemoteDescription(rtc::Description(filtered, rtc::Description::Type::Offer));
        {
            std::unique_lock<std::mutex> l(gatherLock);
            gathered.wait_for(l, std::chrono::milliseconds(rule::kGatherTimeoutMs), [&complete] { return complete; });
        }
        // The callback names locals of this frame: unhook it before they go.
        peer->pc->onGatheringStateChange(nullptr);
        const auto local = peer->pc->localDescription();
        if (!local) throw std::runtime_error("no local description");
        out = rule::filterAnswer(std::string(*local), bound_);
    } catch (const std::exception&) {
        peer->shut();
        return GEODE_WEBRTC_REFUSED_FAILED;
    }
    if (out.find("a=candidate:") == std::string::npos || out.size() >= GEODE_WEBRTC_ANSWER_CAP) {
        peer->shut();
        out.clear();
        return GEODE_WEBRTC_REFUSED_FAILED;
    }
    std::lock_guard<std::mutex> l(lock_);
    if (closed_ || static_cast<int>(peers_.size()) >= rule::kMaxPeers) {
        peer->shut();
        out.clear();
        return closed_ ? GEODE_WEBRTC_REFUSED_FAILED : GEODE_WEBRTC_REFUSED_FULL;
    }
    peers_.push_back(peer);
    return static_cast<int>(out.size());
}

void WebRtcLink::videoConfig(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;
    const rule::H264 profile = rule::profileOf(data, size);
    if (!profile.ok()) return;
    std::lock_guard<std::mutex> l(lock_);
    config_.assign(data, data + size);
    profile_ = profile;
}

void WebRtcLink::video(const uint8_t* data, size_t size, int64_t ptsUs, bool key) {
    if (!data || size == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (peers_.empty()) return;
    // A keyframe that carries its own SPS also tells the profile, before any config buffer.
    if (!profile_.ok() && key) {
        const rule::H264 inBand = rule::profileOf(data, size);
        if (inBand.ok()) profile_ = inBand;
    }
    std::vector<uint8_t> unit;
    for (auto& p : peers_) {
        if (!p->video || !live(*p) || !p->video->isOpen()) continue;
        if (!rule::admitFrame(p->started, key)) continue;
        if (unit.empty()) unit = rule::accessUnit(config_.data(), config_.size(), data, size, key);
        if (p->videoOriginUs == INT64_MIN) p->videoOriginUs = ptsUs;
        const uint32_t ts = rule::rtpTimestamp(ptsUs, p->videoOriginUs, p->videoRtp->startTimestamp, rule::kVideoClockRate);
        try {
            p->video->sendFrame(reinterpret_cast<const rtc::byte*>(unit.data()), unit.size(), rtc::FrameInfo(ts));
        } catch (const std::exception&) {
        }
    }
}

void WebRtcLink::audio(const uint8_t* data, size_t size, int64_t ptsUs) {
    if (!data || size == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    for (auto& p : peers_) {
        // The sound waits for the picture's first keyframe, so both start together.
        if (!p->audio || !p->started || !live(*p) || !p->audio->isOpen()) continue;
        if (p->audioOriginUs == INT64_MIN) p->audioOriginUs = ptsUs;
        const uint32_t ts = rule::rtpTimestamp(ptsUs, p->audioOriginUs, p->audioRtp->startTimestamp, rule::kAudioClockRate);
        try {
            p->audio->sendFrame(reinterpret_cast<const rtc::byte*>(data), size, rtc::FrameInfo(ts));
        } catch (const std::exception&) {
        }
    }
}

int WebRtcLink::peers() const {
    std::lock_guard<std::mutex> l(lock_);
    int n = 0;
    for (const auto& p : peers_)
        if (static_cast<rtc::PeerConnection::State>(p->state.load()) == rtc::PeerConnection::State::Connected) ++n;
    return n;
}

}  // namespace geode::cast
