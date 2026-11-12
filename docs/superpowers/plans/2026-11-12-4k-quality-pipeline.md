# End-to-End 4K Quality Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver true, sharp 4K UHD (`3840x2160`) video across NVIDIA Broadcast (`CameraPhone Virtual Camera`), OBS Studio (`CameraPhone 4K Pro`), and Android (including 4K30-only devices) over both USB and Wi-Fi.

**Architecture:** Update the Desktop Companion to request 4K60 by default instead of 1080p, reorder the DirectShow VCam media types to advertise 4K first and replace nearest-neighbor scaling with bilinear Y-plane scaling, default the OBS plugin to 4K, and update the Android camera/encoder pipeline to detect per-resolution max FPS (`getOutputMinFrameDuration`), try `4K@30fps` before dropping resolution, and raise USB/Wi-Fi bitrates.

**Tech Stack:** C++20 (MinGW-w64 `g++`), DirectShow COM, OBS ABI v30, Kotlin (Android Camera2 + MediaCodec), Python build/test scripts.

**Spec:** `docs/superpowers/specs/2026-11-12-4k-quality-pipeline-design.md`

## Global Constraints
- Preserve all existing security checks (`SecurityGate`, zero-trust LAN/USB rules) and OBS canvas transform behavior.
- Maintain C++20 `-Wall -Wextra` clean compilation.
- Maintain backward compatibility for 1080p/720p devices and DirectShow clients.

## Review Focus
- A 4K30-only phone receiving a `4K@60fps` request must negotiate `3840x2160 @ 30fps` cleanly rather than dropping to `1080p30`.
- Wi-Fi 4K streaming must receive sufficient bitrate (50 Mbps) instead of 15 Mbps.
- `BlitOrScaleNV12` must never read out of bounds on edge pixels (`sx1`, `sy1`) during bilinear interpolation.
- `CameraPhone.exe` must not stay hardcoded to `1920x1080`.
- Rebuilt Windows DLLs/EXEs must be copied to `dist/`, root, and OBS plugin directory (`C:\Program Files\obs-studio\obs-plugins\64bit`) and VCam registered.

---

### Task 1: Fix Desktop Companion & OBS Plugin 4K Defaults

**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp:336-344`
- Modify: `obs-plugin/src/cameraphone-source.cpp:217-218, 387, 686-695`

- [ ] **Step 1: Update `desktop-app/cameraphone_companion.cpp` to request 4K60**
  Change `cfg.width = 3840; cfg.height = 2160; cfg.fps = 60;` in `UsbVcamFeeder::WorkerLoop()`.
- [ ] **Step 2: Update `obs-plugin/src/cameraphone-source.cpp` defaults to 4K**
  Set `current_width{3840}`, `current_height{2160}`, `source_get_defaults` `"resolution"` to `"3840x2160"`, and fallback dimensions in `source_get_width`/`source_get_height` to `3840`/`2160`.

---

### Task 2: Improve DirectShow Virtual Camera (`cameraphone-vcam.dll`) 4K Priority & Bilinear Scaling

**Files:**
- Modify: `vcam/cameraphone-vcam.cpp:183-184, 411-430, 960-1030`

- [ ] **Step 1: Reorder `GetMediaType` indices**
  Advertise `4K60 NV12` (index 0) and `4K30 NV12` (index 1) first, followed by `1080p60 NV12`, `1080p30 NV12`, `720p60 NV12`, `720p30 NV12`, `1080p60 YUY2`, `1080p30 YUY2`. Set initial `m_Width = 3840`, `m_Height = 2160`.
- [ ] **Step 2: Replace nearest-neighbor Y-plane scaling with bilinear interpolation in `BlitOrScaleNV12`**
  When `src_w != dst_w || src_h != dst_h` (both NV12 and YUY2 paths), compute bilinearly interpolated Y samples from `(sx0, sy0)`, `(sx1, sy0)`, `(sx0, sy1)`, `(sx1, sy1)` clamped to `[0, src_w - 1]` and `[0, src_h - 1]`.

---

### Task 3: Fix Android 4K30 Detection, Bitrates & Encoder Fallback Ladder

**Files:**
- Modify: `android-app/app/src/main/java/com/cameraphone/app/camera/Camera2Controller.kt:30-78`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/service/CameraStreamService.kt:235-252`
- Modify: `android-app/app/src/main/java/com/cameraphone/app/camera/HardwareVideoEncoder.kt:43-62`

- [ ] **Step 1: Clamp requested FPS per-resolution in `Camera2Controller.pickSupportedResolutionAndFps`**
  Query `map?.getOutputMinFrameDuration(MediaCodec::class.java, chosenSize)`. If `minDurationNs > 0`, compute `maxSensorFpsAtSize = (1_000_000_000L / minDurationNs).toInt().coerceAtLeast(15)` and clamp `effectiveRequestedFps = minOf(requestedFps, maxSensorFpsAtSize)` before selecting from `CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES`.
- [ ] **Step 2: Raise studio bitrates in `CameraStreamService.startHardwarePipeline`**
  Use:
  - USB: `4K -> 100_000_000`, `1440p -> 50_000_000`, `<=1080p -> 35_000_000`
  - Wi-Fi: `4K -> 50_000_000`, `1440p -> 30_000_000`, `<=1080p -> 20_000_000`
- [ ] **Step 3: Fix fallback ladder in `HardwareVideoEncoder.start`**
  Use `(mime, width, height, fps)` -> `(mime, width, height, minOf(fps, 30))` (so 4K60 falls back to 4K30 first!) -> `(mime, 2560, 1440, minOf(fps, 60))` -> `(mime, 1920, 1080, minOf(fps, 60))` -> `(AVC, 1920, 1080, 30)` -> `(AVC, 1280, 720, 30)` with bitrate scaled appropriately (`>= 20 Mbps` for 1080p, never 8 Mbps).

---

### Task 4: Build, Verify & Deploy Artifacts

- [ ] **Step 1: Build Windows binaries (`cameraphone.dll`, `cameraphone-vcam.dll`, `CameraPhone.exe`)**
  Run `python obs-plugin/build_plugin.py`
- [ ] **Step 2: Build Windows installer (`CameraPhone-Setup.exe`)**
  Run `python installer/build_installer.py`
- [ ] **Step 3: Copy updated Windows binaries to root & OBS plugins, and verify runtime**
  Run `python tools/verify_artifacts_runtime.py`
- [ ] **Step 4: Run Android JVM tests & assemble APK if Android SDK is configured**
  Run `python android-app/build_and_test.py test` and `python android-app/build_and_test.py assembleDebug` (or report if Android SDK is missing).
