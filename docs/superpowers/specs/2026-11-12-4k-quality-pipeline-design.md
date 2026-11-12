# Design: End-to-End 4K Quality Pipeline (NVIDIA Broadcast + OBS + Android)

**Date:** 2026-11-12
**Status:** Approved

## Intent & Why
Deliver true, sharp **4K UHD (`3840x2160`)** video with high bitrate and zero artificial limits across both **NVIDIA Broadcast (`CameraPhone Virtual Camera`)** and **OBS Studio (`CameraPhone 4K Pro`)**, over both **USB** and **Wi-Fi**.

## Goals, Non-Goals & Anti-Goals
- **Goals:**
  - Eliminate silent 1080p/720p downgrades in `CameraPhone.exe`, `cameraphone.dll`, and `HardwareVideoEncoder.kt`.
  - Ensure `cameraphone-vcam.dll` negotiates 4K cleanly with NVIDIA Broadcast and uses smooth bilinear scaling (never nearest-neighbor) if a downstream app requests a different resolution.
  - Increase Wi-Fi and USB bitrates so 4K/1080p video remains crisp in fullscreen without compression blockiness.
  - Rebuild all Windows artifacts (`CameraPhone.exe`, `cameraphone.dll`, `cameraphone-vcam.dll`, `CameraPhone-Setup.exe`) and `CameraPhone.apk` (when Android SDK is available; otherwise provide clear build/install status).
- **Non-Goals:**
  - Changing the wire protocol (`CamPhoneWire v1`) or breaking existing OBS canvas transforms / security gate behavior.
- **Anti-Goals:**
  - Upscaling 1080p to 4K and presenting it as 4K.

## Approaches Considered
1. **Abordagem 1 (Selected): Full End-to-End 4K Pipeline Fix (Windows + VCam + OBS + Android)**
   - Fixes resolution defaults, bitrate limits, encoder fallback ladder, and VCam scaling/format priority across all 4 components.
   - *Trade-off:* Requires rebuilding both Windows binaries and the Android APK, but resolves both resolution and compression quality everywhere.
2. **Abordagem 2 (Rejected): Windows-Only Resolution Defaults**
   - Only changes `CameraPhone.exe`, `cameraphone-vcam.dll`, and `cameraphone.dll`.
   - *Trade-off:* Leaves Wi-Fi capped at 15 Mbps and Android's encoder falling straight from 4K60 to 1080p30 @ 8 Mbps.

## Design Summary (The How)
1. **Desktop Companion (`desktop-app/cameraphone_companion.cpp`):**
   - Change hardcoded `cfg.width = 1920; cfg.height = 1080; cfg.fps = is_usb ? 60 : 30;` to `cfg.width = 3840; cfg.height = 2160; cfg.fps = 60;` (and `caps.supports_hevc ? "hevc" : "avc"`).
2. **DirectShow Virtual Camera (`vcam/cameraphone-vcam.cpp`):**
   - Reorder `GetMediaType` so `4K60 NV12` (index 0) and `4K30 NV12` (index 1) are advertised first, followed by `1080p60/30 NV12`, `720p60/30 NV12`, and `YUY2` formats.
   - Replace nearest-neighbor scaling in `BlitOrScaleNV12` with bilinear interpolation on the Y plane when `src_w != dst_w || src_h != dst_h`.
3. **OBS Native Plugin (`obs-plugin/src/cameraphone-source.cpp`):**
   - Change default `"resolution"` in `source_get_defaults` from `"1920x1080"` to `"3840x2160"`, and initialize `current_width{3840}` / `current_height{2160}`.
4. **Android App (`android-app/.../Camera2Controller.kt`, `CameraStreamService.kt` & `HardwareVideoEncoder.kt`):**
   - In `Camera2Controller.kt`: Use `SCALER_STREAM_CONFIGURATION_MAP.getOutputMinFrameDuration(MediaCodec::class.java, chosenSize)` to clamp `requestedFps` to what the sensor actually supports at `chosenSize` (e.g., if a device only supports `3840x2160` at `30fps` even though `1080p` supports `60fps`, `pickSupportedResolutionAndFps` returns `3840x2160` @ `30fps` cleanly).
   - Increase bitrates in `CameraStreamService.kt`:
     - USB: `4K -> 100 Mbps`, `<4K -> 35 Mbps`
     - Wi-Fi: `4K -> 50 Mbps`, `<4K -> 20 Mbps`
   - Fix `HardwareVideoEncoder.kt` fallback ladder so `4K @ fps` first tries `4K @ 30fps` (and `2560x1440 @ fps`) with high bitrate before falling back to `1920x1080` (at 20 Mbps instead of 8 Mbps) and finally `1280x720`.

## Left to the Builder
- Verify compilation of all Windows binaries with `g++` (MSYS2 UCRT64) and register `cameraphone-vcam.dll` + copy `cameraphone.dll` to OBS plugins folder.
- Check if Android SDK is present to rebuild `CameraPhone.apk`, or document if Android SDK needs to be installed/configured.
