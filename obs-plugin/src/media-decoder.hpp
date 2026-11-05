#pragma once

#include "camphone_protocol.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace camphone {

struct DecodedVideoFrame {
    int      width       = 0;
    int      height      = 0;
    uint64_t pts_ns      = 0;
    bool     is_hdr_p010 = false;
    uint8_t* data[4]     = {nullptr, nullptr, nullptr, nullptr};
    int      linesize[4] = {0, 0, 0, 0};
};

struct DecodedAudioFrame {
    int      sample_rate = 48000;
    int      channels    = 2;
    int      nb_samples  = 0;
    uint64_t pts_ns      = 0;
    uint8_t* data[2]     = {nullptr, nullptr};
};

class MediaDecoder {
public:
    MediaDecoder();
    ~MediaDecoder();

    bool initialize_video(const std::string& codec_name);
    bool decode_video_packet(const FramePacket& pkt, const std::function<void(const DecodedVideoFrame&)>& on_frame);

    bool initialize_audio();
    bool decode_audio_packet(const FramePacket& pkt, const std::function<void(const DecodedAudioFrame&)>& on_frame);

    void shutdown();

private:
    void* avcodec_dll_  = nullptr;
    void* avutil_dll_   = nullptr;
    void* v_ctx_        = nullptr;
    void* v_frame_      = nullptr;
    void* v_pkt_        = nullptr;
    void* a_ctx_        = nullptr;
    void* a_frame_      = nullptr;
    void* a_pkt_        = nullptr;
};

} // namespace camphone
