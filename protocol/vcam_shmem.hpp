#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace camphone {

constexpr uint32_t kVCamMagic = 0x43505643; // "CPVC"
constexpr uint32_t kVCamVersion = 1;
constexpr size_t   kVCamSharedMemSize = 32 * 1024 * 1024; // 32 MB (supports up to 4K60 NV12/YUY2)
constexpr const wchar_t* kVCamSharedMemName = L"Local\\CameraPhone_VCam_Shmem";
constexpr const wchar_t* kVCamEventName     = L"Local\\CameraPhone_VCam_Event";

#pragma pack(push, 1)
struct VCamSharedHeader {
    uint32_t magic;         // kVCamMagic (0x43505643)
    uint32_t version;       // kVCamVersion (1)
    uint32_t width;         // Ex: 1920 or 3840
    uint32_t height;        // Ex: 1080 or 2160
    uint32_t fourcc;        // 'NV12' (0x3231564E), 'YUY2' (0x32595559), or 'RV24' (0x34325652)
    uint32_t frame_index;   // Sequentially increasing counter
    uint64_t timestamp_ns;  // Frame timestamp in ns
    uint32_t data_size;     // Byte size of pixel data in buffer
    uint32_t linesize[4];   // Pitch per plane
};
#pragma pack(pop)

class VCamSharedMemoryWriter {
public:
    VCamSharedMemoryWriter() = default;
    ~VCamSharedMemoryWriter() { close(); }

    bool open() {
#ifdef _WIN32
        if (mapped_ptr_) return true;
        map_handle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(kVCamSharedMemSize),
            kVCamSharedMemName
        );
        if (!map_handle_) return false;

        mapped_ptr_ = MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, kVCamSharedMemSize);
        if (!mapped_ptr_) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
            return false;
        }

        event_handle_ = CreateEventW(nullptr, FALSE, FALSE, kVCamEventName);
        return true;
#else
        return false;
#endif
    }

    void close() {
#ifdef _WIN32
        if (mapped_ptr_) {
            UnmapViewOfFile(mapped_ptr_);
            mapped_ptr_ = nullptr;
        }
        if (map_handle_) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
        }
        if (event_handle_) {
            CloseHandle(event_handle_);
            event_handle_ = nullptr;
        }
#endif
    }

    bool is_open() const noexcept {
#ifdef _WIN32
        return mapped_ptr_ != nullptr;
#else
        return false;
#endif
    }

    bool write_frame(uint32_t width, uint32_t height, int av_pix_fmt,
                     const uint8_t* const data[4], const int linesize[4], uint64_t timestamp_ns) {
#ifdef _WIN32
        if (!mapped_ptr_ && !open()) return false;

        auto* hdr = reinterpret_cast<VCamSharedHeader*>(mapped_ptr_);
        uint8_t* payload = reinterpret_cast<uint8_t*>(mapped_ptr_) + sizeof(VCamSharedHeader);

        uint32_t y_size = width * height;
        uint32_t uv_size = width * height / 2;
        uint32_t total_bytes = y_size + uv_size;
        if (sizeof(VCamSharedHeader) + total_bytes > kVCamSharedMemSize) return false;

        // If av_pix_fmt == 0 (AV_PIX_FMT_YUV420P), interleave U and V into NV12 (preferred by NVIDIA Broadcast GPU TensorRT)
        if (av_pix_fmt == 0 && data[0] && data[1] && data[2]) {
            // Copy Y plane row-by-row respecting pitch
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(payload + y * width, data[0] + y * linesize[0], width);
            }
            // Interleave U and V into NV12 UV plane
            uint8_t* uv_dst = payload + y_size;
            const uint8_t* u_src = data[1];
            const uint8_t* v_src = data[2];
            int u_pitch = linesize[1];
            int v_pitch = linesize[2];
            for (uint32_t y = 0; y < height / 2; ++y) {
                const uint8_t* u_row = u_src + y * u_pitch;
                const uint8_t* v_row = v_src + y * v_pitch;
                uint8_t* dst_row = uv_dst + y * width;
                for (uint32_t x = 0; x < width / 2; ++x) {
                    dst_row[x * 2]     = u_row[x];
                    dst_row[x * 2 + 1] = v_row[x];
                }
            }
        } else if (data[0]) {
            // Already NV12 or raw copy
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(payload + y * width, data[0] + y * linesize[0], width);
            }
            if (data[1]) {
                uint8_t* uv_dst = payload + y_size;
                for (uint32_t y = 0; y < height / 2; ++y) {
                    std::memcpy(uv_dst + y * width, data[1] + y * linesize[1], width);
                }
            }
        }

        hdr->magic        = kVCamMagic;
        hdr->version      = kVCamVersion;
        hdr->width        = width;
        hdr->height       = height;
        hdr->fourcc       = 0x3231564E; // 'NV12'
        hdr->timestamp_ns = timestamp_ns;
        hdr->data_size    = total_bytes;
        hdr->linesize[0]  = width;
        hdr->linesize[1]  = width;
        hdr->linesize[2]  = 0;
        hdr->linesize[3]  = 0;
        hdr->frame_index  = ++frame_counter_;

        if (event_handle_) {
            SetEvent(event_handle_);
        }
        return true;
#else
        (void)width; (void)height; (void)av_pix_fmt; (void)data; (void)linesize; (void)timestamp_ns;
        return false;
#endif
    }

private:
#ifdef _WIN32
    HANDLE   map_handle_ = nullptr;
    void*    mapped_ptr_ = nullptr;
    HANDLE   event_handle_ = nullptr;
#endif
    uint32_t frame_counter_ = 0;
};

class VCamSharedMemoryReader {
public:
    VCamSharedMemoryReader() = default;
    ~VCamSharedMemoryReader() { close(); }

    bool open() {
#ifdef _WIN32
        if (mapped_ptr_) return true;
        map_handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kVCamSharedMemName);
        if (!map_handle_) return false;

        mapped_ptr_ = MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, kVCamSharedMemSize);
        if (!mapped_ptr_) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
            return false;
        }

        event_handle_ = OpenEventW(SYNCHRONIZE, FALSE, kVCamEventName);
        return true;
#else
        return false;
#endif
    }

    void close() {
#ifdef _WIN32
        if (mapped_ptr_) {
            UnmapViewOfFile(mapped_ptr_);
            mapped_ptr_ = nullptr;
        }
        if (map_handle_) {
            CloseHandle(map_handle_);
            map_handle_ = nullptr;
        }
        if (event_handle_) {
            CloseHandle(event_handle_);
            event_handle_ = nullptr;
        }
#endif
    }

    bool is_open() const noexcept {
#ifdef _WIN32
        return mapped_ptr_ != nullptr;
#else
        return false;
#endif
    }

    bool wait_for_frame(uint32_t timeout_ms) {
#ifdef _WIN32
        if (!event_handle_) {
            event_handle_ = OpenEventW(SYNCHRONIZE, FALSE, kVCamEventName);
            if (!event_handle_) return false;
        }
        DWORD res = WaitForSingleObject(event_handle_, static_cast<DWORD>(timeout_ms));
        return res == WAIT_OBJECT_0;
#else
        (void)timeout_ms;
        return false;
#endif
    }

    bool read_frame(VCamSharedHeader& out_hdr, std::vector<uint8_t>& out_payload) {
#ifdef _WIN32
        if (!mapped_ptr_ && !open()) return false;
        const auto* hdr = reinterpret_cast<const VCamSharedHeader*>(mapped_ptr_);
        if (hdr->magic != kVCamMagic || hdr->version != kVCamVersion || hdr->data_size == 0) {
            return false;
        }
        if (sizeof(VCamSharedHeader) + hdr->data_size > kVCamSharedMemSize) {
            return false;
        }
        out_hdr = *hdr;
        out_payload.resize(hdr->data_size);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(mapped_ptr_) + sizeof(VCamSharedHeader);
        std::memcpy(out_payload.data(), src, hdr->data_size);
        return true;
#else
        (void)out_hdr; (void)out_payload;
        return false;
#endif
    }

private:
#ifdef _WIN32
    HANDLE      map_handle_ = nullptr;
    const void* mapped_ptr_ = nullptr;
    HANDLE      event_handle_ = nullptr;
#endif
};

} // namespace camphone
