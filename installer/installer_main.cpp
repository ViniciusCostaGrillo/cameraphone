#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#define ID_BTN_INSTALL    3001
#define ID_BTN_CANCEL     3002
#define ID_CHK_AUTOSTART  3003
#define ID_CHK_DESKTOP    3004
#define ID_CHK_OBS        3005
#define ID_CHK_LAUNCH     3006
#define ID_PROGRESS_BAR   3007

static HWND g_hwnd            = nullptr;
static HWND g_title_hwnd      = nullptr;
static HWND g_sub_hwnd        = nullptr;
static HWND g_dir_lbl_hwnd    = nullptr;
static HWND g_dir_val_hwnd    = nullptr;
static HWND g_chk_autostart   = nullptr;
static HWND g_chk_desktop     = nullptr;
static HWND g_chk_obs         = nullptr;
static HWND g_chk_launch      = nullptr;
static HWND g_status_hwnd     = nullptr;
static HWND g_progress_hwnd   = nullptr;
static HWND g_btn_install     = nullptr;
static HWND g_btn_cancel      = nullptr;

static HBRUSH g_bg_brush      = nullptr;
static HBRUSH g_card_brush    = nullptr;

static bool g_installation_done = false;

static bool is_elevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elv{};
        DWORD cb = sizeof(elv);
        if (GetTokenInformation(token, TokenElevation, &elv, sizeof(elv), &cb)) {
            elevated = elv.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated != FALSE;
}

static void relaunch_elevated(int nCmdShow, const std::string& args = "") {
    char self[MAX_PATH]{};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    SHELLEXECUTEINFOA sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = self;
    sei.lpParameters = args.c_str();
    sei.nShow = nCmdShow;
    ShellExecuteExA(&sei);
}

static HRESULT create_shell_shortcut(LPCWSTR targetPath, LPCWSTR shortcutPath, LPCWSTR description, LPCWSTR iconPath) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath);
        psl->SetDescription(description);
        if (iconPath && iconPath[0]) {
            psl->SetIconLocation(iconPath, 0);
        }
        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath, TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return hr;
}

static bool set_autostart_registry(const fs::path& exe_path) {
    HKEY hKey = nullptr;
    LPCSTR subkey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        std::string val = "\"" + exe_path.string() + "\" --background";
        LONG res = RegSetValueExA(hKey, "CameraPhone", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(val.c_str()),
                                  static_cast<DWORD>(val.length() + 1));
        RegCloseKey(hKey);
        return res == ERROR_SUCCESS;
    }
    return false;
}

static bool extract_payload_to(const fs::path& target_dir) {
    HRSRC hRes = FindResourceA(nullptr, MAKEINTRESOURCEA(1001), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hMem = LoadResource(nullptr, hRes);
    if (!hMem) return false;
    void* pData = LockResource(hMem);
    DWORD resSize = SizeofResource(nullptr, hRes);
    if (!pData || resSize == 0) return false;

    char temp_dir[MAX_PATH]{};
    GetTempPathA(MAX_PATH, temp_dir);
    fs::path zip_path = fs::path(temp_dir) / "cameraphone_payload.zip";

    std::ofstream out(zip_path, std::ios::binary);
    if (!out) return false;
    out.write(static_cast<const char*>(pData), resSize);
    out.close();

    std::error_code ec;
    fs::create_directories(target_dir, ec);

    // Try extracting with built-in Windows tar.exe (fastest, standard in Win 10/11)
    std::string tar_cmd = "tar.exe -xf \"" + zip_path.string() + "\" -C \"" + target_dir.string() + "\"";
    int rc = system(tar_cmd.c_str());
    if (rc != 0) {
        // Fallback to PowerShell Expand-Archive
        std::string ps_cmd = "powershell -NoProfile -Command \"Expand-Archive -Path '" +
                             zip_path.string() + "' -DestinationPath '" + target_dir.string() + "' -Force\"";
        rc = system(ps_cmd.c_str());
    }

    fs::remove(zip_path, ec);
    return (rc == 0);
}

static void perform_installation() {
    EnableWindow(g_btn_install, FALSE);
    EnableWindow(g_btn_cancel, FALSE);

    auto set_status = [](const std::string& txt, int progress) {
        if (g_status_hwnd) SetWindowTextA(g_status_hwnd, txt.c_str());
        if (g_progress_hwnd) SendMessageA(g_progress_hwnd, PBM_SETPOS, static_cast<WPARAM>(progress), 0);
    };

    set_status("Extraindo arquivos em C:\\Program Files\\CameraPhone...", 20);

    const fs::path target_dir = R"(C:\Program Files\CameraPhone)";
    if (!extract_payload_to(target_dir)) {
        set_status("Erro ao extrair pacote de instalação.", 0);
        EnableWindow(g_btn_install, TRUE);
        EnableWindow(g_btn_cancel, TRUE);
        return;
    }

    set_status("Registrando Câmera Virtual DirectShow (NVIDIA Broadcast)...", 45);
    const fs::path vcam_dll = target_dir / "cameraphone-vcam.dll";
    if (fs::exists(vcam_dll)) {
        std::string reg_cmd = "regsvr32.exe /s \"" + vcam_dll.string() + "\"";
        system(reg_cmd.c_str());
    }

    // OBS Studio Plugin check
    const bool install_obs = (SendMessageA(g_chk_obs, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (install_obs) {
        set_status("Configurando plugin no OBS Studio...", 65);
        const fs::path obs_root = R"(C:\Program Files\obs-studio)";
        const fs::path obs_plugins_dir = obs_root / "obs-plugins" / "64bit";
        const fs::path obs_data_dir = obs_root / "data" / "obs-plugins" / "cameraphone";
        const fs::path obs_adb_dir = obs_data_dir / "adb";

        std::error_code ec;
        if (fs::exists(obs_plugins_dir)) {
            // Remove legacy plugin name if present
            fs::remove(obs_plugins_dir / "cameraphone-obs.dll", ec);

            fs::copy_file(target_dir / "cameraphone.dll", obs_plugins_dir / "cameraphone.dll",
                          fs::copy_options::overwrite_existing, ec);
            fs::copy_file(target_dir / "cameraphone-vcam.dll", obs_plugins_dir / "cameraphone-vcam.dll",
                          fs::copy_options::overwrite_existing, ec);

            fs::create_directories(obs_adb_dir, ec);
            if (fs::exists(target_dir / "CameraPhone.apk")) {
                fs::copy_file(target_dir / "CameraPhone.apk", obs_data_dir / "CameraPhone.apk",
                              fs::copy_options::overwrite_existing, ec);
            }
            if (fs::exists(target_dir / "adb")) {
                for (const auto& entry : fs::directory_iterator(target_dir / "adb")) {
                    fs::copy_file(entry.path(), obs_adb_dir / entry.path().filename(),
                                  fs::copy_options::overwrite_existing, ec);
                }
            }
        }
    }

    // Desktop & Start Menu Shortcuts
    const bool make_desktop = (SendMessageA(g_chk_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (make_desktop) {
        set_status("Criando atalhos na Área de Trabalho e Menu Iniciar...", 80);
        CoInitialize(nullptr);

        char desk_path[MAX_PATH]{};
        SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk_path);
        fs::path desk_lnk = fs::path(desk_path) / "CameraPhone 4K Pro.lnk";

        char prog_path[MAX_PATH]{};
        SHGetFolderPathA(nullptr, CSIDL_PROGRAMS, nullptr, 0, prog_path);
        fs::path prog_lnk = fs::path(prog_path) / "CameraPhone 4K Pro.lnk";

        std::wstring target_w = (target_dir / "CameraPhone.exe").wstring();
        create_shell_shortcut(target_w.c_str(), desk_lnk.wstring().c_str(), L"CameraPhone 4K Pro Companion", target_w.c_str());
        create_shell_shortcut(target_w.c_str(), prog_lnk.wstring().c_str(), L"CameraPhone 4K Pro Companion", target_w.c_str());

        CoUninitialize();
    }

    // Windows Autostart on Boot
    const bool autostart = (SendMessageA(g_chk_autostart, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (autostart) {
        set_status("Configurando inicialização automática com o Windows...", 90);
        set_autostart_registry(target_dir / "CameraPhone.exe");
    }

    set_status("Instalação concluída com sucesso!", 100);

    g_installation_done = true;
    SetWindowTextA(g_btn_install, "Concluir");
    EnableWindow(g_btn_install, TRUE);
    ShowWindow(g_btn_cancel, SW_HIDE);
    ShowWindow(g_chk_launch, SW_SHOW);
    SendMessageA(g_chk_launch, BM_SETCHECK, BST_CHECKED, 0);
}

static LRESULT CALLBACK InstallerWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark_mode, sizeof(dark_mode));
        DwmSetWindowAttribute(hwnd, 19, &dark_mode, sizeof(dark_mode));

        HFONT font_title = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_sub = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_ui = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT font_btn = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        HICON hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
        if (hIcon) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }

        // Header
        g_title_hwnd = CreateWindowExA(0, "STATIC", "Instalador do CameraPhone 4K Pro",
                                       WS_CHILD | WS_VISIBLE, 24, 18, 500, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_title_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        g_sub_hwnd = CreateWindowExA(0, "STATIC", "Câmera Virtual NVIDIA Broadcast, Servidor USB e Plugin OBS",
                                     WS_CHILD | WS_VISIBLE, 24, 46, 500, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_sub_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_sub), TRUE);

        // Destination Directory
        g_dir_lbl_hwnd = CreateWindowExA(0, "STATIC", "Pasta de Destino:",
                                         WS_CHILD | WS_VISIBLE, 24, 82, 200, 18, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_dir_lbl_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        g_dir_val_hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "C:\\Program Files\\CameraPhone",
                                         WS_CHILD | WS_VISIBLE | ES_READONLY,
                                         24, 102, 532, 26, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_dir_val_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Options Checkboxes
        g_chk_autostart = CreateWindowExA(0, "BUTTON", "Iniciar automaticamente com o Windows (Fundo / Bandeja)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          24, 142, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_AUTOSTART), nullptr, nullptr);
        SendMessageA(g_chk_autostart, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_autostart, BM_SETCHECK, BST_CHECKED, 0);

        g_chk_desktop = CreateWindowExA(0, "BUTTON", "Criar atalho na Área de Trabalho e Menu Iniciar",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        24, 172, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_DESKTOP), nullptr, nullptr);
        SendMessageA(g_chk_desktop, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_desktop, BM_SETCHECK, BST_CHECKED, 0);

        g_chk_obs = CreateWindowExA(0, "BUTTON", "Instalar Plugin nativo no OBS Studio (C:\\Program Files\\obs-studio)",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    24, 202, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_OBS), nullptr, nullptr);
        SendMessageA(g_chk_obs, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_obs, BM_SETCHECK, BST_CHECKED, 0);

        // Post-install launch checkbox (hidden initially)
        g_chk_launch = CreateWindowExA(0, "BUTTON", "Iniciar o CameraPhone 4K Pro agora",
                                       WS_CHILD | BS_AUTOCHECKBOX,
                                       24, 236, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_LAUNCH), nullptr, nullptr);
        SendMessageA(g_chk_launch, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Status text
        g_status_hwnd = CreateWindowExA(0, "STATIC", "Clique em 'Instalar Agora' para iniciar a instalação rápida.",
                                        WS_CHILD | WS_VISIBLE, 24, 268, 532, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_status_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Progress Bar
        InitCommonControls();
        g_progress_hwnd = CreateWindowExA(0, PROGRESS_CLASSA, nullptr,
                                          WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                          24, 292, 532, 20, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS_BAR), nullptr, nullptr);
        SendMessageA(g_progress_hwnd, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // Buttons
        g_btn_install = CreateWindowExA(0, "BUTTON", "Instalar Agora",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        316, 332, 130, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL), nullptr, nullptr);
        SendMessageA(g_btn_install, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        g_btn_cancel = CreateWindowExA(0, "BUTTON", "Cancelar",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       456, 332, 100, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
        SendMessageA(g_btn_cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        HWND ctl = reinterpret_cast<HWND>(lparam);
        if (ctl == g_title_hwnd) {
            SetTextColor(hdc, RGB(0, 229, 255)); // Cyan #00E5FF
        } else if (ctl == g_status_hwnd) {
            SetTextColor(hdc, RGB(0, 230, 118)); // Mint #00E676
        } else {
            SetTextColor(hdc, RGB(220, 230, 240));
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_bg_brush);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        SetTextColor(hdc, RGB(240, 244, 248));
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_bg_brush);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wparam);
        SetTextColor(hdc, RGB(201, 209, 217));
        SetBkColor(hdc, RGB(24, 30, 37));
        return reinterpret_cast<LRESULT>(g_card_brush);
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == ID_BTN_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_BTN_INSTALL) {
            if (g_installation_done) {
                // If launch checkbox is checked, launch app
                if (SendMessageA(g_chk_launch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    ShellExecuteA(nullptr, "open", "C:\\Program Files\\CameraPhone\\CameraPhone.exe", nullptr, nullptr, SW_SHOW);
                }
                DestroyWindow(hwnd);
                return 0;
            }

            // Run installation in background worker thread to keep UI smooth
            std::thread([]() {
                perform_installation();
            }).detach();
            return 0;
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
    bool silent = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "/S" || arg == "--silent" || arg == "-s") {
            silent = true;
        }
    }

    if (!is_elevated()) {
        std::string args;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) args += " ";
            args += argv[i];
        }
        relaunch_elevated(silent ? SW_HIDE : SW_SHOW, args);
        return 0;
    }

    if (silent) {
        // Headless silent installation
        const fs::path target_dir = R"(C:\Program Files\CameraPhone)";
        if (extract_payload_to(target_dir)) {
            const fs::path vcam_dll = target_dir / "cameraphone-vcam.dll";
            if (fs::exists(vcam_dll)) {
                std::string reg_cmd = "regsvr32.exe /s \"" + vcam_dll.string() + "\"";
                system(reg_cmd.c_str());
            }
            set_autostart_registry(target_dir / "CameraPhone.exe");
            return 0;
        }
        return 1;
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc   = InstallerWndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "CameraPhoneInstallerWnd";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
    g_bg_brush       = CreateSolidBrush(RGB(18, 22, 26)); // Dark Slate #12161A
    g_card_brush     = CreateSolidBrush(RGB(24, 30, 37)); // Card Slate #181E25
    wc.hbrBackground = g_bg_brush;
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(
        0, wc.lpszClassName, "Instalação do CameraPhone 4K Pro",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 595, 420,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_bg_brush) DeleteObject(g_bg_brush);
    if (g_card_brush) DeleteObject(g_card_brush);
    return 0;
}
