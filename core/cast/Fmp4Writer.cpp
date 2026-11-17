#include "cast/Fmp4Writer.hpp"

#include <algorithm>
#include <utility>

namespace geode::cast {

namespace {

using Buf = std::vector<uint8_t>;

constexpr uint32_t kVideoTrack = 1;
constexpr uint32_t kAudioTrack = 2;
constexpr uint8_t kNalSps = 7;
constexpr uint8_t kNalPps = 8;
constexpr uint8_t kNalDelimiter = 9;
constexpr size_t kMaxAsc = 64;

void put8(Buf& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v)); }
void put16(Buf& b, uint32_t v) {
    put8(b, v >> 8);
    put8(b, v);
}
void put24(Buf& b, uint32_t v) {
    put8(b, v >> 16);
    put16(b, v);
}
void put32(Buf& b, uint32_t v) {
    put16(b, v >> 16);
    put16(b, v);
}
void put64(Buf& b, uint64_t v) {
    put32(b, static_cast<uint32_t>(v >> 32));
    put32(b, static_cast<uint32_t>(v));
}
void putZeros(Buf& b, size_t n) { b.insert(b.end(), n, 0); }
void patch32(Buf& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

// Opens a box: its size is patched by close().
size_t open(Buf& b, const char* type) {
    const size_t at = b.size();
    put32(b, 0);
    b.insert(b.end(), type, type + 4);
    return at;
}
size_t openFull(Buf& b, const char* type, uint32_t version, uint32_t flags) {
    const size_t at = open(b, type);
    put32(b, (version << 24) | (flags & 0xFFFFFF));
    return at;
}
void close(Buf& b, size_t at) { patch32(b, at, static_cast<uint32_t>(b.size() - at)); }

void putMatrix(Buf& b) {
    for (uint32_t v : {0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u}) put32(b, v);
}

void putTkhd(Buf& b, uint32_t id, bool audio, uint32_t width, uint32_t height) {
    const size_t at = openFull(b, "tkhd", 0, 0x000003);   // enabled, in the movie
    put32(b, 0);
    put32(b, 0);
    put32(b, id);
    put32(b, 0);
    put32(b, 0);   // duration: unknown, it is live
    putZeros(b, 8);
    put16(b, 0);   // layer
    put16(b, 0);   // alternate group
    put16(b, audio ? 0x0100 : 0);
    put16(b, 0);
    putMatrix(b);
    put32(b, width << 16);
    put32(b, height << 16);
    close(b, at);
}

void putMdhd(Buf& b, uint32_t timescale) {
    const size_t at = openFull(b, "mdhd", 0, 0);
    put32(b, 0);
    put32(b, 0);
    put32(b, timescale);
    put32(b, 0);
    put16(b, 0x55C4);   // "und"
    put16(b, 0);
    close(b, at);
}

void putHdlr(Buf& b, const char* handler, const char* name) {
    const size_t at = openFull(b, "hdlr", 0, 0);
    put32(b, 0);
    b.insert(b.end(), handler, handler + 4);
    putZeros(b, 12);
    for (const char* c = name; *c; ++c) put8(b, static_cast<uint8_t>(*c));
    put8(b, 0);
    close(b, at);
}

void putDinf(Buf& b) {
    const size_t dinf = open(b, "dinf");
    const size_t dref = openFull(b, "dref", 0, 0);
    put32(b, 1);
    close(b, openFull(b, "url ", 0, 0x000001));   // the media is in this file
    close(b, dref);
    close(b, dinf);
}

// stts, stsc, stsz and stco with no samples: every sample is in a fragment.
void putEmptyTables(Buf& b) {
    for (const char* type : {"stts", "stsc", "stco"}) {
        const size_t at = openFull(b, type, 0, 0);
        put32(b, 0);
        close(b, at);
    }
    const size_t stsz = openFull(b, "stsz", 0, 0);
    put32(b, 0);
    put32(b, 0);
    close(b, stsz);
}

void putTrex(Buf& b, uint32_t id) {
    const size_t at = openFull(b, "trex", 0, 0);
    put32(b, id);
    put32(b, 1);
    put32(b, 0);
    put32(b, 0);
    put32(b, 0);
    close(b, at);
}

}  // namespace

Fmp4Writer::Fmp4Writer(const Tracks& tracks, int64_t fragmentUs, Sink sink)
    : tracks_(tracks), fragmentUs_(fragmentUs > 0 ? fragmentUs : 1), sink_(std::move(sink)) {}

std::vector<std::pair<const uint8_t*, size_t>> Fmp4Writer::nalUnits(const uint8_t* data, size_t size) {
    std::vector<std::pair<const uint8_t*, size_t>> out;
    if (!data || size == 0) return out;
    // Where each start code's payload begins, and where its start code began.
    std::vector<std::pair<size_t, size_t>> starts;
    for (size_t i = 0; i + 3 <= size;) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            const size_t codeAt = (i > 0 && data[i - 1] == 0) ? i - 1 : i;
            starts.emplace_back(i + 3, codeAt);
            i += 3;
        } else {
            ++i;
        }
    }
    if (starts.empty()) {
        out.emplace_back(data, size);
        return out;
    }
    for (size_t k = 0; k < starts.size(); ++k) {
        const size_t begin = starts[k].first;
        size_t end = k + 1 < starts.size() ? starts[k + 1].second : size;
        while (end > begin && k + 1 == starts.size() && data[end - 1] == 0) --end;   // trailing zero bytes
        if (end > begin) out.emplace_back(data + begin, end - begin);
    }
    return out;
}

std::vector<uint8_t> Fmp4Writer::lengthPrefixed(const uint8_t* data, size_t size) {
    Buf out;
    out.reserve(size + 16);
    for (const auto& [nal, n] : nalUnits(data, size)) {
        const uint8_t type = nal[0] & 0x1F;
        if (type == kNalSps || type == kNalPps || type == kNalDelimiter) continue;
        put32(out, static_cast<uint32_t>(n));
        out.insert(out.end(), nal, nal + n);
    }
    return out;
}

bool Fmp4Writer::videoConfig(const uint8_t* data, size_t size) {
    if (started_) return true;   // the init segment is out; a repeat changes nothing
    std::vector<uint8_t> sps;
    std::vector<std::vector<uint8_t>> pps;
    for (const auto& [nal, n] : nalUnits(data, size)) {
        const uint8_t type = nal[0] & 0x1F;
        if (type == kNalSps && sps.empty() && n >= 4 && n <= 0xFFFF) sps.assign(nal, nal + n);
        if (type == kNalPps && n <= 0xFFFF && pps.size() < 31) pps.emplace_back(nal, nal + n);
    }
    if (sps.empty() || pps.empty()) return false;
    sps_ = std::move(sps);
    pps_ = std::move(pps);
    haveVideoConfig_ = true;
    start();
    return true;
}

bool Fmp4Writer::audioConfig(const uint8_t* data, size_t size) {
    if (started_) return true;
    if (!data || size == 0 || size > kMaxAsc) return false;
    asc_.assign(data, data + size);
    haveAudioConfig_ = true;
    start();
    return true;
}

void Fmp4Writer::video(const uint8_t* data, size_t size, int64_t ptsUs, bool key) {
    if (!data || size == 0) return;
    accept({lengthPrefixed(data, size), ptsUs, key, true});
}

void Fmp4Writer::audio(const uint8_t* data, size_t size, int64_t ptsUs) {
    if (!data || size == 0) return;
    accept({Buf(data, data + size), ptsUs, true, false});
}

void Fmp4Writer::start() {
    if (started_ || !haveVideoConfig_ || !haveAudioConfig_) return;
    started_ = true;
    const Buf init = initSegment();
    if (sink_) sink_(init.data(), init.size());
    std::vector<Sample> held = std::move(held_);
    held_.clear();
    for (Sample& s : held) accept(std::move(s));
}

int64_t Fmp4Writer::decodeTime(int64_t ptsUs, uint32_t timescale, int64_t step, int64_t& last) const {
    const int64_t fromStart = std::max<int64_t>(0, ptsUs - originUs_);
    int64_t t = fromStart / 1'000'000 * timescale + (fromStart % 1'000'000) * timescale / 1'000'000;
    if (last >= 0 && t < last + step) t = last + step;
    last = t;
    return t;
}

void Fmp4Writer::accept(Sample&& s) {
    if (s.bytes.empty()) return;
    if (!started_) {
        if (held_.size() < kMaxHeld) {
            held_.push_back(std::move(s));
        } else {
            ++dropped_;
        }
        return;
    }
    if (s.video && !sawKey_) {
        if (!s.key) {
            ++dropped_;   // a fragment opens on a keyframe
            return;
        }
        sawKey_ = true;
    }
    if (!haveOrigin_) {
        haveOrigin_ = true;
        originUs_ = s.ptsUs;
    }
    if (s.video) {
        if (s.key && !videoQ_.empty() && (s.ptsUs - openedUs_) * 10 >= fragmentUs_ * 9) {
            int64_t probe = lastVideo_;
            const int64_t next = decodeTime(s.ptsUs, kVideoTimescale, 1, probe);
            lastVideoDuration_ = next - videoQ_.back().decode;
            writeFragment(videoQ_.size());
        }
        if (videoQ_.size() >= kMaxQueued) {
            lastVideoDuration_ = 1;
            writeFragment(videoQ_.size());   // no keyframe for far too long: bound what is held
        }
        if (videoQ_.empty()) openedUs_ = s.ptsUs;
        const int64_t t = decodeTime(s.ptsUs, kVideoTimescale, 1, lastVideo_);
        videoQ_.push_back({std::move(s.bytes), t, s.key});
    } else {
        if (audioQ_.size() >= kMaxQueued) {
            ++dropped_;   // no picture for far too long: the sound waits no longer
            return;
        }
        const int64_t t = decodeTime(s.ptsUs, tracks_.audioRateHz, kAacFrameSamples, lastAudio_);
        audioQ_.push_back({std::move(s.bytes), t, true});
    }
}

void Fmp4Writer::flush() {
    if (!started_ || (videoQ_.empty() && audioQ_.empty())) return;
    writeFragment(videoQ_.size());
}

void Fmp4Writer::writeFragment(size_t videoCount) {
    struct Run {
        const std::vector<Timed>* samples;
        size_t count;
        uint32_t track;
        int64_t lastDuration;
        size_t offsetField = 0;
    };
    const int64_t videoLast = lastVideoDuration_ > 0 ? lastVideoDuration_ : 1;
    Run runs[2] = {{&videoQ_, videoCount, kVideoTrack, videoLast, 0},
                   {&audioQ_, audioQ_.size(), kAudioTrack, kAacFrameSamples, 0}};

    Buf moof;
    const size_t moofAt = open(moof, "moof");
    const size_t mfhd = openFull(moof, "mfhd", 0, 0);
    put32(moof, static_cast<uint32_t>(++sequence_));
    close(moof, mfhd);
    for (Run& run : runs) {
        if (run.count == 0) continue;
        const std::vector<Timed>& q = *run.samples;
        const size_t traf = open(moof, "traf");
        const size_t tfhd = openFull(moof, "tfhd", 0, 0x020000);   // default-base-is-moof
        put32(moof, run.track);
        close(moof, tfhd);
        const size_t tfdt = openFull(moof, "tfdt", 1, 0);
        put64(moof, static_cast<uint64_t>(q.front().decode));
        close(moof, tfdt);
        // data offset, then each sample's duration, size and flags.
        const size_t trun = openFull(moof, "trun", 0, 0x000701);
        put32(moof, static_cast<uint32_t>(run.count));
        run.offsetField = moof.size();
        put32(moof, 0);
        for (size_t i = 0; i < run.count; ++i) {
            const int64_t duration = i + 1 < run.count ? q[i + 1].decode - q[i].decode : run.lastDuration;
            put32(moof, static_cast<uint32_t>(std::clamp<int64_t>(duration, 0, UINT32_MAX)));
            put32(moof, static_cast<uint32_t>(q[i].bytes.size()));
            put32(moof, q[i].key ? kSyncFlags : kNonSyncFlags);
        }
        close(moof, trun);
        close(moof, traf);
    }
    close(moof, moofAt);

    size_t at = moof.size() + 8;
    for (Run& run : runs) {
        if (run.count == 0) continue;
        patch32(moof, run.offsetField, static_cast<uint32_t>(at));
        for (size_t i = 0; i < run.count; ++i) at += (*run.samples)[i].bytes.size();
    }
    const size_t payload = at - moof.size() - 8;

    Buf out = std::move(moof);
    out.reserve(out.size() + 8 + payload);
    put32(out, static_cast<uint32_t>(8 + payload));
    out.insert(out.end(), {'m', 'd', 'a', 't'});
    for (Run& run : runs)
        for (size_t i = 0; i < run.count; ++i) {
            const Buf& bytes = (*run.samples)[i].bytes;
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
    videoQ_.erase(videoQ_.begin(), videoQ_.begin() + static_cast<std::ptrdiff_t>(videoCount));
    audioQ_.clear();
    if (sink_) sink_(out.data(), out.size());
}

std::vector<uint8_t> Fmp4Writer::initSegment() const {
    Buf b;
    const size_t ftyp = open(b, "ftyp");
    b.insert(b.end(), {'i', 's', 'o', '6'});
    put32(b, 0);
    for (const char* brand : {"iso6", "cmfc", "isom", "avc1", "mp41"}) b.insert(b.end(), brand, brand + 4);
    close(b, ftyp);

    const size_t moov = open(b, "moov");
    const size_t mvhd = openFull(b, "mvhd", 0, 0);
    put32(b, 0);
    put32(b, 0);
    put32(b, 1000);
    put32(b, 0);
    put32(b, 0x00010000);   // rate 1.0
    put16(b, 0x0100);       // volume 1.0
    putZeros(b, 10);
    putMatrix(b);
    putZeros(b, 24);
    put32(b, kAudioTrack + 1);
    close(b, mvhd);

    // The video track.
    {
        const size_t trak = open(b, "trak");
        putTkhd(b, kVideoTrack, false, tracks_.width, tracks_.height);
        const size_t mdia = open(b, "mdia");
        putMdhd(b, kVideoTimescale);
        putHdlr(b, "vide", "Video");
        const size_t minf = open(b, "minf");
        const size_t vmhd = openFull(b, "vmhd", 0, 0x000001);
        putZeros(b, 8);
        close(b, vmhd);
        putDinf(b);
        const size_t stbl = open(b, "stbl");
        const size_t stsd = openFull(b, "stsd", 0, 0);
        put32(b, 1);
        const size_t avc1 = open(b, "avc1");
        putZeros(b, 6);
        put16(b, 1);   // data reference index
        putZeros(b, 16);
        put16(b, tracks_.width);
        put16(b, tracks_.height);
        put32(b, 0x00480000);   // 72 dpi
        put32(b, 0x00480000);
        put32(b, 0);
        put16(b, 1);   // frame count
        putZeros(b, 32);   // compressor name
        put16(b, 0x0018);
        put16(b, 0xFFFF);
        const size_t avcC = open(b, "avcC");
        put8(b, 1);
        put8(b, sps_[1]);   // profile
        put8(b, sps_[2]);   // constraint flags
        put8(b, sps_[3]);   // level
        put8(b, 0xFF);      // 4-byte NAL lengths
        put8(b, 0xE1);      // one SPS
        put16(b, static_cast<uint32_t>(sps_.size()));
        b.insert(b.end(), sps_.begin(), sps_.end());
        put8(b, static_cast<uint32_t>(pps_.size()));
        for (const auto& p : pps_) {
            put16(b, static_cast<uint32_t>(p.size()));
            b.insert(b.end(), p.begin(), p.end());
        }
        const uint8_t profile = sps_[1];
        if (profile == 100 || profile == 110 || profile == 122 || profile == 144) {
            // ISO 14496-15's extension for these profiles: 4:2:0, 8 bits, what a surface encoder puts out.
            put8(b, 0xFC | 1);
            put8(b, 0xF8);
            put8(b, 0xF8);
            put8(b, 0);
        }
        close(b, avcC);
        close(b, avc1);
        close(b, stsd);
        putEmptyTables(b);
        close(b, stbl);
        close(b, minf);
        close(b, mdia);
        close(b, trak);
    }

    // The audio track.
    {
        const size_t trak = open(b, "trak");
        putTkhd(b, kAudioTrack, true, 0, 0);
        const size_t mdia = open(b, "mdia");
        putMdhd(b, tracks_.audioRateHz);
        putHdlr(b, "soun", "Sound");
        const size_t minf = open(b, "minf");
        const size_t smhd = openFull(b, "smhd", 0, 0);
        put32(b, 0);
        close(b, smhd);
        putDinf(b);
        const size_t stbl = open(b, "stbl");
        const size_t stsd = openFull(b, "stsd", 0, 0);
        put32(b, 1);
        const size_t mp4a = open(b, "mp4a");
        putZeros(b, 6);
        put16(b, 1);
        putZeros(b, 8);
        put16(b, tracks_.channels);
        put16(b, 16);
        put16(b, 0);
        put16(b, 0);
        put32(b, (tracks_.audioRateHz & 0xFFFF) << 16);
        const size_t esds = openFull(b, "esds", 0, 0);
        const uint32_t asc = static_cast<uint32_t>(asc_.size());
        const uint32_t decoderConfig = 13 + 2 + asc;
        put8(b, 3);   // ES_Descriptor
        put8(b, 3 + 2 + decoderConfig + 3);
        put16(b, kAudioTrack);
        put8(b, 0);
        put8(b, 4);      // DecoderConfigDescriptor
        put8(b, decoderConfig);
        put8(b, 0x40);   // MPEG-4 audio
        put8(b, 0x15);   // audio stream
        put24(b, 0);
        put32(b, tracks_.audioBitRate);
        put32(b, tracks_.audioBitRate);
        put8(b, 5);      // DecoderSpecificInfo
        put8(b, asc);
        b.insert(b.end(), asc_.begin(), asc_.end());
        put8(b, 6);      // SLConfigDescriptor
        put8(b, 1);
        put8(b, 2);
        close(b, esds);
        close(b, mp4a);
        close(b, stsd);
        putEmptyTables(b);
        close(b, stbl);
        close(b, minf);
        close(b, mdia);
        close(b, trak);
    }

    const size_t mvex = open(b, "mvex");
    putTrex(b, kVideoTrack);
    putTrex(b, kAudioTrack);
    close(b, mvex);
    close(b, moov);
    return b;
}

}  // namespace geode::cast
