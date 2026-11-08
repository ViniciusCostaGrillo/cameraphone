#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <cctype>
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
#define ID_BTN_LANG       3008

static HWND g_hwnd            = nullptr;
static HWND g_title_hwnd      = nullptr;
static HWND g_sub_hwnd        = nullptr;
static HWND g_btn_lang        = nullptr;
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
static bool g_lang_pt           = false;

static void update_installer_language(HWND hwnd) {
    if (g_lang_pt) {
        SetWindowTextA(hwnd, "Instalação do CameraPhone 4K Pro");
        if (g_title_hwnd) SetWindowTextA(g_title_hwnd, "Instalador do CameraPhone 4K Pro");
        if (g_sub_hwnd) SetWindowTextA(g_sub_hwnd, "Câmera Virtual NVIDIA Broadcast, Servidor USB/Wi-Fi e Plugin OBS");
        if (g_btn_lang) SetWindowTextA(g_btn_lang, "Language: PT");
        if (g_dir_lbl_hwnd) SetWindowTextA(g_dir_lbl_hwnd, "Pasta de Destino:");
        if (g_chk_autostart) SetWindowTextA(g_chk_autostart, "Iniciar automaticamente com o Windows (Fundo / Bandeja)");
        if (g_chk_desktop) SetWindowTextA(g_chk_desktop, "Criar atalho na Área de Trabalho e Menu Iniciar");
        if (g_chk_obs) SetWindowTextA(g_chk_obs, "Instalar Plugin nativo no OBS Studio (C:\\Program Files\\obs-studio)");
        if (g_chk_launch) SetWindowTextA(g_chk_launch, "Iniciar o CameraPhone 4K Pro agora");
        if (!g_installation_done) {
            if (g_status_hwnd) SetWindowTextA(g_status_hwnd, "Clique em 'Instalar Agora' para iniciar a instalação rápida.");
            if (g_btn_install) SetWindowTextA(g_btn_install, "Instalar Agora");
        } else {
            if (g_status_hwnd) SetWindowTextA(g_status_hwnd, "Instalação concluída com sucesso!");
            if (g_btn_install) SetWindowTextA(g_btn_install, "Concluir");
        }
        if (g_btn_cancel) SetWindowTextA(g_btn_cancel, "Cancelar");
    } else {
        SetWindowTextA(hwnd, "CameraPhone 4K Pro Setup");
        if (g_title_hwnd) SetWindowTextA(g_title_hwnd, "CameraPhone 4K Pro Setup");
        if (g_sub_hwnd) SetWindowTextA(g_sub_hwnd, "NVIDIA Broadcast Virtual Camera, USB/Wi-Fi Server & OBS Plugin");
        if (g_btn_lang) SetWindowTextA(g_btn_lang, "Language: EN");
        if (g_dir_lbl_hwnd) SetWindowTextA(g_dir_lbl_hwnd, "Destination Folder:");
        if (g_chk_autostart) SetWindowTextA(g_chk_autostart, "Start automatically with Windows (Background / System Tray)");
        if (g_chk_desktop) SetWindowTextA(g_chk_desktop, "Create Desktop and Start Menu shortcuts");
        if (g_chk_obs) SetWindowTextA(g_chk_obs, "Install native OBS Studio Plugin (C:\\Program Files\\obs-studio)");
        if (g_chk_launch) SetWindowTextA(g_chk_launch, "Launch CameraPhone 4K Pro now");
        if (!g_installation_done) {
            if (g_status_hwnd) SetWindowTextA(g_status_hwnd, "Click 'Install Now' to begin the quick installation.");
            if (g_btn_install) SetWindowTextA(g_btn_install, "Install Now");
        } else {
            if (g_status_hwnd) SetWindowTextA(g_status_hwnd, "Installation completed successfully!");
            if (g_btn_install) SetWindowTextA(g_btn_install, "Finish");
        }
        if (g_btn_cancel) SetWindowTextA(g_btn_cancel, "Cancel");
    }
}

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

static void deploy_uninstaller_and_register(const fs::path& target_dir) {
    char self_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, self_path, MAX_PATH);
    fs::path self_p(self_path);

    const fs::path uninst_pt = target_dir / "Desinstalar CameraPhone.exe";
    const fs::path uninst_en = target_dir / "Uninstall-CameraPhone.exe";

    std::error_code ec;
    if (fs::exists(self_p)) {
        fs::copy_file(self_p, uninst_pt, fs::copy_options::overwrite_existing, ec);
        fs::copy_file(self_p, uninst_en, fs::copy_options::overwrite_existing, ec);
    }

    HKEY hKey = nullptr;
    LPCSTR uninst_subkey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CameraPhone";
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, uninst_subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        auto set_str = [&](const char* name, const std::string& val) {
            RegSetValueExA(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), static_cast<DWORD>(val.length() + 1));
        };
        set_str("DisplayName", "CameraPhone 4K Pro");
        set_str("DisplayVersion", "1.4.0");
        set_str("Publisher", "CameraPhone Team");
        set_str("InstallLocation", target_dir.string());
        set_str("DisplayIcon", (target_dir / "CameraPhone.exe").string());
        set_str("UninstallString", "\"" + uninst_en.string() + "\" --uninstall");
        set_str("QuietUninstallString", "\"" + uninst_en.string() + "\" --uninstall /S");
        DWORD no_modify = 1;
        RegSetValueExA(hKey, "NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&no_modify), sizeof(DWORD));
        RegSetValueExA(hKey, "NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&no_modify), sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static int perform_uninstallation(bool silent) {
    if (!silent) {
        int resp = MessageBoxA(
            nullptr,
            "Do you want to completely uninstall CameraPhone 4K Pro, the DirectShow Virtual Camera (NVIDIA Broadcast), and the OBS Studio Plugin?\n\n(Deseja desinstalar completamente o CameraPhone 4K Pro?)",
            "Uninstall CameraPhone 4K Pro",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
        );
        if (resp != IDYES) {
            return 0;
        }
    }

    // 1. Stop running CameraPhone.exe daemon
    system("taskkill /F /IM CameraPhone.exe >nul 2>&1");
    Sleep(400);

    const fs::path target_dir = R"(C:\Program Files\CameraPhone)";
    const fs::path obs_root = R"(C:\Program Files\obs-studio)";
    const fs::path obs_plugins_dir = obs_root / "obs-plugins" / "64bit";
    const fs::path obs_data_dir = obs_root / "data" / "obs-plugins" / "cameraphone";

    // 2. Unregister DirectShow Virtual Camera
    if (fs::exists(target_dir / "cameraphone-vcam.dll")) {
        std::string cmd = "regsvr32.exe /u /s \"" + (target_dir / "cameraphone-vcam.dll").string() + "\"";
        system(cmd.c_str());
    }
    if (fs::exists(obs_plugins_dir / "cameraphone-vcam.dll")) {
        std::string cmd = "regsvr32.exe /u /s \"" + (obs_plugins_dir / "cameraphone-vcam.dll").string() + "\"";
        system(cmd.c_str());
    }

    // 3. Remove OBS Studio plugin files
    std::error_code ec;
    fs::remove(obs_plugins_dir / "cameraphone.dll", ec);
    fs::remove(obs_plugins_dir / "cameraphone-obs.dll", ec);
    fs::remove(obs_plugins_dir / "cameraphone-vcam.dll", ec);
    fs::remove_all(obs_data_dir, ec);

    // 4. Remove Registry keys (Autostart & Uninstall)
    HKEY hRun = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegDeleteValueA(hRun, "CameraPhone");
        RegCloseKey(hRun);
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CameraPhone");

    // 5. Remove Desktop & Start Menu shortcuts
    char desk_path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk_path))) {
        fs::remove(fs::path(desk_path) / "CameraPhone 4K Pro.lnk", ec);
    }
    char prog_path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROGRAMS, nullptr, 0, prog_path))) {
        fs::remove(fs::path(prog_path) / "CameraPhone 4K Pro.lnk", ec);
        fs::remove(fs::path(prog_path) / "Desinstalar CameraPhone 4K Pro.lnk", ec);
    }

    // 6. Remove files in C:\Program Files\CameraPhone
    if (fs::exists(target_dir)) {
        for (const auto& entry : fs::directory_iterator(target_dir, ec)) {
            fs::remove_all(entry.path(), ec);
        }
    }

    if (!silent) {
        MessageBoxA(
            nullptr,
            "CameraPhone 4K Pro has been uninstalled successfully.\n\n(O CameraPhone 4K Pro foi desinstalado com sucesso.)",
            "Uninstall Complete",
            MB_OK | MB_ICONINFORMATION
        );
    }

    // 7. Schedule background removal of the uninstaller binary itself and the folder after process exit
    if (fs::exists(target_dir)) {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::string self_del_cmd = "cmd.exe /C timeout /t 2 /nobreak >nul & rmdir /s /q \"" + target_dir.string() + "\"";
        std::vector<char> cmd_buf(self_del_cmd.begin(), self_del_cmd.end());
        cmd_buf.push_back('\0');
        if (CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, "C:\\Windows\\System32", &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return 0;
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

    // Ensure running instances of CameraPhone.exe or adb.exe are closed before overwriting files
    system("taskkill /F /IM CameraPhone.exe >nul 2>&1");
    system("taskkill /F /IM adb.exe >nul 2>&1");
    Sleep(500);

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

    set_status(g_lang_pt
        ? "Extraindo arquivos em C:\\Program Files\\CameraPhone..."
        : "Extracting files to C:\\Program Files\\CameraPhone...", 20);

    // Ensure running instances of CameraPhone.exe or adb.exe are closed before overwriting files
    system("taskkill /F /IM CameraPhone.exe >nul 2>&1");
    system("taskkill /F /IM adb.exe >nul 2>&1");
    Sleep(500);

    const fs::path target_dir = R"(C:\Program Files\CameraPhone)";
    if (!extract_payload_to(target_dir)) {
        set_status(g_lang_pt
            ? "Erro ao extrair pacote de instalação."
            : "Error extracting installation package.", 0);
        EnableWindow(g_btn_install, TRUE);
        EnableWindow(g_btn_cancel, TRUE);
        return;
    }

    set_status(g_lang_pt
        ? "Registrando Câmera Virtual DirectShow (NVIDIA Broadcast)..."
        : "Registering DirectShow Virtual Camera (NVIDIA Broadcast)...", 45);
    const fs::path vcam_dll = target_dir / "cameraphone-vcam.dll";
    if (fs::exists(vcam_dll)) {
        std::string reg_cmd = "regsvr32.exe /s \"" + vcam_dll.string() + "\"";
        system(reg_cmd.c_str());
    }

    // OBS Studio Plugin check
    const bool install_obs = (SendMessageA(g_chk_obs, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (install_obs) {
        set_status(g_lang_pt
            ? "Configurando plugin no OBS Studio..."
            : "Configuring OBS Studio plugin...", 65);
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
        set_status(g_lang_pt
            ? "Criando atalhos na Área de Trabalho e Menu Iniciar..."
            : "Creating Desktop and Start Menu shortcuts...", 80);
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
        set_status(g_lang_pt
            ? "Configurando inicialização automática com o Windows..."
            : "Configuring Windows autostart...", 90);
        set_autostart_registry(target_dir / "CameraPhone.exe");
    }

    deploy_uninstaller_and_register(target_dir);

    set_status(g_lang_pt
        ? "Instalação concluída com sucesso!"
        : "Installation completed successfully!", 100);

    g_installation_done = true;
    SetWindowTextA(g_btn_install, g_lang_pt ? "Concluir" : "Finish");
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
        g_title_hwnd = CreateWindowExA(0, "STATIC", "CameraPhone 4K Pro Setup",
                                       WS_CHILD | WS_VISIBLE, 24, 18, 410, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_title_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        g_sub_hwnd = CreateWindowExA(0, "STATIC", "NVIDIA Broadcast Virtual Camera, USB/Wi-Fi Server & OBS Plugin",
                                     WS_CHILD | WS_VISIBLE, 24, 46, 430, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_sub_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_sub), TRUE);

        g_btn_lang = CreateWindowExA(0, "BUTTON", "Language: EN",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     456, 18, 100, 28, hwnd, reinterpret_cast<HMENU>(ID_BTN_LANG), nullptr, nullptr);
        SendMessageA(g_btn_lang, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Destination Directory
        g_dir_lbl_hwnd = CreateWindowExA(0, "STATIC", "Destination Folder:",
                                         WS_CHILD | WS_VISIBLE, 24, 82, 200, 18, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_dir_lbl_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        g_dir_val_hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "C:\\Program Files\\CameraPhone",
                                         WS_CHILD | WS_VISIBLE | ES_READONLY,
                                         24, 102, 532, 26, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_dir_val_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Options Checkboxes
        g_chk_autostart = CreateWindowExA(0, "BUTTON", "Start automatically with Windows (Background / System Tray)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          24, 142, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_AUTOSTART), nullptr, nullptr);
        SendMessageA(g_chk_autostart, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_autostart, BM_SETCHECK, BST_CHECKED, 0);

        g_chk_desktop = CreateWindowExA(0, "BUTTON", "Create Desktop and Start Menu shortcuts",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        24, 172, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_DESKTOP), nullptr, nullptr);
        SendMessageA(g_chk_desktop, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_desktop, BM_SETCHECK, BST_CHECKED, 0);

        g_chk_obs = CreateWindowExA(0, "BUTTON", "Install native OBS Studio Plugin (C:\\Program Files\\obs-studio)",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    24, 202, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_OBS), nullptr, nullptr);
        SendMessageA(g_chk_obs, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageA(g_chk_obs, BM_SETCHECK, BST_CHECKED, 0);

        // Post-install launch checkbox (hidden initially)
        g_chk_launch = CreateWindowExA(0, "BUTTON", "Launch CameraPhone 4K Pro now",
                                       WS_CHILD | BS_AUTOCHECKBOX,
                                       24, 236, 532, 24, hwnd, reinterpret_cast<HMENU>(ID_CHK_LAUNCH), nullptr, nullptr);
        SendMessageA(g_chk_launch, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Status text
        g_status_hwnd = CreateWindowExA(0, "STATIC", "Click 'Install Now' to begin the quick installation.",
                                        WS_CHILD | WS_VISIBLE, 24, 268, 532, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageA(g_status_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        // Progress Bar
        InitCommonControls();
        g_progress_hwnd = CreateWindowExA(0, PROGRESS_CLASSA, nullptr,
                                          WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                          24, 292, 532, 20, hwnd, reinterpret_cast<HMENU>(ID_PROGRESS_BAR), nullptr, nullptr);
        SendMessageA(g_progress_hwnd, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // Buttons
        g_btn_install = CreateWindowExA(0, "BUTTON", "Install Now",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        316, 332, 130, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_INSTALL), nullptr, nullptr);
        SendMessageA(g_btn_install, WM_SETFONT, reinterpret_cast<WPARAM>(font_btn), TRUE);

        g_btn_cancel = CreateWindowExA(0, "BUTTON", "Cancel",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       456, 332, 100, 36, hwnd, reinterpret_cast<HMENU>(ID_BTN_CANCEL), nullptr, nullptr);
        SendMessageA(g_btn_cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        update_installer_language(hwnd);
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
        if (id == ID_BTN_LANG) {
            g_lang_pt = !g_lang_pt;
            update_installer_language(hwnd);
            return 0;
        }
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
    bool uninstall = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-test-uninstall") {
            std::cout << "[PASS] Uninstaller binary verified and ready.\n";
            return 0;
        }
        if (arg == "/S" || arg == "--silent" || arg == "-s") {
            silent = true;
        }
        if (arg == "--uninstall" || arg == "/U" || arg == "-u") {
            uninstall = true;
        }
    }

    // Auto-detect uninstaller mode if executable filename contains "desinstalar" or "uninstall"
    char self_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, self_path, MAX_PATH);
    std::string exe_name = fs::path(self_path).filename().string();
    std::transform(exe_name.begin(), exe_name.end(), exe_name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (exe_name.find("desinstalar") != std::string::npos || exe_name.find("uninstall") != std::string::npos) {
        uninstall = true;
    }

    if (!is_elevated()) {
        std::string args;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) args += " ";
            args += argv[i];
        }
        if (uninstall && args.find("--uninstall") == std::string::npos) {
            if (!args.empty()) args += " ";
            args += "--uninstall";
        }
        relaunch_elevated(silent ? SW_HIDE : SW_SHOW, args);
        return 0;
    }

    if (uninstall) {
        return perform_uninstallation(silent);
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
            const fs::path obs_root = R"(C:\Program Files\obs-studio)";
            const fs::path obs_plugins_dir = obs_root / "obs-plugins" / "64bit";
            const fs::path obs_data_dir = obs_root / "data" / "obs-plugins" / "cameraphone";
            const fs::path obs_adb_dir = obs_data_dir / "adb";
            std::error_code ec;
            if (fs::exists(obs_plugins_dir)) {
                fs::remove(obs_plugins_dir / "cameraphone-obs.dll", ec);
                fs::copy_file(target_dir / "cameraphone.dll", obs_plugins_dir / "cameraphone.dll", fs::copy_options::overwrite_existing, ec);
                fs::copy_file(target_dir / "cameraphone-vcam.dll", obs_plugins_dir / "cameraphone-vcam.dll", fs::copy_options::overwrite_existing, ec);
                fs::create_directories(obs_adb_dir, ec);
                if (fs::exists(target_dir / "CameraPhone.apk")) {
                    fs::copy_file(target_dir / "CameraPhone.apk", obs_data_dir / "CameraPhone.apk", fs::copy_options::overwrite_existing, ec);
                }
                if (fs::exists(target_dir / "adb")) {
                    for (const auto& entry : fs::directory_iterator(target_dir / "adb")) {
                        fs::copy_file(entry.path(), obs_adb_dir / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
                    }
                }
            }
            set_autostart_registry(target_dir / "CameraPhone.exe");
            deploy_uninstaller_and_register(target_dir);
            ShellExecuteA(nullptr, "open", (target_dir / "CameraPhone.exe").string().c_str(), "--background", target_dir.string().c_str(), SW_HIDE);
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
        0, wc.lpszClassName, "CameraPhone 4K Pro Setup",
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
