# Auto-Connect on APK Open (NVIDIA Broadcast) & App Folder Uninstaller — Design Spec

**Date:** 2026-11-07
**Project:** CameraPhone 4K Pro (`c:\Cameraphone Plugin OBS`)

## 1. Goal
1. **Zero-Touch Auto-Connect to NVIDIA Broadcast (`CameraPhone Virtual Camera`) on APK Launch:** When the user opens the `CameraPhone` APK on an Android phone connected via USB cable, the PC companion (`CameraPhone.exe`) and the DirectShow Virtual Camera (`cameraphone-vcam.dll`) must automatically connect and deliver continuous 60 FPS video frames into NVIDIA Broadcast without manual intervention, flickering, or getting stuck on Standby.
2. **Uninstaller in Installation Directory (`C:\Program Files\CameraPhone\`):** After installation via `CameraPhone-Setup.exe`, the installation folder `C:\Program Files\CameraPhone\` must include a ready-to-use uninstaller (`Desinstalar CameraPhone.exe` and `Uninstall-CameraPhone.exe`) registered in Windows Installed Apps (`HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\CameraPhone`).

## 2. Root Cause Analysis & Architecture

### 2.1 DirectShow Shared Memory & Frame Persistence (`protocol/vcam_shmem.hpp` & `vcam/cameraphone-vcam.cpp`)
- **Problem 1 (Late Producer Startup):** `CVCamFilter::WorkerLoop()` called `shmem_reader.open()` only once before entering its `while` loop. If NVIDIA Broadcast opened `CameraPhone Virtual Camera` before `CameraPhone.exe` started writing to `Local\CameraPhone_VCam_Shmem`, `shmem_reader.is_open()` remained `false` forever.
- **Problem 2 (60 Hz Tick vs 20–30 FPS Camera Stream Flicker):** `CVCamFilter::WorkerLoop()` runs at 60 FPS (`16.6 ms`) and required `shmem_reader.wait_for_frame(8)` (an auto-reset event) to return `true` on every single tick, otherwise it overwrote the sample buffer with `VCamStandbyRenderer::render_nv12()`. Because phone cameras often stream at 20–30 FPS in indoor lighting, 30–40 ticks per second timed out on `wait_for_frame(8)` and overwrote the live camera image with the Standby screen.
- **Problem 3 (Resolution / Subtype Mismatch):** If NVIDIA Broadcast or another DirectShow host negotiates 720p, 4K, or `YUY2` while the USB feeder decodes 1080p `NV12`, a strict `payload.size() == needed_nv12` check fails or copies mismatched stride data.
- **Solution:**
  - Store the producer's monotonic tick (`GetTickCount64()`) in `VCamSharedHeader::linesize[3]` (or dedicated field/timestamp) on every `write_frame()`.
  - `VCamSharedMemoryReader::read_frame(out_hdr, out_payload, max_age_ms = 2500)` automatically calls `open()` if not yet mapped, validates magic/version/size, and checks that the frame was written within `max_age_ms` (`GetTickCount64() - writer_tick_ms <= max_age_ms`).
  - `CVCamFilter::WorkerLoop()` uses `wait_for_frame(4)` only as a low-latency wakeup hint, then calls `shmem_reader.read_frame(hdr, payload, 2500)`. If a fresh frame is present, it renders it into `pBuffer` (supporting direct copy when dimensions match NV12, nearest-neighbor scaling when NV12 dimensions differ, and NV12-to-YUY2 conversion when `MEDIASUBTYPE_YUY2` is active).

### 2.2 Companion Daemon Lifecycle & Auto-Start (`desktop-app/cameraphone_companion.cpp` & `vcam/cameraphone-vcam.cpp`)
- **Problem:** Closing the `CameraPhone.exe` window via the `[X]` button (`WM_CLOSE`) called `DestroyWindow(hwnd)`, which stopped `g_feeder` and killed the background USB daemon. Additionally, if `CameraPhone.exe` was not running when NVIDIA Broadcast opened the camera, nothing started it.
- **Solution:**
  - Handle `WM_CLOSE` in `WndProc` (`desktop-app/cameraphone_companion.cpp`) by hiding the window to the system tray (`ShowWindow(hwnd, SW_HIDE); return 0;`), while `ID_TRAY_EXIT` ("Sair" / "Exit") continues to call `DestroyWindow(hwnd)` to terminate the process.
  - Reduce the USB-connected APK polling interval in `UsbBroadcastFeeder::run()` from `2000 ms` to `600 ms` so opening the APK on the phone connects in `< 1 second`.
  - In `CVCamFilter::StartWorker()` (`vcam/cameraphone-vcam.cpp`), check if either `Local\CameraPhone_Companion_Mutex` or `Local\CameraPhone_OBS_Streaming_Mutex` exists; if neither is held, automatically spawn `C:\Program Files\CameraPhone\CameraPhone.exe --background` via `CreateProcessA(..., CREATE_NO_WINDOW, ...)`.

### 2.3 First-Run Android Permission Grant (`MainActivity.kt` & `CameraStreamService.kt`)
- **Solution:** When `CAMERA` permission is granted in `MainActivity.onRequestPermissionsResult`, invoke `streamService?.onPermissionsGranted()` so if a client is already connected to `/v1/stream`, the hardware camera pipeline starts immediately.

### 2.4 Application Uninstaller (`installer/installer_main.cpp`)
- **Installation Step:**
  - During both GUI (`perform_installation()`) and silent (`/S`) installation, copy the installer binary itself into:
    - `C:\Program Files\CameraPhone\Desinstalar CameraPhone.exe`
    - `C:\Program Files\CameraPhone\Uninstall-CameraPhone.exe`
  - Register `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\CameraPhone` with `DisplayName`, `UninstallString`, `QuietUninstallString`, `DisplayIcon`, `Publisher`, `DisplayVersion` (`1.3.2`), and `InstallLocation`.
- **Uninstallation Mode (`--uninstall` or executable name containing `Desinstalar` / `Uninstall`):**
  - Elevate via UAC (`runas`) if not already elevated.
  - If not `/S`, show a confirmation `MessageBoxA` (or clean uninstall progress).
  - Terminate running `CameraPhone.exe` (`taskkill /F /IM CameraPhone.exe`).
  - Unregister `cameraphone-vcam.dll` (`regsvr32.exe /u /s`) in both `C:\Program Files\CameraPhone` and `C:\Program Files\obs-studio\obs-plugins\64bit`.
  - Remove OBS Studio plugin files (`cameraphone.dll`, `cameraphone-vcam.dll`, `data\obs-plugins\cameraphone`).
  - Remove Desktop and Start Menu shortcuts (`CameraPhone 4K Pro.lnk`, `Desinstalar CameraPhone 4K Pro.lnk`).
  - Remove `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\CameraPhone` and `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\CameraPhone`.
  - Remove files in `C:\Program Files\CameraPhone` and schedule self-deletion of the remaining executable and directory via `cmd.exe /c timeout /t 2 /nobreak >nul & rmdir /s /q "C:\Program Files\CameraPhone"`.
