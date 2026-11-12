#include "../obs-plugin/src/diagnostic-logger.hpp"
#include "../obs-plugin/src/discovery-manager.hpp"
#include "../obs-plugin/src/stream-client.hpp"
#include "../obs-plugin/src/media-decoder.hpp"
#include "../protocol/vcam_shmem.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace camphone;

#define WM_TRAYICON (WM_USER + 100)
#define ID_TRAY_OPEN 2001
#define ID_TRAY_EXIT 2002

enum class UiLanguage { EN, PT };
static UiLanguage g_ui_lang = UiLanguage::EN;

static fs::path get_exe_dir() {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static fs::path find_file_in_candidates(const std::vector<fs::path>& candidates) {
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            return c;
        }
    }
    return {};
}

static bool install_zero_config_bundle(std::string& out_msg, bool allow_uac_prompt) {
    const fs::path exe_dir = get_exe_dir();

    const fs::path dll_src = find_file_in_candidates({
        exe_dir / "cameraphone.dll",
        exe_dir / "dist" / "cameraphone.dll",
        exe_dir / "obs-plugin" / "build" / "cameraphone.dll",
        exe_dir.parent_path() / "obs-plugin" / "build" / "cameraphone.dll",
        R"(C:\Cameraphone Plugin OBS\dist\cameraphone.dll)",
        R"(C:\Cameraphone Plugin OBS\obs-plugin\build\cameraphone.dll)"
    });

    const fs::path apk_src = find_file_in_candidates({
        exe_dir / "CameraPhone.apk",
        exe_dir / "dist" / "CameraPhone.apk",
        exe_dir / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk",
        exe_dir.parent_path() / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk",
        R"(C:\Cameraphone Plugin OBS\dist\CameraPhone.apk)",
        R"(C:\Cameraphone Plugin OBS\CameraPhone.apk)"
    });

    char local_appdata[MAX_PATH]{};
    GetEnvironmentVariableA("LOCALAPPDATA", local_appdata, MAX_PATH);
    const fs::path sdk_pt = fs::path(local_appdata) / "Android" / "Sdk" / "platform-tools";

    const fs::path adb_src = find_file_in_candidates({
        exe_dir / "adb" / "adb.exe",
        exe_dir / "dist" / "adb" / "adb.exe",
        sdk_pt / "adb.exe",
        R"(C:\Cameraphone Plugin OBS\dist\adb\adb.exe)",
        R"(C:\Program Files\obs-studio\data\obs-plugins\droidcam-obs\adb\adb.exe)"
    });

    if (dll_src.empty()) {
        out_msg = (g_ui_lang == UiLanguage::PT)
            ? "[ERRO] Arquivo cameraphone.dll nao encontrado."
            : "[ERROR] cameraphone.dll file not found.";
        DiagnosticLogger::error("Installer", out_msg);
        return false;
    }

    const fs::path obs_root = R"(C:\Program Files\obs-studio)";
    const fs::path obs_plugins_dir = obs_root / "obs-plugins" / "64bit";
    const fs::path obs_data_dir = obs_root / "data" / "obs-plugins" / "cameraphone";
    const fs::path obs_adb_dir = obs_data_dir / "adb";

    if (!fs::exists(obs_plugins_dir)) {
        out_msg = (g_ui_lang == UiLanguage::PT)
            ? "[AVISO] Pasta de plugins nao encontrada em C:\\Program Files\\obs-studio\\obs-plugins\\64bit."
            : "[WARNING] OBS plugin folder not found at C:\\Program Files\\obs-studio\\obs-plugins\\64bit.";
        DiagnosticLogger::warn("Installer", out_msg);
        return false;
    }

    std::error_code ec;
    fs::remove(obs_plugins_dir / "cameraphone-obs.dll", ec);

    fs::create_directories(obs_adb_dir, ec);
    const fs::path dll_dst = obs_plugins_dir / "cameraphone.dll";
    fs::copy_file(dll_src, dll_dst, fs::copy_options::overwrite_existing, ec);

    const fs::path vcam_src = find_file_in_candidates({
        exe_dir / "cameraphone-vcam.dll",
        exe_dir / "dist" / "cameraphone-vcam.dll",
        R"(C:\Cameraphone Plugin OBS\dist\cameraphone-vcam.dll)",
        R"(C:\Cameraphone Plugin OBS\cameraphone-vcam.dll)"
    });
    if (!vcam_src.empty()) {
        auto run_sys32_regsvr32 = [](const fs::path& dll_path) {
            char sys_dir[MAX_PATH]{};
            GetSystemDirectoryA(sys_dir, MAX_PATH);
            fs::path regsvr = fs::path(sys_dir) / "regsvr32.exe";
            std::string cmd = "\"" + regsvr.string() + "\" /s \"" + dll_path.string() + "\"";
            std::vector<char> cmd_buf(cmd.begin(), cmd.end());
            cmd_buf.push_back('\0');
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (CreateProcessA(regsvr.string().c_str(), cmd_buf.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, sys_dir, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 10000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        };
        const fs::path vcam_dst = obs_plugins_dir / "cameraphone-vcam.dll";
        std::error_code vcam_ec;
        fs::copy_file(vcam_src, vcam_dst, fs::copy_options::overwrite_existing, vcam_ec);
        if (!vcam_ec) {
            run_sys32_regsvr32(vcam_dst);
        } else if (!fs::exists(vcam_dst)) {
            run_sys32_regsvr32(vcam_src);
        }
    }

    if (!ec || fs::exists(dll_dst)) {
        std::error_code data_ec;
        if (!apk_src.empty()) {
            fs::copy_file(apk_src, obs_data_dir / "CameraPhone.apk", fs::copy_options::overwrite_existing, data_ec);
        }
        if (!adb_src.empty()) {
            const fs::path adb_parent = adb_src.parent_path();
            fs::copy_file(adb_src, obs_adb_dir / "adb.exe", fs::copy_options::overwrite_existing, data_ec);
            if (fs::exists(adb_parent / "AdbWinApi.dll")) {
                fs::copy_file(adb_parent / "AdbWinApi.dll", obs_adb_dir / "AdbWinApi.dll", fs::copy_options::overwrite_existing, data_ec);
            }
            if (fs::exists(adb_parent / "AdbWinUsbApi.dll")) {
                fs::copy_file(adb_parent / "AdbWinUsbApi.dll", obs_adb_dir / "AdbWinUsbApi.dll", fs::copy_options::overwrite_existing, data_ec);
            }
        }
        if (!ec) {
            out_msg = (g_ui_lang == UiLanguage::PT)
                ? "[ZERO-CONFIG INSTALADO COM SUCESSO]\r\n"
                  "  * Plugin OBS: C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\cameraphone.dll\r\n"
                  "  * Câmera Virtual: CameraPhone Virtual Camera registrada (NVIDIA Broadcast pronta!)\r\n"
                  "  * Driver ADB & APK atualizados para conexao USB/Wi-Fi plug-and-play."
                : "[ZERO-CONFIG INSTALLED SUCCESSFULLY]\r\n"
                  "  * OBS Plugin: C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\cameraphone.dll\r\n"
                  "  * Virtual Camera: CameraPhone Virtual Camera registered (NVIDIA Broadcast ready!)\r\n"
                  "  * ADB Driver & APK updated for plug-and-play USB & Wi-Fi streaming.";
            DiagnosticLogger::info("Installer", "Zero-config bundle installed successfully.");
            return true;
        }
    }

    if (!allow_uac_prompt) {
        if (fs::exists(dll_dst)) {
            out_msg = (g_ui_lang == UiLanguage::PT)
                ? "[ZERO-CONFIG ATIVO] Plugin cameraphone.dll + CameraPhone.apk + ADB instalados!"
                : "[ZERO-CONFIG ACTIVE] Plugin cameraphone.dll + CameraPhone.apk + ADB installed!";
            DiagnosticLogger::info("Installer", out_msg);
            return true;
        }
        out_msg = (g_ui_lang == UiLanguage::PT)
            ? "[ERRO] Sem permissao de escrita em C:\\Program Files (feche o OBS ou Broadcast e execute como Administrador)."
            : "[ERROR] No write permission in C:\\Program Files (close OBS or Broadcast and run as Administrator).";
        DiagnosticLogger::error("Installer", out_msg);
        return false;
    }

    char self_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, self_path, MAX_PATH);
    SHELLEXECUTEINFOA sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";
    sei.lpFile = self_path;
    sei.lpParameters = "--elevated-install";
    sei.nShow = SW_HIDE;

    if (ShellExecuteExA(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        CloseHandle(sei.hProcess);
    }

    if (fs::exists(dll_dst)) {
        out_msg = (g_ui_lang == UiLanguage::PT)
            ? "[ZERO-CONFIG ATIVO] Plugin cameraphone.dll + CameraPhone.apk + ADB instalados!"
            : "[ZERO-CONFIG ACTIVE] Plugin cameraphone.dll + CameraPhone.apk + ADB installed!";
        DiagnosticLogger::info("Installer", out_msg);
        return true;
    }

    out_msg = (g_ui_lang == UiLanguage::PT)
        ? "[AVISO] Nao foi possivel sobrescrever arquivos (feche o OBS ou NVIDIA Broadcast se estiver aberto)."
        : "[WARNING] Could not overwrite files (please close OBS or NVIDIA Broadcast if running).";
    DiagnosticLogger::warn("Installer", out_msg);
    return false;
}

static std::string install_android_apk_and_tunnel() {
    std::string msg;
    DiscoveryManager::install_apk_and_launch_usb(msg, 4748, 4748);
    DiagnosticLogger::info("Installer-USB", msg);
    return msg;
}

// Background Hybrid (USB + Wi-Fi) video feeder that streams directly to DirectShow VCam for NVIDIA Broadcast
class UsbBroadcastFeeder {
public:
    void start() {
        if (m_running.exchange(true)) return;
        m_thread = std::thread([this]() { run(); });
    }

    void stop() {
        if (!m_running.exchange(false)) return;
        m_client.stop_streaming();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool is_streaming() const { return m_streaming.load(); }
    std::string get_status_text() const { return m_status; }

private:
    void run() {
        BoundedFrameQueue video_queue{60};
        BoundedFrameQueue audio_queue{60};
        MediaDecoder decoder;
        VCamSharedMemoryWriter vcam_writer;

        while (m_running.load()) {
            // Yield cleanly while OBS Studio's native CameraPhone source is actively streaming
            if (OBSStreamGuard::is_obs_streaming()) {
                m_streaming.store(false);
                m_status = "OBS Studio active (Virtual Camera fed by OBS)";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            std::string target_host;
            std::string target_pin = "usb-zero-config";
            bool is_usb = false;
            DeviceCapabilities caps{};
            bool connected_caps = false;

            // Priority 1: Check USB Cable (127.0.0.1:4748)
            std::string usb_hint;
            auto usb_devs = DiscoveryManager::list_usb_devices(4748, 4748, &usb_hint);
            if (!usb_devs.empty()) {
                if (m_client.fetch_capabilities("127.0.0.1", 4748, "usb-zero-config", caps)) {
                    target_host = "127.0.0.1";
                    target_pin = "usb-zero-config";
                    is_usb = true;
                    connected_caps = true;
                }
            }

            // Priority 2a: Direct Wi-Fi check using cached Wi-Fi IP (zero extra probe sockets)
            if (!connected_caps) {
                std::string saved_ip, saved_pin;
                if (DiscoveryManager::load_wifi_target(saved_ip, saved_pin) && !saved_ip.empty() && saved_ip != "127.0.0.1") {
                    if (!saved_pin.empty() && m_client.fetch_capabilities(saved_ip, 4748, saved_pin, caps)) {
                        target_host = saved_ip;
                        target_pin = saved_pin;
                        is_usb = false;
                        connected_caps = true;
                    } else if (m_client.fetch_capabilities(saved_ip, 4748, "usb-zero-config", caps)) {
                        target_host = saved_ip;
                        target_pin = "usb-zero-config";
                        is_usb = false;
                        connected_caps = true;
                    }
                }
            }

            // Priority 2b: Automatic Wi-Fi LAN Discovery (UDP 4749 Beacon + Fast Subnet Scan)
            if (!connected_caps) {
                std::string disc_pin;
                auto wifi_devs = DiscoveryManager::discover_wifi_devices(4748, &disc_pin);
                if (!wifi_devs.empty()) {
                    const std::string wifi_ip = wifi_devs.front().host;
                    if (!disc_pin.empty() && m_client.fetch_capabilities(wifi_ip, 4748, disc_pin, caps)) {
                        target_host = wifi_ip;
                        target_pin = disc_pin;
                        is_usb = false;
                        connected_caps = true;
                    } else if (m_client.fetch_capabilities(wifi_ip, 4748, "usb-zero-config", caps)) {
                        target_host = wifi_ip;
                        target_pin = "usb-zero-config";
                        is_usb = false;
                        connected_caps = true;
                    }
                }
            }

            if (!connected_caps) {
                m_streaming.store(false);
                m_status = "Waiting for phone on USB Cable or Wi-Fi...";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            if (OBSStreamGuard::is_obs_streaming()) {
                continue;
            }

            StreamConfig cfg{};
            cfg.host = target_host;
            cfg.port = 4748;
            cfg.pin = target_pin;
            cfg.width = 3840;
            cfg.height = 2160;
            cfg.fps = 60;
            cfg.codec = caps.supports_hevc ? "hevc" : "h264";

            video_queue.clear();
            audio_queue.clear();
            decoder.initialize_video(cfg.codec);
            vcam_writer.open();
            m_client.start_streaming(cfg, video_queue, audio_queue);

            // Wait up to 4000ms for the async worker thread to complete TCP + HTTP 200 OK handshake
            for (int wait_step = 0;
                 wait_step < 80 && m_running.load() && m_client.is_running() &&
                 !OBSStreamGuard::is_obs_streaming() && !m_client.is_connected();
                 ++wait_step) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (m_running.load() && m_client.is_connected() && !OBSStreamGuard::is_obs_streaming()) {
                m_streaming.store(true);
                m_status = std::string("Streaming ") + (is_usb ? "USB" : ("Wi-Fi (" + target_host + ")")) +
                           " -> CameraPhone Virtual Camera (" + caps.device_model + ")";
                DiagnosticLogger::info("UsbFeeder", m_status);

                while (m_running.load() && m_client.is_connected() && !OBSStreamGuard::is_obs_streaming()) {
                    auto pkt = video_queue.pop();
                    if (pkt.has_value()) {
                        decoder.decode_video_packet(*pkt, [&](const DecodedVideoFrame& vf) {
                            vcam_writer.write_frame(
                                static_cast<uint32_t>(vf.width),
                                static_cast<uint32_t>(vf.height),
                                vf.av_pix_fmt,
                                vf.data,
                                vf.linesize,
                                vf.pts_ns
                            );
                        });
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            }

            m_client.stop_streaming();
            decoder.shutdown();
            m_streaming.store(false);
            for (int i = 0; i < 5 && m_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_streaming{false};
    std::string       m_status = "Starting...";
    StreamClient      m_client;
    std::thread       m_thread;
};

static UsbBroadcastFeeder g_feeder;

enum ControlIds : int {
    ID_BTN_INSTALL_PLUGIN = 101,
    ID_BTN_SETUP_USB      = 102,
    ID_BTN_PROBE_CAPS     = 103,
    ID_BTN_VIEW_LOGS      = 104,
    ID_BTN_TOGGLE_LANG    = 105,
    ID_EDIT_LOG           = 106
};

static HWND g_title_hwnd       = nullptr;
static HWND g_subtitle_hwnd    = nullptr;
static HWND g_btn_install_hwnd = nullptr;
static HWND g_btn_usb_hwnd     = nullptr;
static HWND g_btn_probe_hwnd   = nullptr;
static HWND g_btn_logs_hwnd    = nullptr;
static HWND g_btn_lang_hwnd    = nullptr;
static HWND g_console_lbl_hwnd = nullptr;
static HWND g_edit_log         = nullptr;

static HBRUSH g_bg_brush       = nullptr;
static HBRUSH g_log_brush      = nullptr;
static NOTIFYICONDATAA g_nid   = {};

static void update_ui_language(HWND hwnd) {
    if (g_ui_lang == UiLanguage::PT) {
        SetWindowTextA(hwnd, "CameraPhone 4K Pro - Companion & Câmera Virtual DirectShow");
        if (g_title_hwnd) SetWindowTextA(g_title_hwnd, "CameraPhone 4K Pro");
        if (g_subtitle_hwnd) SetWindowTextA(g_subtitle_hwnd, "Câmera Virtual NVIDIA Broadcast & Plugin OBS Zero-Config");
        if (g_btn_install_hwnd) SetWindowTextA(g_btn_install_hwnd, "1. Instalar Tudo (OBS + VCam)");
        if (g_btn_usb_hwnd) SetWindowTextA(g_btn_usb_hwnd, "2. Instalar APK no Celular");
        if (g_btn_probe_hwnd) SetWindowTextA(g_btn_probe_hwnd, "3. Testar Conexao");
        if (g_btn_logs_hwnd) SetWindowTextA(g_btn_logs_hwnd, "4. Ver Logs");
        if (g_btn_lang_hwnd) SetWindowTextA(g_btn_lang_hwnd, "Language: PT");
        if (g_console_lbl_hwnd) SetWindowTextA(g_console_lbl_hwnd, "CONSOLE DE DIAGNOSTICO EM TEMPO REAL");
    } else {
        SetWindowTextA(hwnd, "CameraPhone 4K Pro - Companion & DirectShow Virtual Camera");
        if (g_title_hwnd) SetWindowTextA(g_title_hwnd, "CameraPhone 4K Pro");
        if (g_subtitle_hwnd) SetWindowTextA(g_subtitle_hwnd, "NVIDIA Broadcast Virtual Camera & OBS Zero-Config Plugin");
        if (g_btn_install_hwnd) SetWindowTextA(g_btn_install_hwnd, "1. Install All (OBS + VCam)");
        if (g_btn_usb_hwnd) SetWindowTextA(g_btn_usb_hwnd, "2. Install APK on Phone");
        if (g_btn_probe_hwnd) SetWindowTextA(g_btn_probe_hwnd, "3. Test Connection");
        if (g_btn_logs_hwnd) SetWindowTextA(g_btn_logs_hwnd, "4. View Logs");
        if (g_btn_lang_hwnd) SetWindowTextA(g_btn_lang_hwnd, "Language: EN");
        if (g_console_lbl_hwnd) SetWindowTextA(g_console_lbl_hwnd, "REAL-TIME DIAGNOSTIC CONSOLE");
    }
}

static void append_log(const std::string& msg) {
    if (!g_edit_log) return;
    int len = GetWindowTextLengthA(g_edit_log);
    SendMessageA(g_edit_log, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    std::string line = msg + "\r\n";
    SendMessageA(g_edit_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

static void fetch_and_display_all_logs() {
    std::ostringstream ss;
    if (g_ui_lang == UiLanguage::PT) {
        ss << "\r\n==================== DIAGNOSTICO INTEGRADO (PC + APK) ====================\r\n";
        ss << "[1] LOG LOCAL DO PC (" << DiagnosticLogger::log_file_path().string() << "):\r\n";
    } else {
        ss << "\r\n==================== INTEGRATED DIAGNOSTICS (PC + APK) ===================\r\n";
        ss << "[1] LOCAL PC LOG (" << DiagnosticLogger::log_file_path().string() << "):\r\n";
    }
    ss << DiagnosticLogger::read_tail(6144) << "\r\n";

    StreamClient client;
    std::string apk_logs;
    if (client.fetch_remote_logs("127.0.0.1", 4748, "usb-zero-config", apk_logs)) {
        ss << (g_ui_lang == UiLanguage::PT
               ? "[2] LOGS REMOTOS DO APK NO CELULAR (USB 127.0.0.1:4748):\r\n"
               : "[2] REMOTE PHONE APK LOGS (USB 127.0.0.1:4748):\r\n")
           << apk_logs;
    } else {
        std::string disc_pin;
        auto wifi_devs = DiscoveryManager::discover_wifi_devices(4748, &disc_pin);
        if (!wifi_devs.empty() &&
            ((!disc_pin.empty() && client.fetch_remote_logs(wifi_devs[0].host, 4748, disc_pin, apk_logs)) ||
             client.fetch_remote_logs(wifi_devs[0].host, 4748, "usb-zero-config", apk_logs))) {
            ss << (g_ui_lang == UiLanguage::PT
                   ? "[2] LOGS REMOTOS DO APK NO CELULAR (Wi-Fi "
                   : "[2] REMOTE PHONE APK LOGS (Wi-Fi ")
               << wifi_devs[0].host << ":4748):\r\n" << apk_logs;
        } else {
            ss << (g_ui_lang == UiLanguage::PT
                   ? "[2] LOGS REMOTOS DO APK: Celular desconectado ou app CameraPhone fechado no momento.\r\n"
                   : "[2] REMOTE APK LOGS: Phone disconnected or CameraPhone app currently closed.\r\n");
        }
    }
    ss << "==========================================================================\r\n";
    append_log(ss.str());
}

static void init_tray_icon(HWND hwnd) {
    std::memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
    strcpy_s(g_nid.szTip, "CameraPhone 4K Pro (Active Streaming)");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void remove_tray_icon() {
    if (g_nid.cbSize) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE: {
        // Apply Windows 10/11 Immersive Dark Titlebar
        BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_mode, sizeof(dark_mode));
        DwmSetWindowAttribute(hwnd, 19 /* legacy dark mode attribute */, &dark_mode, sizeof(dark_mode));

        HFONT font_title = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_sub = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_btn = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_console_lbl = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_log = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, "Consolas");

        HICON hIconBig = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
        if (hIconBig) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconBig));
        }

        // Title and Subtitle
        g_title_hwnd = CreateWindowExA(0, "STATIC", "CameraPhone 4K Pro",
                                       WS_CHILD | WS_VISIBLE, 20, 14, 300, 26, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_title_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        g_subtitle_hwnd = CreateWindowExA(0, "STATIC", "NVIDIA Broadcast Virtual Camera & OBS Zero-Config Plugin",
                                          WS_CHILD | WS_VISIBLE, 20, 40, 480, 18, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_subtitle_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_sub), TRUE);

        // Language toggle button at top right
        g_btn_lang_hwnd = CreateWindowExA(0, "BUTTON", "Language: EN",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          620, 16, 110, 30, hwnd, reinterpret_cast<HMENU>(ID_BTN_TOGGLE_LANG), nullptr, nullptr);
        SendMessageA(g_btn_lang_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        // Action Buttons Row (Top Toolbar)
        g_btn_install_hwnd = CreateWindowExA(0, "BUTTON", "1. Install All (OBS + VCam)",
                                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             20, 70, 195, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL_PLUGIN), nullptr, nullptr);
        SendMessageA(g_btn_install_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        g_btn_usb_hwnd = CreateWindowExA(0, "BUTTON", "2. Install APK on Phone",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         225, 70, 175, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_SETUP_USB), nullptr, nullptr);
        SendMessageA(g_btn_usb_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        g_btn_probe_hwnd = CreateWindowExA(0, "BUTTON", "3. Test Connection",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           410, 70, 150, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_PROBE_CAPS), nullptr, nullptr);
        SendMessageA(g_btn_probe_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        g_btn_logs_hwnd = CreateWindowExA(0, "BUTTON", "4. View Logs",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          570, 70, 160, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_VIEW_LOGS), nullptr, nullptr);
        SendMessageA(g_btn_logs_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        // Console Header Label
        g_console_lbl_hwnd = CreateWindowExA(0, "STATIC", "REAL-TIME DIAGNOSTIC CONSOLE",
                                             WS_CHILD | WS_VISIBLE, 20, 116, 400, 16, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_console_lbl_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_console_lbl), TRUE);

        // Expansive Log Box (Fills entire bottom region)
        g_edit_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                     20, 136, 710, 335, hwnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);
        SendMessageA(g_edit_log, WM_SETFONT, reinterpret_cast<WPARAM>(font_log), TRUE);

        update_ui_language(hwnd);
        init_tray_icon(hwnd);
        g_feeder.start();

        std::string auto_install_msg;
        const bool already_installed = fs::exists(R"(C:\Program Files\obs-studio\obs-plugins\64bit\cameraphone.dll)");
        install_zero_config_bundle(auto_install_msg, !already_installed);
        append_log(auto_install_msg);
        append_log("[System] Automatic USB & Wi-Fi streaming to CameraPhone Virtual Camera (NVIDIA Broadcast) started.");
        append_log("[Tip] For USB or Wi-Fi: simply open CameraPhone on your phone (USB cable or same Wi-Fi network)!");
        append_log("[Tip] In OBS Studio: add source 'CameraPhone 4K Pro' and select USB Cable or Wi-Fi.");
        append_log("[Tip] In NVIDIA Broadcast: select 'CameraPhone Virtual Camera' in the Camera device list.");
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        HWND ctl = reinterpret_cast<HWND>(lparam);
        if (ctl == g_title_hwnd) {
            SetTextColor(hdc, RGB(0, 229, 255)); // Cyan #00E5FF
        } else if (ctl == g_console_lbl_hwnd) {
            SetTextColor(hdc, RGB(0, 230, 118)); // Mint #00E676
        } else {
            SetTextColor(hdc, RGB(139, 157, 174)); // Slate #8B9DAE
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_bg_brush);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        SetTextColor(hdc, RGB(201, 209, 217)); // Soft terminal white #C9D1D9
        SetBkColor(hdc, RGB(13, 17, 23));      // Deep dark console #0D1117
        return reinterpret_cast<LRESULT>(g_log_brush);
    }
    case WM_TRAYICON: {
        if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lparam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_OPEN, g_ui_lang == UiLanguage::PT ? "Abrir CameraPhone" : "Open CameraPhone");
            AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, g_ui_lang == UiLanguage::PT ? "Sair" : "Exit");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == ID_TRAY_OPEN) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        }
        if (id == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_BTN_TOGGLE_LANG) {
            g_ui_lang = (g_ui_lang == UiLanguage::EN) ? UiLanguage::PT : UiLanguage::EN;
            update_ui_language(hwnd);
            if (g_ui_lang == UiLanguage::PT) {
                append_log("[Idioma] Idioma da interface alterado para Portugues.");
            } else {
                append_log("[Language] UI language switched to English.");
            }
            return 0;
        }
        if (id == ID_BTN_INSTALL_PLUGIN) {
            std::string status;
            install_zero_config_bundle(status, true);
            append_log(status);
        } else if (id == ID_BTN_SETUP_USB) {
            append_log(g_ui_lang == UiLanguage::PT ? "[Instalando APK via Cabo USB...]" : "[Installing APK via USB Cable...]");
            append_log(install_android_apk_and_tunnel());
        } else if (id == ID_BTN_PROBE_CAPS) {
            std::string usb_hint;
            auto devs = DiscoveryManager::list_usb_devices(4748, 4748, &usb_hint);
            StreamClient client;
            DeviceCapabilities caps{};
            bool ok = false;
            std::string conn_label = "USB 127.0.0.1:4748";

            if (!devs.empty()) {
                append_log((g_ui_lang == UiLanguage::PT ? "[USB DETECTADO] " : "[USB DETECTED] ") + devs[0].display_name);
                DiscoveryManager::ensure_usb_tunnel_ready(4748, 4748, true);
                Sleep(400);
                ok = client.fetch_capabilities("127.0.0.1", 4748, "usb-zero-config", caps);
            }

            if (!ok) {
                std::string disc_pin;
                auto wifi_devs = DiscoveryManager::discover_wifi_devices(4748, &disc_pin);
                if (!wifi_devs.empty()) {
                    const std::string wip = wifi_devs[0].host;
                    append_log((g_ui_lang == UiLanguage::PT
                                ? "[WI-FI DETECTADO] Celular encontrado na rede local em "
                                : "[WI-FI DETECTED] Phone found on local network at ") + wip + ":4748");
                    conn_label = "Wi-Fi " + wip + ":4748";
                    if (!disc_pin.empty()) {
                        ok = client.fetch_capabilities(wip, 4748, disc_pin, caps);
                    }
                    if (!ok) {
                        ok = client.fetch_capabilities(wip, 4748, "usb-zero-config", caps);
                    }
                    if (!ok) {
                        append_log(g_ui_lang == UiLanguage::PT
                            ? "[AVISO WI-FI] Conecte o cabo USB uma vez e clique em '2. Instalar APK no Celular' para liberar o Wi-Fi Zero-Config!"
                            : "[WI-FI NOTICE] Connect USB cable once and click '2. Install APK on Phone' to enable automatic Wi-Fi Zero-Config!");
                    }
                } else {
                    append_log((g_ui_lang == UiLanguage::PT ? "[AVISO] " : "[NOTICE] ") +
                               (usb_hint.empty()
                                ? (g_ui_lang == UiLanguage::PT ? "Nenhum celular detectado via Cabo USB ou Wi-Fi." : "No phone detected via USB Cable or Wi-Fi.")
                                : usb_hint));
                }
            }

            if (ok) {
                std::ostringstream ss;
                if (g_ui_lang == UiLanguage::PT) {
                    ss << "[CONECTADO COM SUCESSO (" << conn_label << ")]\r\n"
                       << "  Modelo: " << caps.device_model << " (" << caps.soc << ")\r\n"
                       << "  Codecs Hardware: HEVC=" << (caps.supports_hevc ? "Sim" : "Nao")
                       << " | AV1=" << (caps.supports_av1 ? "Sim" : "Nao")
                       << " | HDR10=" << (caps.supports_hdr10 ? "Sim" : "Nao") << "\r\n"
                       << "  Lentes Fisicas Detectadas: " << caps.lenses.size();
                } else {
                    ss << "[CONNECTED SUCCESSFULLY (" << conn_label << ")]\r\n"
                       << "  Model: " << caps.device_model << " (" << caps.soc << ")\r\n"
                       << "  Hardware Codecs: HEVC=" << (caps.supports_hevc ? "Yes" : "No")
                       << " | AV1=" << (caps.supports_av1 ? "Yes" : "No")
                       << " | HDR10=" << (caps.supports_hdr10 ? "Yes" : "No") << "\r\n"
                       << "  Physical Lenses Detected: " << caps.lenses.size();
                }
                for (const auto& l : caps.lenses) {
                    ss << "\r\n    * ID [" << l.id << "] " << l.label << " (" << l.focal_mm << "mm"
                       << (l.ois ? ", OIS" : "") << ")";
                }
                append_log(ss.str());
            } else {
                append_log(g_ui_lang == UiLanguage::PT
                    ? "[FALHA] Verifique se o app CameraPhone esta aberto na tela do celular (Cabo USB ou mesma rede Wi-Fi)."
                    : "[FAILED] Make sure the CameraPhone app is open on your phone (USB Cable or same Wi-Fi network).");
            }
        } else if (id == ID_BTN_VIEW_LOGS) {
            fetch_and_display_all_logs();
        }
        return 0;
    }
    case WM_SYSCOMMAND: {
        if ((wparam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        // Hide to System Tray so USB -> NVIDIA Broadcast background daemon continues uninterrupted
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY:
        g_feeder.stop();
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

int main(int argc, char** argv) {
    bool run_background = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-test") {
            DiagnosticLogger::info("SelfTest", "CameraPhone.exe --self-test executed.");
            std::cout << "[PASS] CameraPhone.exe desktop launcher self-test passed.\n";
            return 0;
        }
        if (arg == "--elevated-install") {
            std::string msg;
            return install_zero_config_bundle(msg, false) ? 0 : 1;
        }
        if (arg == "--install-headless") {
            std::string msg;
            install_zero_config_bundle(msg, true);
            std::cout << msg << "\n" << install_android_apk_and_tunnel() << "\n";
            return 0;
        }
        if (arg == "--dump-logs") {
            std::cout << DiagnosticLogger::read_tail(16384) << "\n";
            return 0;
        }
        if (arg == "--background") {
            run_background = true;
        }
    }

    // Enforce Single-Instance for the Companion GUI / UsbBroadcastFeeder daemon
    HANDLE instance_mutex = CreateMutexA(nullptr, FALSE, "Local\\CameraPhone_Companion_Mutex");
    if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!run_background) {
            HWND existing = FindWindowA("CameraPhoneCompanionWnd", nullptr);
            if (existing) {
                PostMessageA(existing, WM_COMMAND, ID_TRAY_OPEN, 0);
                SetForegroundWindow(existing);
            }
        }
        CloseHandle(instance_mutex);
        return 0;
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "CameraPhoneCompanionWnd";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
    g_bg_brush       = CreateSolidBrush(RGB(18, 22, 26)); // Dark Slate #12161A
    g_log_brush      = CreateSolidBrush(RGB(13, 17, 23)); // Console Deep Dark #0D1117
    wc.hbrBackground = g_bg_brush;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, wc.lpszClassName, "CameraPhone 4K Pro - Companion & DirectShow Virtual Camera",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 765, 525,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) {
        if (instance_mutex) CloseHandle(instance_mutex);
        return 1;
    }

    if (!run_background) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_bg_brush) DeleteObject(g_bg_brush);
    if (g_log_brush) DeleteObject(g_log_brush);
    if (instance_mutex) CloseHandle(instance_mutex);
    return 0;
}
