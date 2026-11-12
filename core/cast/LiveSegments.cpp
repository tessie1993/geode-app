#include "cast/LiveSegments.hpp"

#include <cstring>
#include <utility>

#include "viz/frame/LinkShare.hpp"

namespace geode::cast {

namespace hls = viz::frame::hls;
namespace share = viz::frame::link;

namespace {

constexpr size_t kBoxHeader = 8;
constexpr size_t kLargeBoxHeader = 16;

bool isType(const uint8_t* box, const char* name) { return std::memcmp(box + 4, name, 4) == 0; }

}  // namespace

LiveSegments::LiveSegments(std::function<int64_t()> nowMs) : nowMs_(std::move(nowMs)) {}

void LiveSegments::write(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;
    std::lock_guard<std::mutex> hold(lock_);
    if (broken_) return;
    pending_.insert(pending_.end(), data, data + size);
    size_t at = 0;
    while (pending_.size() - at >= kBoxHeader) {
        const uint8_t* box = pending_.data() + at;
        const size_t have = pending_.size() - at;
        uint64_t boxSize = hls::detail::be32(box);
        size_t header = kBoxHeader;
        if (boxSize == 1) {
            if (have < kLargeBoxHeader) break;   // its 64-bit size is not all here yet
            boxSize = hls::detail::be64(box + kBoxHeader);
            header = kLargeBoxHeader;
        }
        if (boxSize < header || boxSize > kMaxBoxBytes) {
            // Not a box any muxer writes (0, "to the end of the file", included: a live stream has no end).
            broken_ = true;
            pending_.clear();
            pending_.shrink_to_fit();
            return;
        }
        if (have < boxSize) break;
        const size_t whole = static_cast<size_t>(boxSize);
        onBox(box, whole);
        at += whole;
        fileAt_ += static_cast<int64_t>(whole);
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(at));
}

void LiveSegments::onBox(const uint8_t* box, size_t size) {
    if (isType(box, "ftyp") || isType(box, "moov")) {
        header_.insert(header_.end(), box, box + size);
        if (isType(box, "moov")) {
            init_ = std::make_shared<const std::vector<uint8_t>>(header_);
            master_.clear();
            lastCutMs_ = nowMs_();
            cut_ = true;
        }
    } else if (isType(box, "moof")) {
        moofAt_ = fileAt_;
        fragment_.assign(box, box + size);
        inFragment_ = true;
    } else if (isType(box, "mdat")) {
        if (!inFragment_) return;
        fragment_.insert(fragment_.end(), box, box + size);
        publish();
        fragment_.clear();
        inFragment_ = false;
    }
    // styp, sidx, mfra (at close): nothing the playlist needs.
}

void LiveSegments::publish() {
    lastCutMs_ = nowMs_();
    cut_ = true;
    std::vector<uint8_t> bytes(fragment_.size() + hls::kMaxGrowth);
    const size_t rewritten =
        init_ ? hls::rewriteFragment(init_->data(), init_->size(), moofAt_, fragment_.data(), fragment_.size(),
                                     decodeTimes_, bytes.data(), bytes.size())
              : 0;
    if (rewritten > 0) {
        bytes.resize(rewritten);
    } else {
        bytes = fragment_;
        ++servedAsMuxed_;
    }
    const double seconds = init_ ? hls::fragmentSeconds(init_->data(), init_->size(), bytes.data(), bytes.size()) : 0.0;
    segments_.push_back({nextSequence_++, std::make_shared<const std::vector<uint8_t>>(std::move(bytes)), seconds});
    while (segments_.size() > static_cast<size_t>(hls::kWindow)) segments_.pop_front();
}

Bytes LiveSegments::init() const {
    std::lock_guard<std::mutex> hold(lock_);
    return init_;
}

std::string LiveSegments::masterPlaylist() const {
    std::lock_guard<std::mutex> hold(lock_);
    if (!init_) return {};
    if (master_.empty()) master_ = share::masterPlaylist(init_->data(), init_->size());
    return master_;
}

Bytes LiveSegments::segment(int64_t sequence) const {
    std::lock_guard<std::mutex> hold(lock_);
    for (const Segment& s : segments_)
        if (s.sequence == sequence) return s.bytes;
    return nullptr;
}

bool LiveSegments::nextAfter(int64_t after, Next& out) const {
    std::lock_guard<std::mutex> hold(lock_);
    if (segments_.empty()) return false;
    const Segment* next = &segments_.back();
    if (next->sequence <= after) return false;
    if (after >= 0) {
        for (const Segment& s : segments_) {
            if (s.sequence == after + 1) {
                next = &s;
                break;
            }
        }
    }
    out.sequence = next->sequence;
    out.bytes = next->bytes;
    return true;
}

std::string LiveSegments::playlist() const {
    std::lock_guard<std::mutex> hold(lock_);
    if (!init_ || segments_.empty()) return {};
    double durations[hls::kWindow] = {};
    int count = 0;
    for (const Segment& s : segments_) {
        if (count == hls::kWindow) break;   // never more than the window is kept
        durations[count++] = s.seconds;
    }
    // The names are string literals, so data() is NUL-terminated.
    return hls::playlist(segments_.front().sequence, durations, count, share::kInitName.data(),
                         share::kSegmentPrefix.data(), share::kSegmentSuffix.data());
}

int64_t LiveSegments::msSinceLastSegment() const {
    std::lock_guard<std::mutex> hold(lock_);
    return cut_ ? nowMs_() - lastCutMs_ : 0;
}

int64_t LiveSegments::servedAsMuxed() const {
    std::lock_guard<std::mutex> hold(lock_);
    return servedAsMuxed_;
}

bool LiveSegments::broken() const {
    std::lock_guard<std::mutex> hold(lock_);
    return broken_;
}

}  // namespace geode::cast
