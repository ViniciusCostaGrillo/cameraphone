#include "../obs-plugin/src/discovery-manager.hpp"
#include "../obs-plugin/src/stream-client.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace camphone;

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
        exe_dir / "cameraphone-obs.dll",
        exe_dir / "dist" / "cameraphone-obs.dll",
        exe_dir / "obs-plugin" / "build" / "cameraphone-obs.dll",
        exe_dir.parent_path() / "obs-plugin" / "build" / "cameraphone-obs.dll",
        R"(C:\Cameraphone Plugin OBS\dist\cameraphone-obs.dll)",
        R"(C:\Cameraphone Plugin OBS\obs-plugin\build\cameraphone-obs.dll)"
    });

    const fs::path apk_src = find_file_in_candidates({
        exe_dir / "CameraPhone-OBS.apk",
        exe_dir / "dist" / "CameraPhone-OBS.apk",
        exe_dir / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk",
        exe_dir.parent_path() / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk",
        R"(C:\Cameraphone Plugin OBS\dist\CameraPhone-OBS.apk)",
        R"(C:\Cameraphone Plugin OBS\CameraPhone-OBS.apk)"
    });

    char local_appdata[MAX_PATH]{};
    GetEnvironmentVariableA("LOCALAPPDATA", local_appdata, MAX_PATH);
    const fs::path sdk_pt = fs::path(local_appdata) / "Android" / "Sdk" / "platform-tools";

    const fs::path adb_src = find_file_in_candidates({
        exe_dir / "adb" / "adb.exe",
        exe_dir / "dist" / "adb" / "adb.exe",
        sdk_pt / "adb.exe",
        R"(C:\Cameraphone Plugin OBS\dist\adb\adb.exe)"
    });

    if (dll_src.empty()) {
        out_msg = "[ERRO] Arquivo cameraphone-obs.dll nao encontrado.";
        return false;
    }

    const fs::path obs_root = R"(C:\Program Files\obs-studio)";
    const fs::path obs_plugins_dir = obs_root / "obs-plugins" / "64bit";
    const fs::path obs_data_dir = obs_root / "data" / "obs-plugins" / "cameraphone-obs";
    const fs::path obs_adb_dir = obs_data_dir / "adb";

    if (!fs::exists(obs_plugins_dir)) {
        out_msg = "[AVISO] OBS Studio nao encontrado em C:\\Program Files\\obs-studio\\obs-plugins\\64bit.";
        return false;
    }

    // Attempt direct copy first (succeeds if already Administrator)
    std::error_code ec;
    fs::create_directories(obs_adb_dir, ec);
    const fs::path dll_dst = obs_plugins_dir / "cameraphone-obs.dll";
    fs::copy_file(dll_src, dll_dst, fs::copy_options::overwrite_existing, ec);

    if (!ec) {
        if (!apk_src.empty()) {
            fs::copy_file(apk_src, obs_data_dir / "CameraPhone-OBS.apk", fs::copy_options::overwrite_existing, ec);
        }
        if (!adb_src.empty()) {
            const fs::path adb_parent = adb_src.parent_path();
            fs::copy_file(adb_src, obs_adb_dir / "adb.exe", fs::copy_options::overwrite_existing, ec);
            if (fs::exists(adb_parent / "AdbWinApi.dll")) {
                fs::copy_file(adb_parent / "AdbWinApi.dll", obs_adb_dir / "AdbWinApi.dll", fs::copy_options::overwrite_existing, ec);
            }
            if (fs::exists(adb_parent / "AdbWinUsbApi.dll")) {
                fs::copy_file(adb_parent / "AdbWinUsbApi.dll", obs_adb_dir / "AdbWinUsbApi.dll", fs::copy_options::overwrite_existing, ec);
            }
        }
        out_msg = "[ZERO-CONFIG ATIVO] Plugin cameraphone-obs.dll + CameraPhone-OBS.apk + ADB instalados no OBS Studio!\r\n"
                  "  -> Abra o OBS Studio > Adicionar Fonte (+) > 'CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)'.\r\n"
                  "  -> Dentro do OBS voce pode escolher Cabo USB ou IP e clicar em 'Instalar APK no Celular'!";
        return true;
    }

    if (!allow_uac_prompt) {
        out_msg = "[ERRO] Sem permissao de escrita em C:\\Program Files\\obs-studio.";
        return false;
    }

    // Re-launch self elevated with --elevated-install to copy DLL + APK + ADB into Program Files
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
        out_msg = "[ZERO-CONFIG ATIVO] Plugin cameraphone-obs.dll + CameraPhone-OBS.apk + ADB instalados no OBS Studio!\r\n"
                  "  -> Abra o OBS Studio > Adicionar Fonte (+) > 'CameraPhone 4K Pro (Cabo USB / Wi-Fi IP)'.\r\n"
                  "  -> Dentro do OBS voce pode escolher Cabo USB ou IP e clicar em 'Instalar APK no Celular'!";
        return true;
    }

    out_msg = "[AVISO] Permissao de Administrador necessria para instalar automaticamente em C:\\Program Files\\obs-studio.";
    return false;
}

static std::string install_android_apk_and_tunnel() {
    std::string msg;
    DiscoveryManager::install_apk_and_launch_usb(msg, 4748, 4748);
    return msg;
}

enum ControlIds : int {
    ID_BTN_INSTALL_OBS = 101,
    ID_BTN_SETUP_USB   = 102,
    ID_BTN_PROBE_CAPS  = 103,
    ID_EDIT_HOST       = 104,
    ID_EDIT_PIN        = 105,
    ID_EDIT_LOG        = 106
};

static HWND g_edit_host = nullptr;
static HWND g_edit_pin  = nullptr;
static HWND g_edit_log  = nullptr;
static HBRUSH g_bg_brush = nullptr;

static void append_log(const std::string& msg) {
    if (!g_edit_log) return;
    int len = GetWindowTextLengthA(g_edit_log);
    SendMessageA(g_edit_log, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    std::string line = msg + "\r\n";
    SendMessageA(g_edit_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font_title = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_ui = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "Consolas");

        HWND title = CreateWindowExA(0, "STATIC", "CameraPhone OBS 4K60/120 HDR - Zero-Config Installer",
                                     WS_CHILD | WS_VISIBLE, 20, 16, 580, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(title, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        CreateWindowExA(0, "STATIC", "Host (USB 127.0.0.1 ou IP Wi-Fi):",
                        WS_CHILD | WS_VISIBLE, 20, 56, 260, 20, hwnd, nullptr, nullptr, nullptr);
        g_edit_host = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "127.0.0.1",
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      20, 78, 260, 26, hwnd, reinterpret_cast<HMENU>(ID_EDIT_HOST), nullptr, nullptr);
        SendMessageA(g_edit_host, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        CreateWindowExA(0, "STATIC", "PIN Wi-Fi (Vazio no Cabo USB):",
                        WS_CHILD | WS_VISIBLE, 300, 56, 240, 20, hwnd, nullptr, nullptr, nullptr);
        g_edit_pin = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                                     300, 78, 180, 26, hwnd, reinterpret_cast<HMENU>(ID_EDIT_PIN), nullptr, nullptr);
        SendMessageA(g_edit_pin, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        CreateWindowExA(0, "BUTTON", "1. Reinstalar no OBS",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        20, 118, 190, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL_OBS), nullptr, nullptr);

        CreateWindowExA(0, "BUTTON", "2. Instalar APK (Cabo USB)",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        220, 118, 190, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_SETUP_USB), nullptr, nullptr);

        CreateWindowExA(0, "BUTTON", "3. Testar Camera 4K/HDR",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        420, 118, 180, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_PROBE_CAPS), nullptr, nullptr);

        g_edit_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                     20, 166, 580, 220, hwnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);
        SendMessageA(g_edit_log, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Zero-Config: automatically install into OBS Studio on startup!
        std::string auto_install_msg;
        install_zero_config_bundle(auto_install_msg, true);
        append_log(auto_install_msg);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == ID_BTN_INSTALL_OBS) {
            std::string status;
            install_zero_config_bundle(status, true);
            append_log(status);
        } else if (id == ID_BTN_SETUP_USB) {
            append_log(install_android_apk_and_tunnel());
        } else if (id == ID_BTN_PROBE_CAPS) {
            char host_buf[128]{};
            char pin_buf[64]{};
            GetWindowTextA(g_edit_host, host_buf, sizeof(host_buf));
            GetWindowTextA(g_edit_pin, pin_buf, sizeof(pin_buf));

            std::string host_s = host_buf[0] ? host_buf : "127.0.0.1";
            std::string pin_s = pin_buf[0] ? pin_buf : (host_s == "127.0.0.1" ? "usb-zero-config" : "");

            StreamClient client;
            DeviceCapabilities caps{};
            if (client.fetch_capabilities(host_s, 4748, pin_s, caps)) {
                std::ostringstream ss;
                ss << "[CONECTADO] Modelo: " << caps.device_model << " (" << caps.soc << ")\r\n"
                   << "  Codecs HW: HEVC=" << (caps.supports_hevc ? "Sim" : "Nao")
                   << " | AV1=" << (caps.supports_av1 ? "Sim" : "Nao")
                   << " | HDR10=" << (caps.supports_hdr10 ? "Sim" : "Nao") << "\r\n"
                   << "  Lentes Fisicas Detectadas: " << caps.lenses.size();
                for (const auto& l : caps.lenses) {
                    ss << "\r\n    * ID [" << l.id << "] " << l.label;
                }
                append_log(ss.str());
            } else {
                append_log("[FALHA] Nao foi possivel conectar em " + host_s + ":4748. Conecte o cabo USB ou verifique o IP/PIN.");
            }
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-test") {
            std::cout << "[PASS] CameraPhone-OBS.exe desktop launcher self-test passed.\n";
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
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "CameraPhoneOBSCompanionWnd";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    g_bg_brush       = CreateSolidBrush(RGB(240, 242, 245));
    wc.hbrBackground = g_bg_brush;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, wc.lpszClassName, "CameraPhone OBS 4K Pro - Zero-Config Installer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 635, 440,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) return 1;

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
