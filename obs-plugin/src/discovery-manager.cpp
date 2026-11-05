#include "discovery-manager.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace fs = std::filesystem;

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

static fs::path get_current_module_dir() {
#ifdef _WIN32
    char buf[MAX_PATH]{};
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&get_current_module_dir), &hm) && hm) {
        GetModuleFileNameA(hm, buf, MAX_PATH);
        return fs::path(buf).parent_path();
    }
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

std::string DiscoveryManager::resolve_adb_path() {
    const fs::path mod_dir = get_current_module_dir();
    std::vector<fs::path> candidates = {
        R"(C:\Program Files\obs-studio\data\obs-plugins\cameraphone-obs\adb\adb.exe)",
        mod_dir / "adb" / "adb.exe",
        mod_dir / "adb.exe",
        mod_dir.parent_path() / "data" / "obs-plugins" / "cameraphone-obs" / "adb" / "adb.exe",
        mod_dir.parent_path().parent_path() / "data" / "obs-plugins" / "cameraphone-obs" / "adb" / "adb.exe",
    };

#ifdef _WIN32
    char local_appdata[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", local_appdata, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        candidates.push_back(fs::path(local_appdata) / "Android" / "Sdk" / "platform-tools" / "adb.exe");
    }
#endif

    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            return "\"" + c.string() + "\"";
        }
    }
    return "adb";
}

std::string DiscoveryManager::resolve_apk_path() {
    const fs::path mod_dir = get_current_module_dir();
    std::vector<fs::path> candidates = {
        R"(C:\Program Files\obs-studio\data\obs-plugins\cameraphone-obs\CameraPhone-OBS.apk)",
        mod_dir / "CameraPhone-OBS.apk",
        mod_dir.parent_path() / "CameraPhone-OBS.apk",
        mod_dir.parent_path().parent_path() / "data" / "obs-plugins" / "cameraphone-obs" / "CameraPhone-OBS.apk",
        R"(C:\Cameraphone Plugin OBS\dist\CameraPhone-OBS.apk)",
        R"(C:\Cameraphone Plugin OBS\CameraPhone-OBS.apk)",
        R"(C:\Cameraphone Plugin OBS\android-app\app\build\outputs\apk\debug\app-debug.apk)"
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            return c.string();
        }
    }
    return "";
}

static bool run_cmd_capture(const std::string& cmd, std::string& output) {
#ifdef _WIN32
    // When cmd starts with a quoted executable path, _popen on Windows cmd.exe requires outer quotes if there are multiple quotes
    std::string wrapped = "\"" + cmd + "\"";
    FILE* pipe = POPEN(wrapped.c_str(), "r");
#else
    FILE* pipe = POPEN(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return false;
    }
    std::array<char, 512> buf{};
    std::ostringstream ss;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        ss << buf.data();
    }
    output = ss.str();
    return PCLOSE(pipe) == 0;
}

bool DiscoveryManager::setup_adb_forward(std::string_view serial, uint16_t local_port, uint16_t remote_port) {
    if (!is_valid_adb_serial(serial)) {
        return false;
    }
    std::ostringstream cmd;
    cmd << resolve_adb_path() << " -s " << serial
        << " forward tcp:" << local_port << " tcp:" << remote_port << " 2>&1";
    std::string out;
    return run_cmd_capture(cmd.str(), out);
}

std::vector<DiscoveredDevice> DiscoveryManager::list_usb_devices(uint16_t local_port, uint16_t remote_port) {
    std::vector<DiscoveredDevice> out;
    std::string cmd = resolve_adb_path() + " devices";
    std::string raw_out;
    run_cmd_capture(cmd, raw_out);
    if (raw_out.empty()) {
        return out;
    }

    std::istringstream iss(raw_out);
    std::string s;
    uint16_t port_cursor = local_port;
    while (std::getline(iss, s)) {
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
            "USB: " + serial + " (Zero-Config)",
            "127.0.0.1",
            port_cursor,
            true
        });
        ++port_cursor;
    }
    return out;
}

bool DiscoveryManager::install_apk_and_launch_usb(std::string& out_status_message, uint16_t local_port, uint16_t remote_port) {
    const std::string apk_path = resolve_apk_path();
    if (apk_path.empty()) {
        out_status_message = "Erro: CameraPhone-OBS.apk nao encontrado na pasta do plugin.";
        return false;
    }

    const std::string adb = resolve_adb_path();
    auto devices = list_usb_devices(local_port, remote_port);
    if (devices.empty()) {
        out_status_message = "Nenhum celular detectado no cabo USB! Ative a Depuracao USB no Android e conecte o cabo.";
        return false;
    }

    const std::string serial = devices.front().id;
    if (!is_valid_adb_serial(serial)) {
        out_status_message = "Erro: Serial ADB invalido.";
        return false;
    }

    std::string install_out;
    std::string install_cmd = adb + " -s " + serial + " install -r -g \"" + apk_path + "\" 2>&1";
    bool installed = run_cmd_capture(install_cmd, install_out);
    if (!installed && install_out.find("Success") == std::string::npos) {
        out_status_message = "Falha ao instalar APK via USB (" + serial + "): " + install_out;
        return false;
    }

    // Grant CAMERA & RECORD_AUDIO permissions and launch MainActivity automatically
    std::string dummy;
    run_cmd_capture(adb + " -s " + serial + " shell pm grant com.cameraphone.obs android.permission.CAMERA 2>&1", dummy);
    run_cmd_capture(adb + " -s " + serial + " shell pm grant com.cameraphone.obs android.permission.RECORD_AUDIO 2>&1", dummy);
    run_cmd_capture(adb + " -s " + serial + " shell am start -n com.cameraphone.obs/.ui.MainActivity 2>&1", dummy);
    setup_adb_forward(serial, local_port, remote_port);

    out_status_message = "APK instalado e aberto no celular (" + serial + ")! Tunel USB 127.0.0.1:" + std::to_string(local_port) + " pronto.";
    return true;
}

} // namespace camphone
