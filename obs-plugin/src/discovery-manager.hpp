#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace camphone {

struct DiscoveredDevice {
    std::string id;          // ADB serial or LAN IP
    std::string display_name;
    std::string host;        // 127.0.0.1 for USB, LAN IP for Wi-Fi
    uint16_t    port = 4748;
    bool        is_usb = true;
};

class DiscoveryManager {
public:
    // Strict allowlist validation to prevent OS command injection via ADB serials
    static bool is_valid_adb_serial(std::string_view serial) noexcept;

    // Enumerate connected Android devices via ADB and establish local port forwarding
    static std::vector<DiscoveredDevice> list_usb_devices(uint16_t local_port = 4748, uint16_t remote_port = 4748);

    // Setup `adb -s <serial> forward tcp:<local_port> tcp:<remote_port>`
    static bool setup_adb_forward(std::string_view serial, uint16_t local_port = 4748, uint16_t remote_port = 4748);
};

} // namespace camphone
