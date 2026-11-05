#include "media-decoder.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace camphone {

// Minimal FFmpeg C ABI definitions matching OBS Studio's bundled avcodec-62.dll / avutil-60.dll
enum AVCodecID {
    AV_CODEC_ID_H264 = 27,
    AV_CODEC_ID_HEVC = 173,
    AV_CODEC_ID_AV1  = 225,
    AV_CODEC_ID_AAC  = 86018
};

struct AVFramePartial {
    uint8_t* data[8];
    int      linesize[8];
    uint8_t** extended_data;
    int      width, height;
    int      nb_samples;
    int      format;
};

struct AVPacketPartial {
    void*    buf;
    int64_t  pts;
    int64_t  dts;
    uint8_t* data;
    int      size;
};

using fn_avcodec_find_decoder = void* (*)(int id);
using fn_avcodec_alloc_context3 = void* (*)(const void* codec);
using fn_avcodec_open2 = int (*)(void* ctx, const void* codec, void** options);
using fn_avcodec_send_packet = int (*)(void* ctx, const void* pkt);
using fn_avcodec_receive_frame = int (*)(void* ctx, void* frame);
using fn_avcodec_free_context = void (*)(void** ctx);
using fn_av_packet_alloc = void* (*)();
using fn_av_packet_free = void (*)(void** pkt);
using fn_av_frame_alloc = void* (*)();
using fn_av_frame_free = void (*)(void** frame);

MediaDecoder::MediaDecoder() {
#ifdef _WIN32
    avutil_dll_  = reinterpret_cast<void*>(LoadLibraryA("avutil-60.dll"));
    avcodec_dll_ = reinterpret_cast<void*>(LoadLibraryA("avcodec-62.dll"));
#endif
}

MediaDecoder::~MediaDecoder() {
    shutdown();
#ifdef _WIN32
    if (avcodec_dll_) FreeLibrary(reinterpret_cast<HMODULE>(avcodec_dll_));
    if (avutil_dll_)  FreeLibrary(reinterpret_cast<HMODULE>(avutil_dll_));
#endif
}

bool MediaDecoder::initialize_video(const std::string& codec_name) {
#ifdef _WIN32
    if (!avcodec_dll_ || !avutil_dll_) return false;
    auto find_dec = reinterpret_cast<fn_avcodec_find_decoder>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_find_decoder"));
    auto alloc_ctx = reinterpret_cast<fn_avcodec_alloc_context3>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_alloc_context3"));
    auto open2 = reinterpret_cast<fn_avcodec_open2>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_open2"));
    auto pkt_alloc = reinterpret_cast<fn_av_packet_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "av_packet_alloc"));
    auto frm_alloc = reinterpret_cast<fn_av_frame_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avutil_dll_), "av_frame_alloc"));
    if (!find_dec || !alloc_ctx || !open2 || !pkt_alloc || !frm_alloc) return false;

    int id = AV_CODEC_ID_HEVC;
    if (codec_name == "av1") id = AV_CODEC_ID_AV1;
    else if (codec_name == "avc") id = AV_CODEC_ID_H264;

    void* codec = find_dec(id);
    if (!codec) return false;
    v_ctx_ = alloc_ctx(codec);
    if (!v_ctx_ || open2(v_ctx_, codec, nullptr) < 0) return false;
    v_pkt_ = pkt_alloc();
    v_frame_ = frm_alloc();
    return v_pkt_ != nullptr && v_frame_ != nullptr;
#else
    (void)codec_name;
    return false;
#endif
}

bool MediaDecoder::decode_video_packet(const FramePacket& pkt, const std::function<void(const DecodedVideoFrame&)>& on_frame) {
#ifdef _WIN32
    if (!v_ctx_ || !v_pkt_ || !v_frame_) return false;
    auto send_pkt = reinterpret_cast<fn_avcodec_send_packet>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_send_packet"));
    auto recv_frm = reinterpret_cast<fn_avcodec_receive_frame>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_receive_frame"));
    if (!send_pkt || !recv_frm) return false;

    auto* avpkt = reinterpret_cast<AVPacketPartial*>(v_pkt_);
    avpkt->data = const_cast<uint8_t*>(pkt.data.data());
    avpkt->size = static_cast<int>(pkt.data.size());
    avpkt->pts  = static_cast<int64_t>(pkt.pts_us);

    if (send_pkt(v_ctx_, v_pkt_) < 0) return false;
    while (recv_frm(v_ctx_, v_frame_) == 0) {
        auto* f = reinterpret_cast<AVFramePartial*>(v_frame_);
        DecodedVideoFrame out{};
        out.width = f->width;
        out.height = f->height;
        out.pts_ns = pkt.pts_us * 1000ULL;
        out.is_hdr_p010 = (pkt.flags & FlagHdr10Bit) != 0;
        for (int i = 0; i < 4; ++i) {
            out.data[i] = f->data[i];
            out.linesize[i] = f->linesize[i];
        }
        on_frame(out);
    }
    return true;
#else
    (void)pkt;
    (void)on_frame;
    return false;
#endif
}

bool MediaDecoder::initialize_audio() {
#ifdef _WIN32
    if (!avcodec_dll_ || !avutil_dll_) return false;
    auto find_dec = reinterpret_cast<fn_avcodec_find_decoder>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_find_decoder"));
    auto alloc_ctx = reinterpret_cast<fn_avcodec_alloc_context3>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_alloc_context3"));
    auto open2 = reinterpret_cast<fn_avcodec_open2>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_open2"));
    auto pkt_alloc = reinterpret_cast<fn_av_packet_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "av_packet_alloc"));
    auto frm_alloc = reinterpret_cast<fn_av_frame_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avutil_dll_), "av_frame_alloc"));
    if (!find_dec || !alloc_ctx || !open2 || !pkt_alloc || !frm_alloc) return false;

    void* codec = find_dec(AV_CODEC_ID_AAC);
    if (!codec) return false;
    a_ctx_ = alloc_ctx(codec);
    if (!a_ctx_ || open2(a_ctx_, codec, nullptr) < 0) return false;
    a_pkt_ = pkt_alloc();
    a_frame_ = frm_alloc();
    return a_pkt_ != nullptr && a_frame_ != nullptr;
#else
    return false;
#endif
}

bool MediaDecoder::decode_audio_packet(const FramePacket& pkt, const std::function<void(const DecodedAudioFrame&)>& on_frame) {
#ifdef _WIN32
    if (!a_ctx_ || !a_pkt_ || !a_frame_) return false;
    auto send_pkt = reinterpret_cast<fn_avcodec_send_packet>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_send_packet"));
    auto recv_frm = reinterpret_cast<fn_avcodec_receive_frame>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_receive_frame"));
    if (!send_pkt || !recv_frm) return false;

    auto* avpkt = reinterpret_cast<AVPacketPartial*>(a_pkt_);
    avpkt->data = const_cast<uint8_t*>(pkt.data.data());
    avpkt->size = static_cast<int>(pkt.data.size());
    avpkt->pts  = static_cast<int64_t>(pkt.pts_us);

    if (send_pkt(a_ctx_, a_pkt_) < 0) return false;
    while (recv_frm(a_ctx_, a_frame_) == 0) {
        auto* f = reinterpret_cast<AVFramePartial*>(a_frame_);
        DecodedAudioFrame out{};
        out.nb_samples = f->nb_samples;
        out.pts_ns = pkt.pts_us * 1000ULL;
        out.data[0] = f->data[0];
        out.data[1] = f->data[1];
        on_frame(out);
    }
    return true;
#else
    (void)pkt;
    (void)on_frame;
    return false;
#endif
}

void MediaDecoder::shutdown() {
#ifdef _WIN32
    if (!avcodec_dll_ || !avutil_dll_) return;
    auto free_ctx = reinterpret_cast<fn_avcodec_free_context>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_free_context"));
    auto free_pkt = reinterpret_cast<fn_av_packet_free>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "av_packet_free"));
    auto free_frm = reinterpret_cast<fn_av_frame_free>(GetProcAddress(reinterpret_cast<HMODULE>(avutil_dll_), "av_frame_free"));
    if (v_pkt_ && free_pkt) free_pkt(&v_pkt_);
    if (v_frame_ && free_frm) free_frm(&v_frame_);
    if (v_ctx_ && free_ctx) free_ctx(&v_ctx_);
    if (a_pkt_ && free_pkt) free_pkt(&a_pkt_);
    if (a_frame_ && free_frm) free_frm(&a_frame_);
    if (a_ctx_ && free_ctx) free_ctx(&a_ctx_);
#endif
}

} // namespace camphone
