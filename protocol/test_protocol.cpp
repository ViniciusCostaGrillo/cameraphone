#include "camphone_protocol.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace camphone;

void test_roundtrip_serialization() {
    PacketHeader hdr{};
    hdr.magic = kMagicBytes;
    hdr.version = kProtocolVersion;
    hdr.stream_type = StreamType::Video;
    hdr.flags = FlagKeyframe | FlagHdr10Bit;
    hdr.pts_us = 0x0102030405060708ULL;
    hdr.payload_size = 512000;

    std::array<uint8_t, kHeaderSize> wire{};
    serialize_header(hdr, wire);

    // Verify Big-Endian byte order on wire
    assert(wire[0] == 0x43 && wire[1] == 0x50); // 'C', 'P'
    assert(wire[2] == 0x01);                    // version 1
    assert(wire[3] == 0x01);                    // Video
    assert(wire[4] == 0x01);                    // pts_us MSB

    PacketHeader decoded{};
    ParseStatus st = deserialize_header(wire, decoded);
    assert(st == ParseStatus::Ok);
    assert(decoded.magic == kMagicBytes);
    assert(decoded.version == kProtocolVersion);
    assert(decoded.stream_type == StreamType::Video);
    assert(decoded.flags == (FlagKeyframe | FlagHdr10Bit));
    assert(decoded.pts_us == 0x0102030405060708ULL);
    assert(decoded.payload_size == 512000U);
}

void test_rejects_bad_magic_and_oversized_payload() {
    PacketHeader hdr{};
    hdr.magic = 0xdead;
    hdr.version = kProtocolVersion;
    hdr.stream_type = StreamType::Video;
    hdr.payload_size = 1024;

    std::array<uint8_t, kHeaderSize> wire{};
    serialize_header(hdr, wire);

    PacketHeader decoded{};
    assert(deserialize_header(wire, decoded) == ParseStatus::InvalidMagic);

    // Oversized payload > 8 MiB (DoS protection)
    hdr.magic = kMagicBytes;
    hdr.payload_size = kMaxPayloadBytes + 1;
    serialize_header(hdr, wire);
    assert(deserialize_header(wire, decoded) == ParseStatus::PayloadTooLarge);
}

void test_constant_time_token_equals() {
    assert(constant_time_equals("482910", "482910") == true);
    assert(constant_time_equals("482910", "482911") == false);
    assert(constant_time_equals("482910", "482") == false);
    assert(constant_time_equals("", "") == false); // Empty tokens never allowed
}

void test_bounded_queue_never_drops_codec_config() {
    BoundedFrameQueue queue(3);
    queue.push(FramePacket{StreamType::Video, FlagCodecConfig, 0, {0x00, 0x00, 0x00, 0x01, 0x67}});
    queue.push(FramePacket{StreamType::Video, FlagKeyframe, 1000, {0x01}});
    queue.push(FramePacket{StreamType::Video, 0, 2000, {0x02}});
    queue.push(FramePacket{StreamType::Video, 0, 3000, {0x03}}); // triggers drop of oldest non-config frame (pts=1000)

    assert(queue.size() == 3);
    auto first = queue.pop();
    assert(first.has_value());
    assert((first->flags & FlagCodecConfig) != 0); // Codec config (SPS/PPS/VPS) survived!
    auto second = queue.pop();
    assert(second.has_value());
    assert(second->pts_us == 2000ULL);
}

int main() {
    test_roundtrip_serialization();
    test_rejects_bad_magic_and_oversized_payload();
    test_constant_time_token_equals();
    test_bounded_queue_never_drops_codec_config();
    std::cout << "[PASS] All CamPhoneWire v1 protocol & security tests passed.\n";
    return 0;
}
