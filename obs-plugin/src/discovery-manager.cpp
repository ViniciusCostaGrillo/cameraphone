#include "discovery-manager.hpp"
#include "diagnostic-logger.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
        R"(C:\Program Files\obs-studio\data\obs-plugins\cameraphone\adb\adb.exe)",
        mod_dir / "adb" / "adb.exe",
        mod_dir / "dist" / "adb" / "adb.exe",
        mod_dir / "adb.exe",
        mod_dir.parent_path() / "data" / "obs-plugins" / "cameraphone" / "adb" / "adb.exe",
        mod_dir.parent_path().parent_path() / "data" / "obs-plugins" / "cameraphone" / "adb" / "adb.exe",
        R"(C:\Program Files\obs-studio\data\obs-plugins\droidcam-obs\adb\adb.exe)",
        R"(C:\Cameraphone Plugin OBS\dist\adb\adb.exe)"
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
            return c.string();
        }
    }
    return "adb.exe";
}

std::string DiscoveryManager::resolve_apk_path() {
    const fs::path mod_dir = get_current_module_dir();
    std::vector<fs::path> candidates = {
        R"(C:\Program Files\obs-studio\data\obs-plugins\cameraphone\CameraPhone.apk)",
        mod_dir / "CameraPhone.apk",
        mod_dir / "dist" / "CameraPhone.apk",
        mod_dir.parent_path() / "CameraPhone.apk",
        mod_dir.parent_path().parent_path() / "data" / "obs-plugins" / "cameraphone" / "CameraPhone.apk",
        R"(C:\Cameraphone Plugin OBS\dist\CameraPhone.apk)",
        R"(C:\Cameraphone Plugin OBS\CameraPhone.apk)",
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

bool DiscoveryManager::run_adb_command(const std::vector<std::string>& args, std::string& output, int timeout_ms) {
    output.clear();
    const std::string adb_exe = resolve_adb_path();

    std::ostringstream cmdline;
    cmdline << "\"" << adb_exe << "\"";
    for (const auto& a : args) {
        cmdline << " ";
        if (a.find(' ') != std::string::npos || a.find('\t') != std::string::npos) {
            cmdline << "\"" << a << "\"";
        } else {
            cmdline << a;
        }
    }
    std::string cmd_str = cmdline.str();

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        DiagnosticLogger::error("ADB", "CreatePipe failed for: " + cmd_str);
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    // Prevent grandchild background daemons (like adb fork-server) from inheriting parent stdio pipes
    if (HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, 0);
    if (HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE))  SetHandleInformation(hErr, HANDLE_FLAG_INHERIT, 0);
    if (HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE))  SetHandleInformation(hIn,  HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError  = write_pipe;
    si.hStdInput  = nullptr;

    PROCESS_INFORMATION pi{};
    std::vector<char> mutable_cmd(cmd_str.begin(), cmd_str.end());
    mutable_cmd.push_back('\0');

    const char* app_name = (adb_exe.find('\\') != std::string::npos || adb_exe.find('/') != std::string::npos)
        ? adb_exe.c_str()
        : nullptr;

    BOOL created = CreateProcessA(
        app_name,
        mutable_cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    // Close child-side write handle immediately so ReadFile reaches EOF when child exits
    CloseHandle(write_pipe);

    if (!created) {
        DWORD err = GetLastError();
        CloseHandle(read_pipe);
        DiagnosticLogger::error("ADB", "CreateProcessA failed (err=" + std::to_string(err) + ") for: " + cmd_str);
        return false;
    }

    std::ostringstream ss;
    auto start_tp = std::chrono::steady_clock::now();
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            std::array<char, 1024> buf{};
            DWORD bytes_read = 0;
            if (ReadFile(read_pipe, buf.data(), static_cast<DWORD>(buf.size()), &bytes_read, nullptr) && bytes_read > 0) {
                ss.write(buf.data(), bytes_read);
                continue;
            }
        }
        DWORD wait_res = WaitForSingleObject(pi.hProcess, 25);
        if (wait_res == WAIT_OBJECT_0) {
            // Drain any remaining bytes in the pipe
            std::array<char, 1024> buf{};
            DWORD bytes_read = 0;
            while (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
                   ReadFile(read_pipe, buf.data(), static_cast<DWORD>(buf.size()), &bytes_read, nullptr) && bytes_read > 0) {
                ss.write(buf.data(), bytes_read);
            }
            break;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_tp).count();
        if (elapsed >= timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            DiagnosticLogger::warn("ADB", "Timeout (" + std::to_string(timeout_ms) + "ms) running: " + cmd_str);
            break;
        }
    }

    output = ss.str();
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(read_pipe);
    return exit_code == 0;
#else
    (void)timeout_ms;
    FILE* pipe = popen(cmd_str.c_str(), "r");
    if (!pipe) return false;
    std::array<char, 512> buf{};
    std::ostringstream ss;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        ss << buf.data();
    }
    output = ss.str();
    return pclose(pipe) == 0;
#endif
}

std::vector<AdbDeviceEntry> DiscoveryManager::parse_adb_devices_output(const std::string& raw_out) {
    std::vector<AdbDeviceEntry> entries;
    std::istringstream iss(raw_out);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        if (line.find("List of devices") != std::string::npos) continue;
        if (line[0] == '*') continue; // "* daemon not running..." / "* daemon started..."
        if (line.find("adb") == 0) continue;

        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string serial = line.substr(0, tab);
        std::string state  = line.substr(tab + 1);
        while (!state.empty() && (state.front() == ' ' || state.front() == '\t')) {
            state.erase(state.begin());
        }
        if (!is_valid_adb_serial(serial)) continue;
        entries.push_back(AdbDeviceEntry{serial, state});
    }
    return entries;
}

bool DiscoveryManager::setup_adb_forward(std::string_view serial, uint16_t local_port, uint16_t remote_port) {
    if (!is_valid_adb_serial(serial)) {
        return false;
    }
    std::string out;
    bool ok = run_adb_command({
        "-s", std::string(serial),
        "forward",
        "tcp:" + std::to_string(local_port),
        "tcp:" + std::to_string(remote_port)
    }, out, 5000);
    if (ok) {
        DiagnosticLogger::info("ADB", "USB tunnel active: 127.0.0.1:" + std::to_string(local_port) + " -> " + std::string(serial) + ":" + std::to_string(remote_port));
    } else {
        DiagnosticLogger::warn("ADB", "adb forward failed for " + std::string(serial) + ": " + out);
    }
    return ok;
}

std::vector<DiscoveredDevice> DiscoveryManager::list_usb_devices(uint16_t local_port, uint16_t remote_port, std::string* out_hint) {
    std::vector<DiscoveredDevice> out;
    std::string raw_out;
    bool ok = run_adb_command({"devices"}, raw_out, 8000);

    // If ADB daemon reset or reported protocol fault on first call, retry once after start-server
    if (!ok || raw_out.find("protocol fault") != std::string::npos || raw_out.find("killing") != std::string::npos) {
        std::string dummy;
        run_adb_command({"start-server"}, dummy, 8000);
        run_adb_command({"devices"}, raw_out, 8000);
    }

    auto entries = parse_adb_devices_output(raw_out);

    // If any device is "offline", run `adb reconnect offline` and re-query
    bool had_offline = false;
    for (const auto& e : entries) {
        if (e.state.find("offline") != std::string::npos) {
            had_offline = true;
            break;
        }
    }
    if (had_offline) {
        std::string dummy;
        run_adb_command({"reconnect", "offline"}, dummy, 4000);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        run_adb_command({"devices"}, raw_out, 6000);
        entries = parse_adb_devices_output(raw_out);
    }

    uint16_t port_cursor = local_port;
    for (const auto& e : entries) {
        if (e.state == "device" || e.state.find("device") == 0) {
            setup_adb_forward(e.serial, port_cursor, remote_port);
            out.push_back(DiscoveredDevice{
                e.serial,
                "USB: " + e.serial + " (Zero-Config)",
                "127.0.0.1",
                port_cursor,
                true
            });
            ++port_cursor;
        } else if (e.state.find("unauthorized") != std::string::npos) {
            std::string msg = "Celular detectado no cabo USB (" + e.serial + "), mas AGUARDANDO AUTORIZACAO! Desbloqueie a tela do celular e toque em 'Permitir Depuracao USB'.";
            DiagnosticLogger::warn("ADB", msg);
            if (out_hint) *out_hint = msg;
        } else if (e.state.find("offline") != std::string::npos) {
            std::string msg = "Celular detectado no cabo USB (" + e.serial + "), mas em estado OFFLINE. Reconecte o cabo USB ou altere o modo USB para Transferencia de Arquivos.";
            DiagnosticLogger::warn("ADB", msg);
            if (out_hint && out_hint->empty()) *out_hint = msg;
        }
    }
    return out;
}

bool DiscoveryManager::ensure_usb_tunnel_ready(uint16_t local_port, uint16_t remote_port, bool launch_app_if_needed) {
    auto devices = list_usb_devices(local_port, remote_port);
    if (devices.empty()) {
        return false;
    }
    if (launch_app_if_needed) {
        const std::string& serial = devices.front().id;
        std::string dummy;
        run_adb_command({"-s", serial, "shell", "am", "start", "-n", "com.cameraphone.app/.ui.MainActivity"}, dummy, 4000);
    }
    return true;
}

bool DiscoveryManager::install_apk_and_launch_usb(std::string& out_status_message, uint16_t local_port, uint16_t remote_port) {
    const std::string apk_path = resolve_apk_path();
    if (apk_path.empty()) {
        out_status_message = "Erro: CameraPhone.apk nao encontrado na pasta do plugin.";
        return false;
    }

    std::string hint;
    auto devices = list_usb_devices(local_port, remote_port, &hint);
    if (devices.empty()) {
        if (!hint.empty()) {
            out_status_message = hint;
        } else {
            out_status_message = "Nenhum celular detectado no cabo USB! Verifique:\r\n"
                                 "1) Opcoes do Desenvolvedor -> Depuracao USB ativada no celular;\r\n"
                                 "2) Modo USB em 'Transferencia de Arquivos / Android Auto';\r\n"
                                 "3) Toque em 'Permitir' no aviso de Depuracao USB na tela do celular.";
        }
        return false;
    }

    const std::string serial = devices.front().id;
    if (!is_valid_adb_serial(serial)) {
        out_status_message = "Erro: Serial ADB invalido.";
        return false;
    }

    DiagnosticLogger::info("ADB", "Installing CameraPhone.apk onto USB device " + serial + "...");
    std::string install_out;
    bool installed = run_adb_command({"-s", serial, "install", "-r", "-g", apk_path}, install_out, 60000);
    if (!installed && install_out.find("Success") == std::string::npos) {
        out_status_message = "Falha ao instalar APK via USB (" + serial + "): " + install_out;
        DiagnosticLogger::error("ADB", out_status_message);
        return false;
    }

    // Grant CAMERA & RECORD_AUDIO permissions and launch MainActivity automatically
    std::string dummy;
    run_adb_command({"-s", serial, "shell", "pm", "grant", "com.cameraphone.app", "android.permission.CAMERA"}, dummy, 5000);
    run_adb_command({"-s", serial, "shell", "pm", "grant", "com.cameraphone.app", "android.permission.RECORD_AUDIO"}, dummy, 5000);
    run_adb_command({"-s", serial, "shell", "am", "start", "-n", "com.cameraphone.app/.ui.MainActivity"}, dummy, 5000);
    setup_adb_forward(serial, local_port, remote_port);

    out_status_message = "APK CameraPhone instalado e aberto no celular (" + serial + ")! Tunel USB 127.0.0.1:" + std::to_string(local_port) + " pronto.";
    DiagnosticLogger::info("ADB", out_status_message);
    return true;
}

} // namespace camphone
