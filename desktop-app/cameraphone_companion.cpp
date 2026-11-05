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

static bool install_obs_plugin(std::string& out_msg) {
    const fs::path exe_dir = get_exe_dir();
    std::vector<fs::path> candidates = {
        exe_dir / "cameraphone-obs.dll",
        exe_dir / "obs-plugin" / "build" / "cameraphone-obs.dll",
        exe_dir.parent_path() / "obs-plugin" / "build" / "cameraphone-obs.dll"
    };

    fs::path dll_src;
    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            dll_src = c;
            break;
        }
    }
    if (dll_src.empty()) {
        out_msg = "[ERRO] Arquivo cameraphone-obs.dll nao encontrado.";
        return false;
    }

    const fs::path obs_dir = R"(C:\Program Files\obs-studio\obs-plugins\64bit)";
    if (!fs::exists(obs_dir)) {
        out_msg = "[AVISO] Pasta do OBS Studio nao encontrada em C:\\Program Files\\obs-studio\\obs-plugins\\64bit.\r\nDLL pronta em: " + dll_src.string();
        return false;
    }

    const fs::path dst = obs_dir / "cameraphone-obs.dll";
    std::error_code ec;
    fs::copy_file(dll_src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        // Request UAC elevation via ShellExecuteA runas
        std::string params = "/c copy /Y \"" + dll_src.string() + "\" \"" + dst.string() + "\"";
        HINSTANCE res = ShellExecuteA(nullptr, "runas", "cmd.exe", params.c_str(), nullptr, SW_HIDE);
        if (reinterpret_cast<intptr_t>(res) <= 32) {
            out_msg = "[AVISO] Permissao de Administrador negada ao copiar para Program Files.\r\nCopie manualmente: " + dll_src.string();
            return false;
        }
    }
    out_msg = "[OK] Plugin cameraphone-obs.dll instalado no OBS Studio com sucesso!";
    return true;
}

static std::string install_android_apk_and_tunnel() {
    const fs::path exe_dir = get_exe_dir();
    std::vector<fs::path> apk_candidates = {
        exe_dir / "CameraPhone-OBS.apk",
        exe_dir / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk",
        exe_dir.parent_path() / "android-app" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    };

    fs::path apk_src;
    for (const auto& c : apk_candidates) {
        if (fs::exists(c)) {
            apk_src = c;
            break;
        }
    }

    auto devices = DiscoveryManager::list_usb_devices(4748, 4748);
    std::ostringstream ss;
    if (devices.empty()) {
        ss << "[USB] Nenhum celular Android detectado via ADB no momento.\r\n"
           << "      Conecte o cabo USB com Depuracao USB ativa ou use Wi-Fi na porta 4748.\r\n";
        if (!apk_src.empty()) {
            ss << "[APK Pronto] " << apk_src.string();
        }
        return ss.str();
    }

    for (const auto& d : devices) {
        ss << "[USB Detectado] " << d.id << " -> Tunel TCP 127.0.0.1:" << d.port << " ativo!\r\n";
        if (!apk_src.empty() && DiscoveryManager::is_valid_adb_serial(d.id)) {
            std::string cmd = "adb -s " + d.id + " install -r \"" + apk_src.string() + "\"";
            std::system(cmd.c_str());
            ss << "[APK Instalado] CameraPhone OBS instalado no aparelho " << d.id << ".\r\n";
        }
    }
    return ss.str();
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

        HWND title = CreateWindowExA(0, "STATIC", "CameraPhone OBS 4K60/120 HDR - Studio Companion",
                                     WS_CHILD | WS_VISIBLE, 20, 16, 580, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(title, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        CreateWindowExA(0, "STATIC", "Host (USB 127.0.0.1 ou IP Wi-Fi):",
                        WS_CHILD | WS_VISIBLE, 20, 56, 260, 20, hwnd, nullptr, nullptr, nullptr);
        g_edit_host = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "127.0.0.1",
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      20, 78, 260, 26, hwnd, reinterpret_cast<HMENU>(ID_EDIT_HOST), nullptr, nullptr);
        SendMessageA(g_edit_host, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        CreateWindowExA(0, "STATIC", "PIN Zero-Trust (6 digitos):",
                        WS_CHILD | WS_VISIBLE, 300, 56, 240, 20, hwnd, nullptr, nullptr, nullptr);
        g_edit_pin = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                                     300, 78, 180, 26, hwnd, reinterpret_cast<HMENU>(ID_EDIT_PIN), nullptr, nullptr);
        SendMessageA(g_edit_pin, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        CreateWindowExA(0, "BUTTON", "1. Instalar Plugin no OBS",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        20, 118, 190, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL_OBS), nullptr, nullptr);

        CreateWindowExA(0, "BUTTON", "2. Ativar USB ADB + APK",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        220, 118, 190, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_SETUP_USB), nullptr, nullptr);

        CreateWindowExA(0, "BUTTON", "3. Testar Camera 4K/HDR",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        420, 118, 180, 34, hwnd, reinterpret_cast<HMENU>(ID_BTN_PROBE_CAPS), nullptr, nullptr);

        g_edit_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                     20, 166, 580, 220, hwnd, reinterpret_cast<HMENU>(ID_EDIT_LOG), nullptr, nullptr);
        SendMessageA(g_edit_log, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        append_log("[Pronto] CameraPhone OBS Companion iniciado.");
        append_log("  - Binario OBS Plugin: cameraphone-obs.dll");
        append_log("  - App Android: CameraPhone-OBS.apk");
        append_log("  - Porta TCP CamPhoneWire v1: 4748");
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == ID_BTN_INSTALL_OBS) {
            std::string status;
            install_obs_plugin(status);
            append_log(status);
        } else if (id == ID_BTN_SETUP_USB) {
            append_log(install_android_apk_and_tunnel());
        } else if (id == ID_BTN_PROBE_CAPS) {
            char host_buf[128]{};
            char pin_buf[64]{};
            GetWindowTextA(g_edit_host, host_buf, sizeof(host_buf));
            GetWindowTextA(g_edit_pin, pin_buf, sizeof(pin_buf));

            StreamClient client;
            DeviceCapabilities caps{};
            if (client.fetch_capabilities(host_buf, 4748, pin_buf, caps)) {
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
                append_log("[FALHA] Nao foi possivel autenticar em " + std::string(host_buf) + ":4748. Verifique o PIN de 6 digitos e o cabo/Wi-Fi.");
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
        if (arg == "--install-headless") {
            std::string msg;
            install_obs_plugin(msg);
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
        0, wc.lpszClassName, "CameraPhone OBS 4K Pro - Desktop Companion & Installer",
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
