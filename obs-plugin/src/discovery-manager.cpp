#include "discovery-manager.hpp"
#include <array>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace camphone {

bool DiscoveryManager::is_valid_adb_serial(std::string_view serial) noexcept {
    if (serial.empty() || serial.size() > 64) {
        return false;
    }
    for (char c : serial) {
        const bool is_alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_safe_punct = (c == '-' || c == '_' || c == '.' || c == ':');
        if (!is_alpha && !is_digit && !is_safe_punct) {
            return false;
        }
    }
    return true;
}

static std::string find_adb_executable() {
    return "adb";
}

bool DiscoveryManager::setup_adb_forward(std::string_view serial, uint16_t local_port, uint16_t remote_port) {
    if (!is_valid_adb_serial(serial)) {
        return false;
    }
    std::ostringstream cmd;
    cmd << find_adb_executable() << " -s " << serial
        << " forward tcp:" << local_port << " tcp:" << remote_port << " 2>&1";
    FILE* pipe = POPEN(cmd.str().c_str(), "r");
    if (!pipe) {
        return false;
    }
    std::array<char, 256> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {}
    return PCLOSE(pipe) == 0;
}

std::vector<DiscoveredDevice> DiscoveryManager::list_usb_devices(uint16_t local_port, uint16_t remote_port) {
    std::vector<DiscoveredDevice> out;
    std::string cmd = find_adb_executable() + " devices";
    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) {
        return out;
    }

    std::array<char, 512> line{};
    uint16_t port_cursor = local_port;
    while (fgets(line.data(), static_cast<int>(line.size()), pipe) != nullptr) {
        std::string s(line.data());
        if (s.find("List of devices") != std::string::npos) continue;
        auto tab = s.find('\t');
        if (tab == std::string::npos) continue;
        std::string serial = s.substr(0, tab);
        std::string state = s.substr(tab + 1);
        if (state.find("device") == std::string::npos) continue;
        if (!is_valid_adb_serial(serial)) continue;

        setup_adb_forward(serial, port_cursor, remote_port);
        out.push_back(DiscoveredDevice{
            serial,
            "USB: " + serial,
            "127.0.0.1",
            port_cursor,
            true
        });
        ++port_cursor;
    }
    PCLOSE(pipe);
    return out;
}

} // namespace camphone
