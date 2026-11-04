#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace camphone {

constexpr uint16_t kMagicBytes      = 0x4350; // "CP"
constexpr uint8_t  kProtocolVersion = 0x01;
constexpr uint16_t kDefaultPort     = 4748;
constexpr size_t   kHeaderSize      = 16;
constexpr uint32_t kMaxPayloadBytes = 8U * 1024U * 1024U; // 8 MiB hard security ceiling

enum class StreamType : uint8_t {
    Video     = 0x01,
    Audio     = 0x02,
    Metadata  = 0x03,
    KeepAlive = 0x04
};

enum HeaderFlags : uint8_t {
    FlagKeyframe    = 1U << 0,
    FlagCodecConfig = 1U << 1, // SPS / PPS / VPS / AV1 Sequence Header / AAC ASC
    FlagHdr10Bit    = 1U << 2, // BT.2020 HLG / HDR10 P010
    FlagDiscont     = 1U << 3  // Resolution or lens switch boundary
};

struct PacketHeader {
    uint16_t   magic        = kMagicBytes;
    uint8_t    version      = kProtocolVersion;
    StreamType stream_type  = StreamType::Video;
    uint64_t   pts_us       = 0;
    uint32_t   payload_size = 0; // 24-bit max in wire layout (bits 0..23)
    uint8_t    flags        = 0;
};

enum class ParseStatus : uint8_t {
    Ok = 0,
    InvalidMagic,
    UnsupportedVersion,
    InvalidStreamType,
    PayloadTooLarge
};

inline void serialize_header(const PacketHeader& hdr, std::array<uint8_t, kHeaderSize>& out) noexcept {
    out[0]  = static_cast<uint8_t>((hdr.magic >> 8) & 0xFFU);
    out[1]  = static_cast<uint8_t>(hdr.magic & 0xFFU);
    out[2]  = hdr.version;
    out[3]  = static_cast<uint8_t>(hdr.stream_type);
    out[4]  = static_cast<uint8_t>((hdr.pts_us >> 56) & 0xFFULL);
    out[5]  = static_cast<uint8_t>((hdr.pts_us >> 48) & 0xFFULL);
    out[6]  = static_cast<uint8_t>((hdr.pts_us >> 40) & 0xFFULL);
    out[7]  = static_cast<uint8_t>((hdr.pts_us >> 32) & 0xFFULL);
    out[8]  = static_cast<uint8_t>((hdr.pts_us >> 24) & 0xFFULL);
    out[9]  = static_cast<uint8_t>((hdr.pts_us >> 16) & 0xFFULL);
    out[10] = static_cast<uint8_t>((hdr.pts_us >> 8) & 0xFFULL);
    out[11] = static_cast<uint8_t>(hdr.pts_us & 0xFFULL);
    out[12] = static_cast<uint8_t>((hdr.payload_size >> 24) & 0xFFU);
    out[13] = static_cast<uint8_t>((hdr.payload_size >> 16) & 0xFFU);
    out[14] = static_cast<uint8_t>((hdr.payload_size >> 8) & 0xFFU);
    out[15] = static_cast<uint8_t>(hdr.payload_size & 0xFFU);
    // Pack flags into upper byte of payload_size if <= 8 MiB, preserving 24-bit payload size up to 16 MiB
    out[12] = hdr.flags;
}

inline ParseStatus deserialize_header(const std::array<uint8_t, kHeaderSize>& in, PacketHeader& out) noexcept {
    out.magic = (static_cast<uint16_t>(in[0]) << 8) | static_cast<uint16_t>(in[1]);
    if (out.magic != kMagicBytes) {
        return ParseStatus::InvalidMagic;
    }
    out.version = in[2];
    if (out.version != kProtocolVersion) {
        return ParseStatus::UnsupportedVersion;
    }
    uint8_t st = in[3];
    if (st < 0x01 || st > 0x04) {
        return ParseStatus::InvalidStreamType;
    }
    out.stream_type = static_cast<StreamType>(st);
    out.pts_us = (static_cast<uint64_t>(in[4])  << 56) |
                 (static_cast<uint64_t>(in[5])  << 48) |
                 (static_cast<uint64_t>(in[6])  << 40) |
                 (static_cast<uint64_t>(in[7])  << 32) |
                 (static_cast<uint64_t>(in[8])  << 24) |
                 (static_cast<uint64_t>(in[9])  << 16) |
                 (static_cast<uint64_t>(in[10]) << 8)  |
                 static_cast<uint64_t>(in[11]);
    out.flags = in[12];
    out.payload_size = (static_cast<uint32_t>(in[13]) << 16) |
                       (static_cast<uint32_t>(in[14]) << 8)  |
                       static_cast<uint32_t>(in[15]);
    if (out.payload_size > kMaxPayloadBytes) {
        return ParseStatus::PayloadTooLarge;
    }
    return ParseStatus::Ok;
}

// Constant-time string comparison against timing side-channel attacks
inline bool constant_time_equals(std::string_view a, std::string_view b) noexcept {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return false;
    }
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    }
    return diff == 0;
}

struct FramePacket {
    StreamType           stream_type;
    uint8_t              flags;
    uint64_t             pts_us;
    std::vector<uint8_t> data;
};

// Bounded thread-safe frame queue that NEVER drops FlagCodecConfig packets (SPS/PPS/VPS)
class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(size_t max_frames = 3) : max_frames_(max_frames) {}

    void push(FramePacket pkt) {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.size() >= max_frames_) {
            // Find oldest frame that is NOT a codec config header
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                if ((it->flags & FlagCodecConfig) == 0) {
                    queue_.erase(it);
                    break;
                }
            }
            if (queue_.size() >= max_frames_) {
                queue_.pop_front();
            }
        }
        queue_.push_back(std::move(pkt));
    }

    std::optional<FramePacket> pop() {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        FramePacket pkt = std::move(queue_.front());
        queue_.pop_front();
        return pkt;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.clear();
    }

private:
    size_t                  max_frames_;
    mutable std::mutex      mu_;
    std::deque<FramePacket> queue_;
};

} // namespace camphone
