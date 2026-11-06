#pragma once

#include "camphone_protocol.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

namespace camphone {

struct LensInfo {
    std::string id;
    std::string label;
    std::string facing;
    int         focal_mm = 24;
    bool        ois      = false;
    int         iso_min  = 50;
    int         iso_max  = 6400;
    float       max_zoom = 10.0f;
};

struct DeviceCapabilities {
    std::string           device_model;
    std::string           soc;
    bool                  supports_hevc  = false;
    bool                  supports_av1   = false;
    bool                  supports_hdr10 = false;
    std::vector<LensInfo> lenses;
};

struct StreamConfig {
    std::string host   = "127.0.0.1";
    uint16_t    port   = kDefaultPort;
    std::string pin    = "";
    std::string lens   = "0";
    int         width  = 3840;
    int         height = 2160;
    int         fps    = 60;
    std::string codec  = "hevc";
    bool        hdr    = false;
};

class StreamClient {
public:
    StreamClient();
    ~StreamClient();

    static bool parse_capabilities_json(const std::string& json, DeviceCapabilities& out);

    bool fetch_capabilities(const std::string& host, uint16_t port, const std::string& pin, DeviceCapabilities& out);
    bool fetch_remote_logs(const std::string& host, uint16_t port, const std::string& pin, std::string& out_logs);
    bool send_control_patch(const std::string& host, uint16_t port, const std::string& pin, const std::string& json_patch);
    bool send_tally_state(const std::string& host, uint16_t port, const std::string& pin, const std::string& state);

    void start_streaming(const StreamConfig& cfg, BoundedFrameQueue& video_queue, BoundedFrameQueue& audio_queue);
    void stop_streaming();
    bool is_connected() const noexcept { return connected_.load(); }

private:
    std::atomic<bool>      running_{false};
    std::atomic<bool>      connected_{false};
    std::atomic<uintptr_t> active_sock_{static_cast<uintptr_t>(~0ULL)};
    std::thread            worker_;
};

} // namespace camphone
