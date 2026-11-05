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

    // Resolve path to adb.exe (bundled OBS data folder, Android SDK, or system PATH)
    static std::string resolve_adb_path();

    // Resolve path to bundled CameraPhone-OBS.apk
    static std::string resolve_apk_path();

    // Enumerate connected Android devices via ADB and establish local port forwarding
    static std::vector<DiscoveredDevice> list_usb_devices(uint16_t local_port = 4748, uint16_t remote_port = 4748);

    // Setup `adb -s <serial> forward tcp:<local_port> tcp:<remote_port>`
    static bool setup_adb_forward(std::string_view serial, uint16_t local_port = 4748, uint16_t remote_port = 4748);

    // One-click inside OBS Studio: install APK via USB cable, grant CAMERA/RECORD_AUDIO permissions,
    // launch MainActivity on the phone, and activate TCP port 4748 tunnel
    static bool install_apk_and_launch_usb(std::string& out_status_message, uint16_t local_port = 4748, uint16_t remote_port = 4748);
};

} // namespace camphone
