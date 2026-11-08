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

struct AdbDeviceEntry {
    std::string serial;
    std::string state; // "device", "unauthorized", "offline", etc.
};

class DiscoveryManager {
public:
    // Strict allowlist validation to prevent OS command injection via ADB serials
    static bool is_valid_adb_serial(std::string_view serial) noexcept;

    // Resolve raw unquoted filesystem path to adb.exe (bundled OBS data folder, Android SDK, or system PATH)
    static std::string resolve_adb_path();

    // Resolve path to bundled CameraPhone.apk
    static std::string resolve_apk_path();

    // Execute adb.exe directly via Win32 CreateProcessA (CREATE_NO_WINDOW) without cmd.exe / _popen quote bugs
    static bool run_adb_command(const std::vector<std::string>& args, std::string& output, int timeout_ms = 15000);

    // Parse raw `adb devices` output into structured entries
    static std::vector<AdbDeviceEntry> parse_adb_devices_output(const std::string& raw_out);

    // Enumerate connected Android devices via ADB and establish local port forwarding
    static std::vector<DiscoveredDevice> list_usb_devices(uint16_t local_port = 4748, uint16_t remote_port = 4748, std::string* out_hint = nullptr);

    // Setup `adb -s <serial> forward tcp:<local_port> tcp:<remote_port>`
    static bool setup_adb_forward(std::string_view serial, uint16_t local_port = 4748, uint16_t remote_port = 4748);

    // Ensure the CameraPhone app is running on the USB-connected device and port forward is active
    static bool ensure_usb_tunnel_ready(uint16_t local_port = 4748, uint16_t remote_port = 4748, bool launch_app_if_needed = false);

    // One-click inside OBS Studio: install APK via USB cable, grant CAMERA/RECORD_AUDIO permissions,
    // launch MainActivity on the phone, and activate TCP port 4748 tunnel
    static bool install_apk_and_launch_usb(std::string& out_status_message, uint16_t local_port = 4748, uint16_t remote_port = 4748);

    // Save/load persistent Wi-Fi IP and PIN in %APPDATA%\CameraPhone\wifi_target.cfg
    static void save_wifi_target(std::string_view ip, std::string_view pin = "");
    static bool load_wifi_target(std::string& out_ip, std::string& out_pin);

    // Automatic LAN discovery of CameraPhone Android devices over Wi-Fi (UDP 4749 beacon + fast non-blocking TCP 4748 scan)
    static std::vector<DiscoveredDevice> discover_wifi_devices(uint16_t port = 4748, std::string* out_discovered_pin = nullptr);
};

} // namespace camphone
