# Auto-Connect on APK Open & Uninstaller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable instant, flicker-free automatic connection from the Android APK to NVIDIA Broadcast (`CameraPhone Virtual Camera`) upon opening the APK, and install an uninstaller (`Desinstalar CameraPhone.exe` / `Uninstall-CameraPhone.exe`) inside `C:\Program Files\CameraPhone\`.

**Architecture:** Enhance `VCamSharedMemoryWriter` and `VCamSharedMemoryReader` with lazy-open and frame-freshness persistence (`max_age_ms = 2500`) so 20–30 FPS phone streams render smoothly at 60 FPS in DirectShow without Standby flicker; auto-launch `CameraPhone.exe --background` when `CVCamFilter` starts and keep `CameraPhone.exe` alive in the system tray on `WM_CLOSE`; extend `installer_main.cpp` to deploy and register an uninstaller in `C:\Program Files\CameraPhone\`.

**Tech Stack:** C++20, Win32 API, DirectShow (`IBaseFilter`, `IAMStreamConfig`, `IMemInputPin`), Kotlin (Android SDK 34).

**Spec:** `docs/superpowers/specs/2026-11-07-auto-connect-and-uninstaller-design.md`

## Global Constraints

- Only modify code strictly necessary for the two requested features.
- Maintain ABI compatibility with OBS Studio v30 (`obs_module_ver() == (30 << 24)`) and NVIDIA Broadcast (`PIN_CATEGORY_CAPTURE`, `NV12` / `YUY2`).

## Review Focus

- Reader opened before Writer creates shared memory (`VCamSharedMemoryReader` must lazy-open when Writer starts later).
- 60 Hz consumer reading a 20–30 FPS producer (`read_frame` must return the last valid frame across intermediate ticks without falling back to Standby until `> 2500 ms` of inactivity).
- Resolution or format mismatch between producer (1080p NV12) and DirectShow pin (e.g. 720p/4K NV12 or 1080p YUY2).
- Closing `CameraPhone.exe` window via `[X]` must hide to tray rather than killing the background USB feeder.
- Running `Desinstalar CameraPhone.exe` from `C:\Program Files\CameraPhone\` must cleanly unregister the VCam, remove OBS files, remove registry keys, and delete the installation folder.

---

### Task 1: Shared Memory Lazy-Open, Frame Freshness Persistence & Format/Scale Helper

**Files:**
- Modify: `protocol/vcam_shmem.hpp`
- Modify: `vcam/cameraphone-vcam.cpp`
- Modify: `tests/test_vcam_dshow.cpp`

- [ ] **Step 1: Write the failing test in `tests/test_vcam_dshow.cpp`**
  Test that:
  1. A `VCamSharedMemoryReader` created *before* `VCamSharedMemoryWriter::open()` automatically opens and reads a frame once the writer writes one.
  2. Calling `read_frame()` multiple times at 60 Hz without a new `write_frame()` continues to return `true` while the frame is fresh (`< 2500 ms`), and returns `false` when `max_age_ms` is exceeded.
- [ ] **Step 2: Run the test to confirm it fails**
- [ ] **Step 3: Implement lazy-open, writer tick timestamp, and NV12/YUY2 scaling/conversion in `protocol/vcam_shmem.hpp` and `vcam/cameraphone-vcam.cpp`**
- [ ] **Step 4: Run `tests/test_vcam_dshow.exe` and verify all checks pass**

### Task 2: Companion Daemon Tray Persistence, Fast APK Detection & VCam Auto-Launch

**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp`
- Modify: `vcam/cameraphone-vcam.cpp`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/ui/MainActivity.kt`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/service/CameraStreamService.kt`

- [ ] **Step 1: Update `desktop-app/cameraphone_companion.cpp`**
  - Handle `WM_CLOSE` in `WndProc` by hiding the window (`ShowWindow(hwnd, SW_HIDE); return 0;`) so the background USB feeder stays active.
  - Reduce USB-connected APK polling sleep in `UsbBroadcastFeeder::run()` from `2000 ms` to `600 ms` and post-stream cooldown from `1500 ms` to `500 ms`.
- [ ] **Step 2: Update `vcam/cameraphone-vcam.cpp`**
  - In `CVCamFilter::StartWorker()`, if neither `Local\CameraPhone_Companion_Mutex` nor `Local\CameraPhone_OBS_Streaming_Mutex` is active, launch `C:\Program Files\CameraPhone\CameraPhone.exe --background` via `CreateProcessA`.
- [ ] **Step 3: Update `MainActivity.kt` & `CameraStreamService.kt`**
  - Add `onPermissionsGranted()` in `CameraStreamService` to retry starting the active stream pipeline if `CAMERA` permission was just granted on first run.

### Task 3: Application Folder Uninstaller (`Desinstalar CameraPhone.exe` / `Uninstall-CameraPhone.exe`)

**Files:**
- Modify: `installer/installer_main.cpp`
- Modify: `tools/verify_artifacts_runtime.py`

- [ ] **Step 1: Add uninstaller verification in `tools/verify_artifacts_runtime.py`**
- [ ] **Step 2: Implement uninstaller creation, registry registration, and `--uninstall` mode in `installer/installer_main.cpp`**
  - Copy installer binary to `C:\Program Files\CameraPhone\Desinstalar CameraPhone.exe` and `C:\Program Files\CameraPhone\Uninstall-CameraPhone.exe`.
  - Register `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\CameraPhone`.
  - Support `--uninstall` (and auto-trigger when executable filename contains `Desinstalar` or `Uninstall`).
- [ ] **Step 3: Rebuild all 5 artifacts, run `CameraPhone-Setup.exe /S`, and run `python tools/verify_artifacts_runtime.py`**
