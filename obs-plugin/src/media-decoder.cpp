#include "media-decoder.hpp"
#include "diagnostic-logger.hpp"

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

// Exact FFmpeg 7.x / avutil-60 AVFrame layout verified via REA + Ghidra on droidcam-obs.dll (FUN_180100c30):
//   0x00: uint8_t* data[8]         (64 bytes)
//   0x40: int linesize[8]          (32 bytes)
//   0x60: uint8_t** extended_data  (8 bytes)
//   0x68: int width                (4 bytes)
//   0x6C: int height               (4 bytes)
//   0x70: int nb_samples           (4 bytes)
//   0x74: int format               (4 bytes) -> AVPixelFormat / AVSampleFormat
struct AVFramePartial {
    uint8_t* data[8];
    int      linesize[8];
    uint8_t** extended_data;
    int      width;
    int      height;
    int      nb_samples;
    int      format;
};
static_assert(offsetof(AVFramePartial, width) == 0x68, "AVFrame.width must be at offset 0x68");
static_assert(offsetof(AVFramePartial, height) == 0x6C, "AVFrame.height must be at offset 0x6C");
static_assert(offsetof(AVFramePartial, format) == 0x74, "AVFrame.format must be at offset 0x74");

struct AVPacketPartial {
    void*    buf;
    int64_t  pts;
    int64_t  dts;
    uint8_t* data;
    int      size;
};

using fn_avcodec_find_decoder = void* (*)(int id);
using fn_avcodec_find_decoder_by_name = void* (*)(const char* name);
using fn_avcodec_alloc_context3 = void* (*)(const void* codec);
using fn_avcodec_open2 = int (*)(void* ctx, const void* codec, void** options);
using fn_avcodec_send_packet = int (*)(void* ctx, const void* pkt);
using fn_avcodec_receive_frame = int (*)(void* ctx, void* frame);
using fn_avcodec_free_context = void (*)(void** ctx);
using fn_av_packet_alloc = void* (*)();
using fn_av_packet_free = void (*)(void** pkt);
using fn_av_frame_alloc = void* (*)();
using fn_av_frame_free = void (*)(void** frame);

static void* load_obs_ffmpeg_dll(const char* dll_name) {
#ifdef _WIN32
    if (HMODULE h = GetModuleHandleA(dll_name)) {
        return reinterpret_cast<void*>(h);
    }
    if (HMODULE h = LoadLibraryA(dll_name)) {
        return reinterpret_cast<void*>(h);
    }
    // Check directory of loaded obs.dll first (supports custom OBS install paths)
    if (HMODULE hObs = GetModuleHandleA("obs.dll")) {
        char obs_path[MAX_PATH] = {0};
        if (GetModuleFileNameA(hObs, obs_path, MAX_PATH) > 0) {
            std::string dir(obs_path);
            auto slash = dir.find_last_of("\\/");
            if (slash != std::string::npos) {
                std::string candidate = dir.substr(0, slash + 1) + dll_name;
                if (HMODULE h = LoadLibraryExA(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
                    return reinterpret_cast<void*>(h);
                }
            }
        }
    }
    std::string full_path = std::string("C:\\Program Files\\obs-studio\\bin\\64bit\\") + dll_name;
    if (HMODULE h = LoadLibraryExA(full_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
        return reinterpret_cast<void*>(h);
    }
#else
    (void)dll_name;
#endif
    return nullptr;
}

MediaDecoder::MediaDecoder() {
#ifdef _WIN32
    avutil_dll_  = load_obs_ffmpeg_dll("avutil-60.dll");
    avcodec_dll_ = load_obs_ffmpeg_dll("avcodec-62.dll");
    if (!avutil_dll_ || !avcodec_dll_) {
        DiagnosticLogger::error("MediaDecoder", "Failed to load avutil-60.dll or avcodec-62.dll from OBS Studio!");
    }
#endif
}

MediaDecoder::~MediaDecoder() {
    shutdown();
}

static std::string detect_annexb_codec(const std::vector<uint8_t>& buf) {
    for (size_t i = 0; i + 4 < buf.size() && i < 64; ++i) {
        size_t nal_idx = 0;
        if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
            nal_idx = i + 4;
        } else if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
            nal_idx = i + 3;
        }
        if (nal_idx > 0 && nal_idx < buf.size()) {
            uint8_t b = buf[nal_idx];
            uint8_t h264_type = b & 0x1F;
            uint8_t hevc_type = (b >> 1) & 0x3F;
            // HEVC VPS (32), SPS (33), PPS (34) have b=0x40, 0x42, 0x44 where h264_type is 0, 2, 4
            if (hevc_type == 32 || hevc_type == 33 || hevc_type == 34) {
                return "hevc";
            }
            // H.264 SPS (7), PPS (8) have b=0x67/0x27, 0x68/0x28 where hevc_type is 19/51, 20/52
            if ((b & 0x80) == 0 && (h264_type == 7 || h264_type == 8)) {
                return "avc";
            }
        }
    }
    return "";
}

bool MediaDecoder::initialize_video(const std::string& codec_name) {
#ifdef _WIN32
    if (!avcodec_dll_ || !avutil_dll_) {
        DiagnosticLogger::error("MediaDecoder", "Cannot initialize video: FFmpeg DLLs not loaded.");
        return false;
    }
    auto find_dec = reinterpret_cast<fn_avcodec_find_decoder>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_find_decoder"));
    auto find_dec_name = reinterpret_cast<fn_avcodec_find_decoder_by_name>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_find_decoder_by_name"));
    auto alloc_ctx = reinterpret_cast<fn_avcodec_alloc_context3>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_alloc_context3"));
    auto open2 = reinterpret_cast<fn_avcodec_open2>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_open2"));
    auto pkt_alloc = reinterpret_cast<fn_av_packet_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "av_packet_alloc"));
    auto frm_alloc = reinterpret_cast<fn_av_frame_alloc>(GetProcAddress(reinterpret_cast<HMODULE>(avutil_dll_), "av_frame_alloc"));
    auto free_ctx = reinterpret_cast<fn_avcodec_free_context>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "avcodec_free_context"));
    auto free_pkt = reinterpret_cast<fn_av_packet_free>(GetProcAddress(reinterpret_cast<HMODULE>(avcodec_dll_), "av_packet_free"));
    auto free_frm = reinterpret_cast<fn_av_frame_free>(GetProcAddress(reinterpret_cast<HMODULE>(avutil_dll_), "av_frame_free"));
    if (!find_dec || !alloc_ctx || !open2 || !pkt_alloc || !frm_alloc) {
        DiagnosticLogger::error("MediaDecoder", "Failed to resolve avcodec/avutil exports.");
        return false;
    }

    if (v_pkt_ && free_pkt) free_pkt(&v_pkt_);
    if (v_frame_ && free_frm) free_frm(&v_frame_);
    if (v_ctx_ && free_ctx) free_ctx(&v_ctx_);

    int id = AV_CODEC_ID_HEVC;
    const char* ffmpeg_name = "hevc";
    if (codec_name == "av1") {
        id = AV_CODEC_ID_AV1;
        ffmpeg_name = "av1";
    } else if (codec_name == "avc" || codec_name == "h264") {
        id = AV_CODEC_ID_H264;
        ffmpeg_name = "h264";
    }

    void* codec = find_dec_name ? find_dec_name(ffmpeg_name) : nullptr;
    if (!codec) {
        codec = find_dec(id);
    }
    if (!codec) {
        DiagnosticLogger::error("MediaDecoder", "avcodec_find_decoder failed for codec=" + codec_name);
        return false;
    }
    v_ctx_ = alloc_ctx(codec);
    if (!v_ctx_ || open2(v_ctx_, codec, nullptr) < 0) {
        DiagnosticLogger::error("MediaDecoder", "avcodec_open2 failed for codec=" + codec_name);
        return false;
    }
    v_pkt_ = pkt_alloc();
    v_frame_ = frm_alloc();
    decoded_frames_count_ = 0;
    active_codec_name_ = (codec_name == "h264") ? "avc" : codec_name;
    DiagnosticLogger::info("MediaDecoder", "Initialized FFmpeg video decoder for codec=" + active_codec_name_ + " (" + ffmpeg_name + ")");
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

    if ((pkt.flags & FlagCodecConfig) != 0 || (pkt.flags & FlagKeyframe) != 0 || decoded_frames_count_ == 0) {
        std::string detected = detect_annexb_codec(pkt.data);
        if (!detected.empty() && detected != active_codec_name_) {
            DiagnosticLogger::warn("MediaDecoder", "Auto-switching video decoder from " + active_codec_name_ + " to " + detected + " based on Annex-B NAL header");
            initialize_video(detected);
        }
    }

    if ((pkt.flags & FlagCodecConfig) != 0 && !pkt.data.empty()) {
        codec_config_ = pkt.data;
    }

    std::vector<uint8_t> combined;
    const uint8_t* send_ptr = pkt.data.data();
    int send_size = static_cast<int>(pkt.data.size());

    if ((pkt.flags & FlagKeyframe) != 0 && (pkt.flags & FlagCodecConfig) == 0 && !codec_config_.empty()) {
        bool already_has_config = pkt.data.size() >= codec_config_.size() &&
            std::equal(codec_config_.begin(), codec_config_.end(), pkt.data.begin());
        if (!already_has_config) {
            combined.reserve(codec_config_.size() + pkt.data.size());
            combined.insert(combined.end(), codec_config_.begin(), codec_config_.end());
            combined.insert(combined.end(), pkt.data.begin(), pkt.data.end());
            send_ptr = combined.data();
            send_size = static_cast<int>(combined.size());
        }
    }

    auto* avpkt = reinterpret_cast<AVPacketPartial*>(v_pkt_);
    avpkt->data = const_cast<uint8_t*>(send_ptr);
    avpkt->size = send_size;
    avpkt->pts  = static_cast<int64_t>(pkt.pts_us);
    avpkt->dts  = static_cast<int64_t>(pkt.pts_us);

    int ret = send_pkt(v_ctx_, v_pkt_);
    if (ret < 0) {
        DiagnosticLogger::warn("MediaDecoder", "avcodec_send_packet returned " + std::to_string(ret) + " (pkt size=" + std::to_string(pkt.data.size()) + ")");
        return false;
    }
    while (recv_frm(v_ctx_, v_frame_) == 0) {
        auto* f = reinterpret_cast<AVFramePartial*>(v_frame_);
        DecodedVideoFrame out{};
        out.width = f->width;
        out.height = f->height;
        out.pts_ns = pkt.pts_us * 1000ULL;
        out.av_pix_fmt = f->format;
        out.is_hdr_p010 = (pkt.flags & FlagHdr10Bit) != 0;
        for (int i = 0; i < 8; ++i) {
            out.data[i] = f->data[i];
            out.linesize[i] = f->linesize[i];
        }
        ++decoded_frames_count_;
        if (decoded_frames_count_ == 1 || decoded_frames_count_ % 120 == 0) {
            DiagnosticLogger::info(
                "MediaDecoder",
                "Decoded video frame #" + std::to_string(decoded_frames_count_) +
                " (" + std::to_string(out.width) + "x" + std::to_string(out.height) +
                ", av_pix_fmt=" + std::to_string(out.av_pix_fmt) +
                ", linesize0=" + std::to_string(out.linesize[0]) + ")"
            );
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
    v_pkt_ = nullptr;
    v_frame_ = nullptr;
    v_ctx_ = nullptr;
    a_pkt_ = nullptr;
    a_frame_ = nullptr;
    a_ctx_ = nullptr;
#endif
}

} // namespace camphone
