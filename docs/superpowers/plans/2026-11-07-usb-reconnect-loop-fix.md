# USB Reconnect Loop Fix & OBS/Companion Coexistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the 1.85s USB connect/disconnect loop in `UsbBroadcastFeeder`, prevent OBS vs. Companion stream contention, enforce a single instance of `CameraPhone.exe`, and fix stream replacement cleanup in `CamPhoneServer.kt`.

**Architecture:** Fix the async connection startup check in `UsbBroadcastFeeder::run()` so it waits for the initial handshake instead of immediately calling `stop_streaming()`. Add a named Windows mutex (`Local\CameraPhone_OBS_Streaming_Mutex`) held by the OBS source while active so `UsbBroadcastFeeder` yields cleanly to OBS, and a single-instance mutex (`Local\CameraPhone_Companion_Mutex`) in `CameraPhone.exe`. Fix `CamPhoneServer.kt` to compare `activeStreamOutput === out` before stopping the camera on disconnect.

**Tech Stack:** C++20, Win32 API, Kotlin (Android API 28–34), JUnit, LLVM-MinGW.

**Spec:** `docs/superpowers/specs/2026-11-07-usb-reconnect-loop-fix-design.md`

## Global Constraints
- Target Windows 10/11 64-bit; statically linked C++20 binaries (`-static`).
- Zero warnings under `-Wall -Wextra -Werror`.

## Review Focus
- Slow Android camera open (~500ms) before HTTP `200 OK`: `UsbBroadcastFeeder` must not treat the pre-connected startup window as a disconnect.
- OBS Studio opened while `UsbBroadcastFeeder` is streaming: `UsbBroadcastFeeder` must detect `OBSStreamGuard::is_obs_streaming()` within 100ms and stop its `StreamClient` without fighting OBS.
- User double-clicks `CameraPhone.exe` while `--background` tray instance is already running: second process must restore the existing window and exit `0` instead of spawning a second `UsbBroadcastFeeder`.
- Intentional `StreamClient::stop_streaming()`: worker thread must not call `DiscoveryManager::ensure_usb_tunnel_ready` when `running_` is `false`.
- Second client connects to `/v1/stream` on Android: old client thread's `finally` block must not call `onStopStream()` if `activeStreamOutput` now points to the new client's `OutputStream`.

---

### Task 1: Fix Android `CamPhoneServer.kt` Stream Replacement Cleanup

**Files:**
- Modify: `android-app/app/src/main/java/com/cameraphone/app/net/CamPhoneServer.kt:143-210`
- Modify: `android-app/app/src/test/java/com/cameraphone/app/AllTests.kt`

**Interfaces:**
- Produces: Safe stream handoff in `CamPhoneServer` when a new `/v1/stream` connection replaces a previous one.

- [ ] **Step 1: Add unit test `replacementStreamDoesNotTriggerStopStreamFromOldConnection` in `android-app/app/src/test/java/com/cameraphone/app/AllTests.kt`**
- [ ] **Step 2: Update `CamPhoneServer.kt` so `finally` checks `activeStreamOutput === out` under `synchronized(writeLock)` before clearing `activeStreamOutput` and calling `onStopStream()`**
- [ ] **Step 3: Run Android unit tests and assemble APK via `python android-app/build_and_test.py test` and `python android-app/build_and_test.py assembleDebug`**

### Task 2: Add `OBSStreamGuard` IPC & Fix `StreamClient` Shutdown

**Files:**
- Modify: `protocol/vcam_shmem.hpp`
- Modify: `obs-plugin/src/stream-client.hpp`
- Modify: `obs-plugin/src/stream-client.cpp`
- Modify: `obs-plugin/src/cameraphone-source.cpp`
- Modify: `obs-plugin/tests/test_client_and_discovery.cpp`

**Interfaces:**
- Produces:
  - `camphone::OBSStreamGuard` in `protocol/vcam_shmem.hpp` (`acquire()`, `release()`, `static bool is_obs_streaming()`) using named mutex `Local\CameraPhone_OBS_Streaming_Mutex`.
  - `bool StreamClient::is_running() const noexcept` in `obs-plugin/src/stream-client.hpp`.

- [ ] **Step 1: Add test in `obs-plugin/tests/test_client_and_discovery.cpp` verifying `OBSStreamGuard::is_obs_streaming()` and `StreamClient::start_streaming` sustained connection without premature disconnect**
- [ ] **Step 2: Implement `OBSStreamGuard` in `protocol/vcam_shmem.hpp` and hold it in `CameraPhoneSourceContext::start_pipeline()` / release in `stop_pipeline_locked()`**
- [ ] **Step 3: Update `StreamClient` in `obs-plugin/src/stream-client.hpp` and `stream-client.cpp` so `!running_.load()` skips `ensure_usb_tunnel_ready` on intentional stop**

### Task 3: Fix `UsbBroadcastFeeder` Race Condition & Add Single-Instance Mutex in `CameraPhone.exe`

**Files:**
- Modify: `desktop-app/cameraphone_companion.cpp`

**Interfaces:**
- Consumes: `camphone::OBSStreamGuard::is_obs_streaming()`, `StreamClient::is_running()`

- [ ] **Step 1: Update `UsbBroadcastFeeder::run()` in `desktop-app/cameraphone_companion.cpp`**
  - Yield immediately when `OBSStreamGuard::is_obs_streaming()` is `true`.
  - Wait up to 4000ms for `m_client.is_connected()` to become `true` after `m_client.start_streaming(...)` before entering the active streaming loop.
  - Avoid redundant `ensure_usb_tunnel_ready` calls (`list_usb_devices` already ensures tunnel when needed, or call `ensure_usb_tunnel_ready(4748, 4748, false)` once).
- [ ] **Step 2: Add single-instance mutex `Local\CameraPhone_Companion_Mutex` in `main()` of `desktop-app/cameraphone_companion.cpp`**
  - If already running (and no CLI flags like `--self-test`, `--elevated-install`, `--install-headless`, `--dump-logs`), find `CameraPhoneCompanionWnd`, send a custom message `WM_SHOW_COMPANION` (`WM_USER + 101`) to restore it, and exit `0`.
- [ ] **Step 3: Build plugin, companion EXE, and installer (`python obs-plugin/build_plugin.py`, `python installer/build_installer.py`) and run `python tools/verify_artifacts_runtime.py`**
